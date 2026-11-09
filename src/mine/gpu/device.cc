#include "mine/gpu/device.hh"
#include "hash/heavyhash.hh"
#include "hash/ethash.hh"
#include "base/system/clock.hh"
#include "base/system/threads.hh"
#include "base/bits.hh"
#include "base/system/com.hh"
#include "base/memory.hh"
#include "base/strings.hh"
#include <windows.h>
#include <winternl.h>
#include <d3d11.h>
#include <dxgi.h>
#include <d3dkmthk.h>
#include "sha256d.h"
#include "scrypt_start.h"
#include "scrypt_mix.h"
#include "scrypt_finish.h"
#include "kheavyhash.h"
#include "ethash_dag.h"
#include "ethash_search.h"

namespace mine {

	static constexpr uint32_t scrypt_lane_bytes = 65536;
	static constexpr uint32_t scrypt_state_bytes = 192;

	struct work_constants {

		uint32_t midstate [ 8 ];
		uint32_t tail [ 4 ];
		uint32_t limits [ 4 ];
		uint32_t head [ 16 ];

	};

	struct heavy_layout {

		uint64_t pow [ 26 ];
		uint64_t finish [ 26 ];
		uint32_t limits [ 4 ];
		uint32_t rows [ 512 ];

	};

	struct dag_layout {

		uint32_t limits [ 4 ];
		uint32_t place [ 4 ];

	};

	struct search_layout {

		uint32_t header [ 8 ];
		uint32_t limits [ 4 ];
		uint32_t more [ 4 ];

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
		IDXGIAdapter1* none = find_adapter ( ~0u - 1, out, room, count );

		drop ( none );

		return base::min ( count, room );

	}

	gpu_device::~gpu_device ( ) {

		close ( );

	}

	bool gpu_device::open ( uint32_t adapter ) {

		close ( );

		static const D3D_FEATURE_LEVEL levels [ ] = { D3D_FEATURE_LEVEL_11_1, D3D_FEATURE_LEVEL_11_0 };
		bool ok = false;

		if ( adapter == software_adapter ) {

			base::copy_text ( label, base::count_of ( label ), L"Microsoft Basic Render Driver" );
			ok = SUCCEEDED ( D3D11CreateDevice ( nullptr, D3D_DRIVER_TYPE_WARP, nullptr, D3D11_CREATE_DEVICE_SINGLETHREADED, levels, 2, D3D11_SDK_VERSION, &device, nullptr, &context ) );

		} else {

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
				luid_low = description.AdapterLuid.LowPart;
				luid_high = description.AdapterLuid.HighPart;
				has_luid = true;

			}

			ok = SUCCEEDED ( D3D11CreateDevice ( chosen, D3D_DRIVER_TYPE_UNKNOWN, nullptr, D3D11_CREATE_DEVICE_SINGLETHREADED, levels, 2, D3D11_SDK_VERSION, &device, nullptr, &context ) );
			chosen->Release ( );

		}

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

		for ( int32_t i = 0; i < 2 && ok; i++ ) {

			layout = { };
			layout.ByteWidth = sample_room * 4;
			layout.Usage = D3D11_USAGE_DEFAULT;
			layout.BindFlags = D3D11_BIND_UNORDERED_ACCESS;
			layout.MiscFlags = D3D11_RESOURCE_MISC_BUFFER_ALLOW_RAW_VIEWS;
			ok = SUCCEEDED ( device->CreateBuffer ( &layout, nullptr, &results [ i ] ) );

			layout.Usage = D3D11_USAGE_STAGING;
			layout.BindFlags = 0;
			layout.MiscFlags = 0;
			layout.CPUAccessFlags = D3D11_CPU_ACCESS_READ;
			ok = ok && SUCCEEDED ( device->CreateBuffer ( &layout, nullptr, &readback [ i ] ) );

			D3D11_UNORDERED_ACCESS_VIEW_DESC shape = { };

			shape.Format = DXGI_FORMAT_R32_TYPELESS;
			shape.ViewDimension = D3D11_UAV_DIMENSION_BUFFER;
			shape.Buffer.NumElements = sample_room;
			shape.Buffer.Flags = D3D11_BUFFER_UAV_FLAG_RAW;
			ok = ok && SUCCEEDED ( device->CreateUnorderedAccessView ( results [ i ], &shape, &views [ i ] ) );

		}

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

