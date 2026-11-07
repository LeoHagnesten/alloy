#include "mine/gpu.hh"
#include "base/bits.hh"
#include "base/com.hh"
#include "base/memory.hh"
#include "base/strings.hh"
#include <windows.h>
#include <d3d11.h>
#include <dxgi.h>
#include "sha256d.h"
#include "scrypt_start.h"
#include "scrypt_mix.h"
#include "scrypt_finish.h"

namespace mine {

	static constexpr uint32_t scrypt_lane_bytes = 131072;
	static constexpr uint32_t scrypt_state_bytes = 192;

	struct work_constants {

		uint32_t midstate [ 8 ];
		uint32_t tail [ 4 ];
		uint32_t limits [ 4 ];
		uint32_t head [ 16 ];

	};

	using base::drop;

	static IDXGIAdapter1* find_adapter ( uint32_t wanted, adapter_info* out, int32_t room, int32_t& count ) {

		IDXGIFactory1* factory = nullptr;
		IDXGIAdapter1* chosen = nullptr;

		count = 0;

		if ( FAILED ( CreateDXGIFactory1 ( __uuidof ( IDXGIFactory1 ), reinterpret_cast < void** > ( &factory ) ) ) )
			return nullptr;

		IDXGIAdapter1* adapter = nullptr;

		for ( UINT i = 0; factory->EnumAdapters1 ( i, &adapter ) != DXGI_ERROR_NOT_FOUND; i++ ) {

			DXGI_ADAPTER_DESC1 description;

			if ( SUCCEEDED ( adapter->GetDesc1 ( &description ) ) && !( description.Flags & DXGI_ADAPTER_FLAG_SOFTWARE ) ) {

				if ( out && count < room ) {

					base::copy_text ( out [ count ].name, base::count_of ( out [ count ].name ), description.Description );
					out [ count ].memory = description.DedicatedVideoMemory;

				}

				if ( static_cast < uint32_t > ( count ) == wanted && !chosen ) {

					chosen = adapter;
					chosen->AddRef ( );

				}

				count++;

			}

			adapter->Release ( );

		}

		factory->Release ( );

		return chosen;

	}

	int32_t list_adapters ( adapter_info* out, int32_t room ) {

		int32_t count = 0;
		IDXGIAdapter1* none = find_adapter ( ~0u, out, room, count );

		drop ( none );

		return base::min ( count, room );

	}

	gpu_device::~gpu_device ( ) {

		close ( );

	}

	bool gpu_device::open ( uint32_t adapter ) {

		close ( );

		int32_t count = 0;
		IDXGIAdapter1* chosen = find_adapter ( adapter, nullptr, 0, count );

		if ( !chosen ) {

			base::copy_text ( label, base::count_of ( label ), L"No graphics adapter" );

			return false;

		}

		DXGI_ADAPTER_DESC1 description;

		if ( SUCCEEDED ( chosen->GetDesc1 ( &description ) ) ) {

			base::copy_text ( label, base::count_of ( label ), description.Description );
			video_memory = description.DedicatedVideoMemory;

		}

		static const D3D_FEATURE_LEVEL levels [ ] = { D3D_FEATURE_LEVEL_11_1, D3D_FEATURE_LEVEL_11_0 };
		bool ok = SUCCEEDED ( D3D11CreateDevice ( chosen, D3D_DRIVER_TYPE_UNKNOWN, nullptr, D3D11_CREATE_DEVICE_SINGLETHREADED, levels, 2, D3D11_SDK_VERSION, &device, nullptr, &context ) );

		chosen->Release ( );

		IDXGIDevice* dxgi = nullptr;

		if ( ok && SUCCEEDED ( device->QueryInterface ( __uuidof ( IDXGIDevice ), reinterpret_cast < void** > ( &dxgi ) ) ) ) {

			dxgi->SetGPUThreadPriority ( -7 );
			dxgi->Release ( );

		}

		ok = ok && SUCCEEDED ( device->CreateComputeShader ( sha256d_code, sizeof ( sha256d_code ), nullptr, &shader ) );

		D3D11_BUFFER_DESC layout = { };

		layout.ByteWidth = sizeof ( work_constants );
		layout.Usage = D3D11_USAGE_DYNAMIC;
		layout.BindFlags = D3D11_BIND_CONSTANT_BUFFER;
		layout.CPUAccessFlags = D3D11_CPU_ACCESS_WRITE;
		ok = ok && SUCCEEDED ( device->CreateBuffer ( &layout, nullptr, &constants ) );

		layout = { };
		layout.ByteWidth = sample_room * 4;
		layout.Usage = D3D11_USAGE_DEFAULT;
		layout.BindFlags = D3D11_BIND_UNORDERED_ACCESS;
		layout.MiscFlags = D3D11_RESOURCE_MISC_BUFFER_ALLOW_RAW_VIEWS;
		ok = ok && SUCCEEDED ( device->CreateBuffer ( &layout, nullptr, &results ) );

		layout.Usage = D3D11_USAGE_STAGING;
		layout.BindFlags = 0;
		layout.MiscFlags = 0;
		layout.CPUAccessFlags = D3D11_CPU_ACCESS_READ;
		ok = ok && SUCCEEDED ( device->CreateBuffer ( &layout, nullptr, &readback ) );

		D3D11_UNORDERED_ACCESS_VIEW_DESC shape = { };

		shape.Format = DXGI_FORMAT_R32_TYPELESS;
		shape.ViewDimension = D3D11_UAV_DIMENSION_BUFFER;
		shape.Buffer.NumElements = sample_room;
		shape.Buffer.Flags = D3D11_BUFFER_UAV_FLAG_RAW;
		ok = ok && SUCCEEDED ( device->CreateUnorderedAccessView ( results, &shape, &view ) );

		if ( !ok ) {

			close ( );
			base::copy_text ( label, base::count_of ( label ), L"The graphics adapter refused the compute kernel" );

		}

		return ok;

	}

