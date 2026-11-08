#pragma once

#include "chain/header.hh"
#include "chain/algorithm.hh"

struct ID3D11Device;
struct ID3D11DeviceContext;
struct ID3D11ComputeShader;
struct ID3D11Buffer;
struct ID3D11UnorderedAccessView;

namespace hash {

	class heavy_hasher;

}

namespace mine {

	static constexpr int32_t adapter_room = 8;
	static constexpr uint32_t sample_room = 16384;
	static constexpr uint32_t software_adapter = 0xFFFFFFFFu;

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

		bool submit ( uint8_t algorithm, const chain::header& header, uint32_t first, uint32_t count, uint32_t threshold, int32_t& slot );

		bool submit_heavy ( const hash::heavy_hasher& prepared, uint32_t nonce_high, uint32_t first, uint32_t count, uint32_t threshold, int32_t& slot );

		bool sample_heavy ( const hash::heavy_hasher& prepared, uint32_t nonce_high, uint32_t first, uint32_t count, uint32_t* tops );

		bool collect ( int32_t slot, uint32_t* found, uint32_t room, uint32_t& found_count );

		bool search ( uint8_t algorithm, const chain::header& header, uint32_t first, uint32_t count, uint32_t threshold, uint32_t* found, uint32_t room, uint32_t& found_count );

		bool sample ( uint8_t algorithm, const chain::header& header, uint32_t first, uint32_t count, uint32_t* tops );

		uint32_t capacity ( uint8_t algorithm );

		bool temperature ( float& celsius );

		const wchar_t* name ( ) const {

			return label;

		}

	private:

		bool prepare_scrypt ( );

		bool prepare_heavy ( );

		bool dispatch_heavy ( const hash::heavy_hasher& prepared, uint32_t nonce_high, uint32_t first, uint32_t count, uint32_t threshold, int32_t slot, uint32_t words );

		bool dispatch ( uint8_t algorithm, const chain::header& header, uint32_t first, uint32_t count, uint32_t threshold, bool sampling, int32_t slot );

		bool read ( int32_t slot, uint32_t* out, uint32_t words );

		ID3D11Device* device = nullptr;
		ID3D11DeviceContext* context = nullptr;
		ID3D11ComputeShader* shader = nullptr;
		ID3D11ComputeShader* scrypt_stages [ 3 ] = { };
		ID3D11ComputeShader* heavy_shader = nullptr;
		ID3D11Buffer* heavy_constants = nullptr;
		bool heavy_failed = false;
		ID3D11Buffer* constants = nullptr;
		ID3D11Buffer* results [ 2 ] = { };
		ID3D11Buffer* readback [ 2 ] = { };
		ID3D11Buffer* table = nullptr;
		ID3D11Buffer* states = nullptr;
		ID3D11UnorderedAccessView* views [ 2 ] = { };
		ID3D11UnorderedAccessView* table_view = nullptr;
		ID3D11UnorderedAccessView* state_view = nullptr;
		uint64_t video_memory = 0;
		uint32_t scrypt_room = 0;
		bool scrypt_failed = false;
		int32_t next_slot = 0;
		uint32_t luid_low = 0;
		int32_t luid_high = 0;
		uint32_t kernel_adapter = 0;
		bool has_luid = false;
		wchar_t label [ 128 ] = { };

	};

}