		for ( int32_t i = 0; i < 2; i++ ) {

			drop ( views [ i ] );
			drop ( readback [ i ] );
			drop ( results [ i ] );

		}

		drop_dag ( );
		drop ( idle_query );
		drop ( search_constants );
		drop ( dag_constants );
		drop ( search_shader );
		drop ( dag_shader );
		drop ( constants );
		drop ( heavy_constants );
		drop ( heavy_shader );

		for ( ID3D11ComputeShader*& stage : scrypt_stages )
			drop ( stage );

		drop ( shader );
		drop ( context );
		drop ( device );

		if ( kernel_adapter ) {

			D3DKMT_CLOSEADAPTER closing = { };

			closing.hAdapter = kernel_adapter;
			D3DKMTCloseAdapter ( &closing );

		}

		kernel_adapter = 0;
		has_luid = false;
		scrypt_room = 0;
		scrypt_failed = false;
		heavy_failed = false;
		ethash_failed = false;
		next_slot = 0;

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

		for ( uint32_t lanes = 32768; ok && lanes >= 1024 && !table_view; lanes /= 2 ) {

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

	bool gpu_device::prepare_heavy ( ) {

		if ( heavy_shader && heavy_constants )
			return true;

		if ( heavy_failed || !device )
			return false;

		D3D11_BUFFER_DESC layout = { };

		layout.ByteWidth = sizeof ( heavy_layout );
		layout.Usage = D3D11_USAGE_DYNAMIC;
		layout.BindFlags = D3D11_BIND_CONSTANT_BUFFER;
		layout.CPUAccessFlags = D3D11_CPU_ACCESS_WRITE;

		bool ok = SUCCEEDED ( device->CreateComputeShader ( kheavyhash_code, sizeof ( kheavyhash_code ), nullptr, &heavy_shader ) ) && SUCCEEDED ( device->CreateBuffer ( &layout, nullptr, &heavy_constants ) );

		if ( !ok ) {

			drop ( heavy_constants );
			drop ( heavy_shader );
			heavy_failed = true;

		}

		return ok;

	}

	bool gpu_device::dispatch_heavy ( const hash::heavy_hasher& prepared, uint32_t nonce_high, uint32_t first, uint32_t count, uint32_t threshold, int32_t slot, uint32_t words ) {

		if ( !prepare_heavy ( ) )
			return false;

		uint32_t groups = base::max ( ( count + 255 ) / 256, 1u );
		uint32_t across = base::min ( groups, 4096u );
		uint32_t down = ( groups + across - 1 ) / across;
		heavy_layout* values = new heavy_layout { };

		base::copy ( values->pow, prepared.pow_state ( ), 25 * 8 );
		base::copy ( values->finish, prepared.finish_state ( ), 25 * 8 );
		values->limits [ 0 ] = threshold;
		values->limits [ 1 ] = nonce_high;
		values->limits [ 2 ] = across * 256;
		values->limits [ 3 ] = first;
		hash::heavy_pack ( prepared.matrix ( ), values->rows );

		D3D11_MAPPED_SUBRESOURCE mapped;
		bool mapped_ok = SUCCEEDED ( context->Map ( heavy_constants, 0, D3D11_MAP_WRITE_DISCARD, 0, &mapped ) );

		if ( mapped_ok ) {

			base::copy ( mapped.pData, values, sizeof ( heavy_layout ) );
			context->Unmap ( heavy_constants, 0 );

		}

		delete values;

		if ( !mapped_ok )
			return false;

		static const UINT zeros [ 4 ] = { };

		context->ClearUnorderedAccessViewUint ( views [ slot ], zeros );
		context->CSSetConstantBuffers ( 0, 1, &heavy_constants );
		context->CSSetUnorderedAccessViews ( 0, 1, &views [ slot ], nullptr );
		context->CSSetShader ( heavy_shader, nullptr, 0 );
		context->Dispatch ( across, down, 1 );

		D3D11_BOX box = { 0, 0, 0, words * 4, 1, 1 };

		context->CopySubresourceRegion ( readback [ slot ], 0, 0, 0, 0, results [ slot ], 0, &box );
		context->Flush ( );

		return true;

	}

	bool gpu_device::submit_heavy ( const hash::heavy_hasher& prepared, uint32_t nonce_high, uint32_t first, uint32_t count, uint32_t threshold, int32_t& slot ) {

		slot = next_slot;
		next_slot ^= 1;

		return dispatch_heavy ( prepared, nonce_high, first, count, base::min ( threshold, 0xFFFFFFFEu ), slot, 32 );

	}

	bool gpu_device::sample_heavy ( const hash::heavy_hasher& prepared, uint32_t nonce_high, uint32_t first, uint32_t count, uint32_t* tops ) {

		count = base::min ( count, sample_room );

		int32_t slot = next_slot;

		next_slot ^= 1;

		return count && dispatch_heavy ( prepared, nonce_high, first, count, 0xFFFFFFFFu, slot, count ) && read ( slot, tops, count );

	}

	bool gpu_device::prepare_ethash ( ) {

		if ( dag_shader && search_shader && dag_constants && search_constants && idle_query )
			return true;

		if ( ethash_failed || !device )
			return false;

		D3D11_BUFFER_DESC layout = { };
		D3D11_QUERY_DESC asking = { };

		layout.Usage = D3D11_USAGE_DYNAMIC;
		layout.BindFlags = D3D11_BIND_CONSTANT_BUFFER;
		layout.CPUAccessFlags = D3D11_CPU_ACCESS_WRITE;
		layout.ByteWidth = sizeof ( dag_layout );
		asking.Query = D3D11_QUERY_EVENT;

		bool ok = SUCCEEDED ( device->CreateComputeShader ( ethash_dag_code, sizeof ( ethash_dag_code ), nullptr, &dag_shader ) );

		ok = ok && SUCCEEDED ( device->CreateComputeShader ( ethash_search_code, sizeof ( ethash_search_code ), nullptr, &search_shader ) );
		ok = ok && SUCCEEDED ( device->CreateBuffer ( &layout, nullptr, &dag_constants ) );
		layout.ByteWidth = sizeof ( search_layout );
		ok = ok && SUCCEEDED ( device->CreateBuffer ( &layout, nullptr, &search_constants ) );
		ok = ok && SUCCEEDED ( device->CreateQuery ( &asking, &idle_query ) );

		if ( !ok ) {

			drop ( idle_query );
			drop ( search_constants );
			drop ( dag_constants );
			drop ( search_shader );
			drop ( dag_shader );
			ethash_failed = true;

		}

		return ok;

	}

	bool gpu_device::finish_queue ( ) {

		if ( !idle_query )
			return false;

		context->End ( idle_query );

		BOOL done = FALSE;

		while ( true ) {

			HRESULT state = context->GetData ( idle_query, &done, sizeof ( done ), 0 );

			if ( FAILED ( state ) )
				return false;

			if ( state == S_OK && done )
				return true;

			base::sleep ( 1 );

		}

	}

	void gpu_device::drop_dag ( ) {

		if ( context ) {

			ID3D11ShaderResourceView* empty [ dag_part_room ] = { };

			context->CSSetShaderResources ( 0, dag_part_room, empty );

		}

		for ( uint32_t i = 0; i < dag_part_room; i++ ) {

			drop ( dag_views [ i ] );
			drop ( dag_parts [ i ] );

		}

		dag_part_count = 0;
		dag_rows = 0;
		dag_part_rows = 0;
		dag_epoch = 0;

	}

	bool gpu_device::build_dag ( const hash::ethash_cache& cache, volatile uint32_t* progress, volatile uint32_t* stop ) {

		if ( dag_ready ( cache.epoch ( ) ) )
			return true;

		drop_dag ( );

		if ( !cache.ready ( ) || !prepare_ethash ( ) )
			return false;

		uint64_t limit_bytes = base::min ( base::max ( video_memory / 4, 128ull << 20 ), 2048ull << 20 );
		uint64_t rows = cache.dataset_bytes ( ) / 128;

		dag_part_rows = static_cast < uint32_t > ( limit_bytes / 128 );
		dag_rows = static_cast < uint32_t > ( rows );
		dag_part_count = static_cast < uint32_t > ( ( rows + dag_part_rows - 1 ) / dag_part_rows );

		if ( dag_part_count > dag_part_room ) {

			dag_part_count = 0;

			return false;

		}

		D3D11_BUFFER_DESC layout = { };
		D3D11_SUBRESOURCE_DATA initial = { };
		ID3D11Buffer* cache_buffer = nullptr;
		ID3D11ShaderResourceView* cache_view = nullptr;
		D3D11_SHADER_RESOURCE_VIEW_DESC reading = { };

		layout.ByteWidth = static_cast < UINT > ( cache.bytes ( ) );
		layout.Usage = D3D11_USAGE_IMMUTABLE;
		layout.BindFlags = D3D11_BIND_SHADER_RESOURCE;
		layout.MiscFlags = D3D11_RESOURCE_MISC_BUFFER_ALLOW_RAW_VIEWS;
		initial.pSysMem = cache.words ( );
		reading.Format = DXGI_FORMAT_R32_TYPELESS;
		reading.ViewDimension = D3D11_SRV_DIMENSION_BUFFEREX;
		reading.BufferEx.NumElements = static_cast < UINT > ( cache.bytes ( ) / 4 );
		reading.BufferEx.Flags = D3D11_BUFFEREX_SRV_FLAG_RAW;

		bool ok = SUCCEEDED ( device->CreateBuffer ( &layout, &initial, &cache_buffer ) ) && SUCCEEDED ( device->CreateShaderResourceView ( cache_buffer, &reading, &cache_view ) );
		uint64_t items_total = rows * 2;
		uint64_t items_done = 0;

		for ( uint32_t part = 0; part < dag_part_count && ok; part++ ) {

			uint64_t first_row = static_cast < uint64_t > ( part ) * dag_part_rows;
			uint64_t part_rows = base::min ( static_cast < uint64_t > ( dag_part_rows ), rows - first_row );
			ID3D11UnorderedAccessView* writing = nullptr;
			D3D11_UNORDERED_ACCESS_VIEW_DESC shape = { };

			layout = { };
			layout.ByteWidth = static_cast < UINT > ( part_rows * 128 );
			layout.Usage = D3D11_USAGE_DEFAULT;
			layout.BindFlags = D3D11_BIND_SHADER_RESOURCE | D3D11_BIND_UNORDERED_ACCESS;
			shape.Format = DXGI_FORMAT_R32G32B32A32_UINT;
			shape.ViewDimension = D3D11_UAV_DIMENSION_BUFFER;
			shape.Buffer.NumElements = static_cast < UINT > ( part_rows * 8 );

			D3D11_SHADER_RESOURCE_VIEW_DESC rows_view = { };

			rows_view.Format = DXGI_FORMAT_R32G32B32A32_UINT;
			rows_view.ViewDimension = D3D11_SRV_DIMENSION_BUFFER;
			rows_view.Buffer.NumElements = static_cast < UINT > ( part_rows * 8 );
			ok = SUCCEEDED ( device->CreateBuffer ( &layout, nullptr, &dag_parts [ part ] ) ) && SUCCEEDED ( device->CreateUnorderedAccessView ( dag_parts [ part ], &shape, &writing ) ) && SUCCEEDED ( device->CreateShaderResourceView ( dag_parts [ part ], &rows_view, &dag_views [ part ] ) );

			uint64_t first_item = first_row * 2;
			uint64_t last_item = first_item + part_rows * 2;

			for ( uint64_t at = first_item; at < last_item && ok; at += 1u << 18 ) {

				if ( stop && base::atomic_load ( *stop ) ) {

					ok = false;

					break;

				}

				uint32_t count = static_cast < uint32_t > ( base::min ( 1ull << 18, last_item - at ) );
				uint32_t groups = ( count + 63 ) / 64;
				uint32_t across = base::min ( groups, 4096u );
				uint32_t down = ( groups + across - 1 ) / across;
				dag_layout values = { };
				D3D11_MAPPED_SUBRESOURCE mapped;

				values.limits [ 0 ] = static_cast < uint32_t > ( at );
				values.limits [ 1 ] = count;
				values.limits [ 2 ] = static_cast < uint32_t > ( cache.bytes ( ) / 64 );
				values.limits [ 3 ] = across * 64;
				values.place [ 0 ] = static_cast < uint32_t > ( first_item );

				if ( FAILED ( context->Map ( dag_constants, 0, D3D11_MAP_WRITE_DISCARD, 0, &mapped ) ) ) {

					ok = false;

					break;

				}

				base::copy ( mapped.pData, &values, sizeof ( values ) );
				context->Unmap ( dag_constants, 0 );
				context->CSSetConstantBuffers ( 0, 1, &dag_constants );
				context->CSSetShaderResources ( 0, 1, &cache_view );
				context->CSSetUnorderedAccessViews ( 0, 1, &writing, nullptr );
				context->CSSetShader ( dag_shader, nullptr, 0 );
				context->Dispatch ( across, down, 1 );
				ok = finish_queue ( );
				items_done += count;

				if ( progress )
					base::atomic_store ( *progress, static_cast < uint32_t > ( base::min ( items_done * 1000 / items_total, 999ull ) ) );

			}

			ID3D11UnorderedAccessView* none = nullptr;

			context->CSSetUnorderedAccessViews ( 0, 1, &none, nullptr );
			drop ( writing );

		}

		ID3D11ShaderResourceView* empty = nullptr;

		context->CSSetShaderResources ( 0, 1, &empty );
		drop ( cache_view );
		drop ( cache_buffer );

		if ( !ok ) {

			drop_dag ( );

			return false;

		}

		dag_epoch = cache.epoch ( );

		if ( progress )
			base::atomic_store ( *progress, 1000u );

		return true;

	}

	bool gpu_device::dag_item ( uint32_t index, uint32_t* out ) {

		if ( !dag_part_count || index / 2 >= dag_rows )
			return false;

		uint32_t part = index / 2 / dag_part_rows;
		uint32_t offset = ( index - part * dag_part_rows * 2 ) * 64;
		D3D11_BUFFER_DESC layout = { };
		ID3D11Buffer* staging = nullptr;

		if ( part >= dag_part_count )
			return false;

		layout.ByteWidth = 64;
		layout.Usage = D3D11_USAGE_STAGING;
		layout.CPUAccessFlags = D3D11_CPU_ACCESS_READ;

		if ( FAILED ( device->CreateBuffer ( &layout, nullptr, &staging ) ) )
			return false;

		D3D11_BOX box = { offset, 0, 0, offset + 64, 1, 1 };
		D3D11_MAPPED_SUBRESOURCE mapped;

		context->CopySubresourceRegion ( staging, 0, 0, 0, 0, dag_parts [ part ], 0, &box );

		bool ok = SUCCEEDED ( context->Map ( staging, 0, D3D11_MAP_READ, 0, &mapped ) );

		if ( ok ) {

			base::copy ( out, mapped.pData, 64 );
			context->Unmap ( staging, 0 );

		}

		drop ( staging );

		return ok;

	}

	bool gpu_device::dispatch_ethash ( const uint8_t* header, uint32_t nonce_high, uint32_t first, uint32_t count, uint64_t threshold, bool sampling, int32_t slot ) {

		if ( !dag_part_count || !prepare_ethash ( ) )
			return false;

		uint32_t groups = base::max ( ( count + 127 ) / 128, 1u );
		uint32_t across = base::min ( groups, 4096u );
		uint32_t down = ( groups + across - 1 ) / across;
		search_layout values = { };
		D3D11_MAPPED_SUBRESOURCE mapped;

		for ( int32_t i = 0; i < 8; i++ )
			values.header [ i ] = base::load_little ( header + i * 4 );

		values.limits [ 0 ] = static_cast < uint32_t > ( threshold >> 32 );
		values.limits [ 1 ] = static_cast < uint32_t > ( threshold );
		values.limits [ 2 ] = across * 128;
		values.limits [ 3 ] = first;
		values.more [ 0 ] = nonce_high;
		values.more [ 1 ] = dag_rows;
		values.more [ 2 ] = dag_part_rows;
		values.more [ 3 ] = sampling ? 1 : 0;

		if ( FAILED ( context->Map ( search_constants, 0, D3D11_MAP_WRITE_DISCARD, 0, &mapped ) ) )
			return false;

		base::copy ( mapped.pData, &values, sizeof ( values ) );
		context->Unmap ( search_constants, 0 );

		static const UINT zeros [ 4 ] = { };

		context->ClearUnorderedAccessViewUint ( views [ slot ], zeros );
		context->CSSetConstantBuffers ( 0, 1, &search_constants );
		context->CSSetShaderResources ( 0, dag_part_count, dag_views );
		context->CSSetUnorderedAccessViews ( 0, 1, &views [ slot ], nullptr );
		context->CSSetShader ( search_shader, nullptr, 0 );
		context->Dispatch ( across, down, 1 );

		uint32_t words = sampling ? base::min ( count, sample_room ) : 32;
		D3D11_BOX box = { 0, 0, 0, words * 4, 1, 1 };

		context->CopySubresourceRegion ( readback [ slot ], 0, 0, 0, 0, results [ slot ], 0, &box );
		context->Flush ( );

		return true;

	}

	bool gpu_device::submit_ethash ( const uint8_t* header, uint32_t nonce_high, uint32_t first, uint32_t count, uint64_t threshold, int32_t& slot ) {

		slot = next_slot;
		next_slot ^= 1;

		return dispatch_ethash ( header, nonce_high, first, count, threshold, false, slot );

	}

	bool gpu_device::sample_ethash ( const uint8_t* header, uint32_t nonce_high, uint32_t first, uint32_t count, uint32_t* tops ) {

		count = base::min ( count, sample_room );

		int32_t slot = next_slot;

		next_slot ^= 1;

		return count && dispatch_ethash ( header, nonce_high, first, count, ~0ull, true, slot ) && read ( slot, tops, count );

	}

	uint32_t gpu_device::capacity ( uint8_t algorithm ) {

		if ( algorithm == chain::ALGORITHM_SCRYPT )
			return prepare_scrypt ( ) ? scrypt_room : 0;

		if ( algorithm == chain::ALGORITHM_KHEAVYHASH )
			return prepare_heavy ( ) ? 1u << 26 : 0;

		if ( algorithm == chain::ALGORITHM_ETHASH )
			return prepare_ethash ( ) ? 1u << 24 : 0;

		return algorithm == chain::ALGORITHM_SHA256D && device ? 1u << 28 : 0;

	}

	bool gpu_device::temperature ( float& celsius ) {

		if ( !has_luid )
			return false;

		if ( !kernel_adapter ) {

			D3DKMT_OPENADAPTERFROMLUID opening = { };

			opening.AdapterLuid.LowPart = luid_low;
			opening.AdapterLuid.HighPart = luid_high;

			if ( D3DKMTOpenAdapterFromLuid ( &opening ) != 0 ) {

				has_luid = false;

				return false;

			}

			kernel_adapter = opening.hAdapter;

		}

		D3DKMT_ADAPTER_PERFDATA data = { };
		D3DKMT_QUERYADAPTERINFO query = { };

		query.hAdapter = kernel_adapter;
		query.Type = KMTQAITYPE_ADAPTERPERFDATA;
		query.pPrivateDriverData = &data;
		query.PrivateDriverDataSize = sizeof ( data );

		if ( D3DKMTQueryAdapterInfo ( &query ) != 0 || !data.Temperature )
			return false;

		celsius = static_cast < float > ( data.Temperature ) / 10.0f;

		return true;

	}

	bool gpu_device::dispatch ( uint8_t algorithm, const chain::header& header, uint32_t first, uint32_t count, uint32_t threshold, bool sampling, int32_t slot ) {

		bool scrypt = algorithm == chain::ALGORITHM_SCRYPT;

		if ( !device || algorithm == chain::ALGORITHM_RANDOMX || algorithm == chain::ALGORITHM_KHEAVYHASH || ( scrypt && !prepare_scrypt ( ) ) )
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

		if ( scrypt ) {

			for ( int32_t i = 0; i < 16; i++ )
				values.head [ i ] = base::load_big ( header.bytes + i * 4 );

		} else {

			const uint32_t* words = values.tail;
			uint32_t w16 = hash::sha256_small0 ( words [ 1 ] ) + words [ 0 ];
			uint32_t w17 = hash::sha256_small1 ( 640 ) + hash::sha256_small0 ( words [ 2 ] ) + words [ 1 ];

			hash::sha256_head_rounds ( values.midstate, words, 3, values.head );
			values.head [ 8 ] = w16;
			values.head [ 9 ] = w17;
			values.head [ 10 ] = hash::sha256_small1 ( w16 ) + words [ 2 ];
			values.head [ 11 ] = hash::sha256_small1 ( w17 ) + hash::sha256_small0 ( 0x80000000 );

		}

		D3D11_MAPPED_SUBRESOURCE mapped;

		if ( FAILED ( context->Map ( constants, 0, D3D11_MAP_WRITE_DISCARD, 0, &mapped ) ) )
			return false;

		base::copy ( mapped.pData, &values, sizeof ( values ) );
		context->Unmap ( constants, 0 );

		static const UINT zeros [ 4 ] = { };
		ID3D11UnorderedAccessView* bound [ 3 ] = { views [ slot ], table_view, state_view };

		context->ClearUnorderedAccessViewUint ( views [ slot ], zeros );
		context->CSSetConstantBuffers ( 0, 1, &constants );
		context->CSSetUnorderedAccessViews ( 0, scrypt ? 3 : 1, bound, nullptr );

		if ( scrypt ) {

			for ( ID3D11ComputeShader* stage : scrypt_stages ) {

				context->CSSetShader ( stage, nullptr, 0 );
				context->Dispatch ( across, down, 1 );

			}

		} else {

			context->CSSetShader ( shader, nullptr, 0 );
			context->Dispatch ( across, down, 1 );

		}

		uint32_t words = sampling ? base::min ( count, sample_room ) : 32;
		D3D11_BOX box = { 0, 0, 0, words * 4, 1, 1 };

		context->CopySubresourceRegion ( readback [ slot ], 0, 0, 0, 0, results [ slot ], 0, &box );
		context->Flush ( );

		return true;

	}

	bool gpu_device::read ( int32_t slot, uint32_t* out, uint32_t words ) {

		D3D11_MAPPED_SUBRESOURCE mapped;

		if ( FAILED ( context->Map ( readback [ slot ], 0, D3D11_MAP_READ, 0, &mapped ) ) )
			return false;

		base::copy ( out, mapped.pData, words * 4 );
		context->Unmap ( readback [ slot ], 0 );

		return true;

	}

	bool gpu_device::submit ( uint8_t algorithm, const chain::header& header, uint32_t first, uint32_t count, uint32_t threshold, int32_t& slot ) {

		slot = next_slot;
		next_slot ^= 1;

		return dispatch ( algorithm, header, first, count, threshold, false, slot );

	}

	bool gpu_device::collect ( int32_t slot, uint32_t* found, uint32_t room, uint32_t& found_count ) {

		uint32_t words [ 32 ];

		found_count = 0;

		if ( !read ( slot, words, 32 ) )
			return false;

		found_count = base::min ( base::min ( words [ 0 ], 31u ), room );

		for ( uint32_t i = 0; i < found_count; i++ )
			found [ i ] = words [ 1 + i ];

		return true;

	}

	bool gpu_device::search ( uint8_t algorithm, const chain::header& header, uint32_t first, uint32_t count, uint32_t threshold, uint32_t* found, uint32_t room, uint32_t& found_count ) {

		int32_t slot = 0;

		found_count = 0;

		return submit ( algorithm, header, first, count, threshold, slot ) && collect ( slot, found, room, found_count );

	}

	bool gpu_device::sample ( uint8_t algorithm, const chain::header& header, uint32_t first, uint32_t count, uint32_t* tops ) {

		count = base::min ( count, sample_room );

		if ( algorithm == chain::ALGORITHM_SCRYPT )
			count = base::min ( count, capacity ( algorithm ) );

		int32_t slot = next_slot;

		next_slot ^= 1;

		return count && dispatch ( algorithm, header, first, count, 0, true, slot ) && read ( slot, tops, count );

	}

}