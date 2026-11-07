#pragma once

#include "chain/header.hh"

struct ID3D11Device;
struct ID3D11DeviceContext;
struct ID3D11ComputeShader;
struct ID3D11Buffer;
struct ID3D11UnorderedAccessView;

namespace mine {

	static constexpr int32_t adapter_room = 8;
	static constexpr uint32_t sample_room = 16384;

	struct adapter_info {

		wchar_t name [ 128 ] = { };
		uint64_t memory = 0;

	};

	int32_t list_adapters ( adapter_info* out, int32_t room );

	class gpu_device {

	public:

		~gpu_device ( );

		bool open ( uint32_t adapter );

		void close ( );

		bool search ( const chain::header& header, uint32_t first, uint32_t count, uint32_t threshold, uint32_t* found, uint32_t room, uint32_t& found_count );

		bool sample ( const chain::header& header, uint32_t first, uint32_t count, uint32_t* tops );

		const wchar_t* name ( ) const {

			return label;

		}

	private:

		bool dispatch ( const chain::header& header, uint32_t first, uint32_t count, uint32_t threshold, bool sampling );

		bool read ( uint32_t* out, uint32_t words );

		ID3D11Device* device = nullptr;
		ID3D11DeviceContext* context = nullptr;
		ID3D11ComputeShader* shader = nullptr;
		ID3D11Buffer* constants = nullptr;
		ID3D11Buffer* results = nullptr;
		ID3D11Buffer* readback = nullptr;
		ID3D11UnorderedAccessView* view = nullptr;
		wchar_t label [ 128 ] = { };

	};

}