	void gpu_device::close ( ) {

		if ( context )
			context->ClearState ( );

		drop ( state_view );
		drop ( states );
		drop ( table_view );
		drop ( table );
		drop ( view );
		drop ( readback );
		drop ( results );
		drop ( constants );

		for ( ID3D11ComputeShader*& stage : scrypt_stages )
			drop ( stage );

		drop ( shader );
		drop ( context );
		drop ( device );
		scrypt_room = 0;
		scrypt_failed = false;

	}

	bool gpu_device::prepare_scrypt ( ) {

		if ( state_view )
			return true;

		if ( scrypt_failed || !device )
			return false;

		bool ok = SUCCEEDED ( device->CreateComputeShader ( scrypt_start_code, sizeof ( scrypt_start_code ), nullptr, &scrypt_stages [ 0 ] ) );

		ok = ok && SUCCEEDED ( device->CreateComputeShader ( scrypt_mix_code, sizeof ( scrypt_mix_code ), nullptr, &scrypt_stages [ 1 ] ) );
		ok = ok && SUCCEEDED ( device->CreateComputeShader ( scrypt_finish_code, sizeof ( scrypt_finish_code ), nullptr, &scrypt_stages [ 2 ] ) );

		uint64_t budget = video_memory ? video_memory / 4 : 512ull << 20;

		for ( uint32_t lanes = 16384; ok && lanes >= 1024 && !table_view; lanes /= 2 ) {

			if ( static_cast < uint64_t > ( lanes ) * scrypt_lane_bytes > budget && lanes > 1024 )
				continue;

			D3D11_BUFFER_DESC layout = { };

			layout.ByteWidth = lanes * scrypt_lane_bytes;
			layout.Usage = D3D11_USAGE_DEFAULT;
			layout.BindFlags = D3D11_BIND_UNORDERED_ACCESS;
			layout.MiscFlags = D3D11_RESOURCE_MISC_BUFFER_STRUCTURED;
			layout.StructureByteStride = 16;

			if ( FAILED ( device->CreateBuffer ( &layout, nullptr, &table ) ) )
				continue;

			D3D11_UNORDERED_ACCESS_VIEW_DESC shape = { };

			shape.Format = DXGI_FORMAT_UNKNOWN;
			shape.ViewDimension = D3D11_UAV_DIMENSION_BUFFER;
			shape.Buffer.NumElements = lanes * ( scrypt_lane_bytes / 16 );

			if ( FAILED ( device->CreateUnorderedAccessView ( table, &shape, &table_view ) ) ) {

				drop ( table );

				continue;

			}

			scrypt_room = lanes;

		}

		if ( ok && table_view ) {

			D3D11_BUFFER_DESC layout = { };

			layout.ByteWidth = scrypt_room * scrypt_state_bytes;
			layout.Usage = D3D11_USAGE_DEFAULT;
			layout.BindFlags = D3D11_BIND_UNORDERED_ACCESS;
			layout.MiscFlags = D3D11_RESOURCE_MISC_BUFFER_STRUCTURED;
			layout.StructureByteStride = 16;
			ok = SUCCEEDED ( device->CreateBuffer ( &layout, nullptr, &states ) );

			D3D11_UNORDERED_ACCESS_VIEW_DESC shape = { };

			shape.Format = DXGI_FORMAT_UNKNOWN;
			shape.ViewDimension = D3D11_UAV_DIMENSION_BUFFER;
			shape.Buffer.NumElements = scrypt_room * ( scrypt_state_bytes / 16 );
			ok = ok && SUCCEEDED ( device->CreateUnorderedAccessView ( states, &shape, &state_view ) );

		}

		if ( !ok || !table_view || !state_view ) {

			drop ( state_view );
			drop ( states );
			drop ( table_view );
			drop ( table );

			for ( ID3D11ComputeShader*& stage : scrypt_stages )
				drop ( stage );

			scrypt_room = 0;
			scrypt_failed = true;

			return false;

		}

		return true;

	}

