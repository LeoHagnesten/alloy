#include "chain/header.hh"
#include "base/bits.hh"
#include "base/memory.hh"
#include "base/strings.hh"

namespace chain {

	uint32_t header::field ( uint32_t offset ) const {

		return base::load_little ( bytes + offset );

	}

	void header::set_field ( uint32_t offset, uint32_t value ) {

		base::store_little ( bytes + offset, value );

	}

	void header_hasher::prepare ( const header& source, hash::compressor kernel ) {

		compress = kernel;

		for ( int32_t i = 0; i < 8; i++ )
			midstate [ i ] = hash::sha256_start [ i ];

		compress ( midstate, source.bytes );
		base::zero ( second, sizeof ( second ) );
		base::copy ( second, source.bytes + 64, 16 );
		second [ 16 ] = 0x80;
		second [ 62 ] = 0x02;
		second [ 63 ] = 0x80;

	}

	void header_hasher::hash ( uint32_t nonce, uint8_t* out ) const {

		uint8_t block [ 64 ];
		uint32_t state [ 8 ];

		base::copy ( block, second, 64 );
		base::store_little ( block + 12, nonce );

		for ( int32_t i = 0; i < 8; i++ )
			state [ i ] = midstate [ i ];

		compress ( state, block );

		uint8_t again [ 64 ] = { };

		hash::sha256_digest ( state, again );
		again [ 32 ] = 0x80;
		again [ 62 ] = 0x01;

		for ( int32_t i = 0; i < 8; i++ )
			state [ i ] = hash::sha256_start [ i ];

		compress ( state, again );
		hash::sha256_digest ( state, out );

	}

	void genesis ( header& out ) {

		static const char raw [ ] = "0100000000000000000000000000000000000000000000000000000000000000000000003ba3edfd7a7b12b27ac72c3e67768f617fc81bc3888a51323a9fb8aa4b1e5e4a29ab5f49ffff001d1dac2b7c";

		base::read_hex ( raw, out.bytes, header_size );

	}

	void litecoin_genesis ( header& out ) {

		static const char raw [ ] = "010000000000000000000000000000000000000000000000000000000000000000000000d9ced4ed1130f7b7faad9be25323ffafa33232a17c3edf6cfd97bee6bafbdd97b9aa8e4ef0ff0f1ecd513f7c";

		base::read_hex ( raw, out.bytes, header_size );

	}

}