	uint32_t gpu_device::capacity ( uint8_t algorithm ) {

		if ( algorithm == chain::ALGORITHM_SCRYPT )
			return prepare_scrypt ( ) ? scrypt_room : 0;

		return algorithm == chain::ALGORITHM_SHA256D && device ? 1u << 28 : 0;

	}

	bool gpu_device::dispatch ( uint8_t algorithm, const chain::header& header, uint32_t first, uint32_t count, uint32_t threshold, bool sampling ) {

		bool scrypt = algorithm == chain::ALGORITHM_SCRYPT;

		if ( !device || algorithm == chain::ALGORITHM_RANDOMX || ( scrypt && !prepare_scrypt ( ) ) )
			return false;

		uint32_t width = scrypt ? 64 : 256;

		if ( scrypt )
			count = base::min ( count, scrypt_room );

		uint32_t groups = base::max ( ( count + width - 1 ) / width, 1u );
		uint32_t across = base::min ( groups, 4096u );
		uint32_t down = ( groups + across - 1 ) / across;
		work_constants values = { };

		for ( int32_t i = 0; i < 8; i++ )
			values.midstate [ i ] = hash::sha256_start [ i ];

		hash::sha256_block ( values.midstate, header.bytes );
		values.tail [ 0 ] = base::load_big ( header.bytes + 64 );
		values.tail [ 1 ] = base::load_big ( header.bytes + 68 );
		values.tail [ 2 ] = base::load_big ( header.bytes + 72 );
		values.tail [ 3 ] = first;
		values.limits [ 0 ] = threshold;
		values.limits [ 1 ] = sampling ? 1 : 0;
		values.limits [ 2 ] = across * width;

		for ( int32_t i = 0; i < 16; i++ )
			values.head [ i ] = base::load_big ( header.bytes + i * 4 );

		D3D11_MAPPED_SUBRESOURCE mapped;

		if ( FAILED ( context->Map ( constants, 0, D3D11_MAP_WRITE_DISCARD, 0, &mapped ) ) )
			return false;

		base::copy ( mapped.pData, &values, sizeof ( values ) );
		context->Unmap ( constants, 0 );

		static const UINT zeros [ 4 ] = { };

		ID3D11UnorderedAccessView* views [ 3 ] = { view, table_view, state_view };

		context->ClearUnorderedAccessViewUint ( view, zeros );
		context->CSSetConstantBuffers ( 0, 1, &constants );
		context->CSSetUnorderedAccessViews ( 0, scrypt ? 3 : 1, views, nullptr );

		if ( !scrypt ) {

			context->CSSetShader ( shader, nullptr, 0 );
			context->Dispatch ( across, down, 1 );

			return true;

		}

		for ( ID3D11ComputeShader* stage : scrypt_stages ) {

			context->CSSetShader ( stage, nullptr, 0 );
			context->Dispatch ( across, down, 1 );

		}

		return true;

	}

	bool gpu_device::read ( uint32_t* out, uint32_t words ) {

		D3D11_BOX box = { 0, 0, 0, words * 4, 1, 1 };
		D3D11_MAPPED_SUBRESOURCE mapped;

		context->CopySubresourceRegion ( readback, 0, 0, 0, 0, results, 0, &box );

		if ( FAILED ( context->Map ( readback, 0, D3D11_MAP_READ, 0, &mapped ) ) )
			return false;

		base::copy ( out, mapped.pData, words * 4 );
		context->Unmap ( readback, 0 );

		return true;

	}

	bool gpu_device::search ( uint8_t algorithm, const chain::header& header, uint32_t first, uint32_t count, uint32_t threshold, uint32_t* found, uint32_t room, uint32_t& found_count ) {

		uint32_t words [ 32 ];

		found_count = 0;

		if ( !dispatch ( algorithm, header, first, count, threshold, false ) || !read ( words, 32 ) )
			return false;

		found_count = base::min ( base::min ( words [ 0 ], 31u ), room );

		for ( uint32_t i = 0; i < found_count; i++ )
			found [ i ] = words [ 1 + i ];

		return true;

	}

	bool gpu_device::sample ( uint8_t algorithm, const chain::header& header, uint32_t first, uint32_t count, uint32_t* tops ) {

		count = base::min ( count, sample_room );

		if ( algorithm == chain::ALGORITHM_SCRYPT )
			count = base::min ( count, capacity ( algorithm ) );

		return count && dispatch ( algorithm, header, first, count, 0, true ) && read ( tops, count );

	}

}
