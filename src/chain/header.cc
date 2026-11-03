#include "chain/header.hh"
#include "hash/sha256.hh"

namespace chain {

	uint32_t header::field ( uint32_t offset ) const {

		return static_cast < uint32_t > ( bytes [ offset ] ) | static_cast < uint32_t > ( bytes [ offset + 1 ] ) << 8 | static_cast < uint32_t > ( bytes [ offset + 2 ] ) << 16 | static_cast < uint32_t > ( bytes [ offset + 3 ] ) << 24;

	}

	void header::set_field ( uint32_t offset, uint32_t value ) {

		for ( uint32_t i = 0; i < 4; i++ )
			bytes [ offset + i ] = static_cast < uint8_t > ( value >> ( i * 8 ) );

	}

	void header_hasher::prepare ( const header& source ) {

		for ( int32_t i = 0; i < 8; i++ )
			midstate [ i ] = hash::sha256_start [ i ];

		hash::sha256_block ( midstate, source.bytes );
		base::fill ( second, 0, sizeof ( second ) );
		base::copy ( second, source.bytes + 64, 16 );
		second [ 16 ] = 0x80;
		second [ 62 ] = 0x02;
		second [ 63 ] = 0x80;

	}

	void header_hasher::hash ( uint32_t nonce, uint8_t* out ) const {

		uint8_t block [ 64 ];
		uint32_t state [ 8 ];

		base::copy ( block, second, 64 );
		block [ 12 ] = static_cast < uint8_t > ( nonce );
		block [ 13 ] = static_cast < uint8_t > ( nonce >> 8 );
		block [ 14 ] = static_cast < uint8_t > ( nonce >> 16 );
		block [ 15 ] = static_cast < uint8_t > ( nonce >> 24 );

		for ( int32_t i = 0; i < 8; i++ )
			state [ i ] = midstate [ i ];

		hash::sha256_block ( state, block );

		uint8_t again [ 64 ] = { };

		hash::sha256_digest ( state, again );
		again [ 32 ] = 0x80;
		again [ 62 ] = 0x01;

		for ( int32_t i = 0; i < 8; i++ )
			state [ i ] = hash::sha256_start [ i ];

		hash::sha256_block ( state, again );
		hash::sha256_digest ( state, out );

	}

	void genesis ( header& out ) {

		static const char raw [ ] = "0100000000000000000000000000000000000000000000000000000000000000000000003ba3edfd7a7b12b27ac72c3e67768f617fc81bc3888a51323a9fb8aa4b1e5e4a29ab5f49ffff001d1dac2b7c";

		base::read_hex ( raw, out.bytes, header_size );

	}

	void target_of ( uint32_t bits, uint8_t* out ) {

		uint32_t exponent = bits >> 24;
		uint32_t mantissa = bits & 0x007FFFFF;

		base::fill ( out, 0, 32 );

		for ( uint32_t i = 0; i < 3; i++ ) {

			int32_t place = static_cast < int32_t > ( exponent ) - 3 + static_cast < int32_t > ( i );

			if ( place >= 0 && place < 32 )
				out [ place ] = static_cast < uint8_t > ( mantissa >> ( i * 8 ) );

		}

	}

	bool meets ( const uint8_t* digest, uint32_t bits ) {

		uint8_t target [ 32 ];

		target_of ( bits, target );

		for ( int32_t i = 31; i >= 0; i-- ) {

			if ( digest [ i ] != target [ i ] )
				return digest [ i ] < target [ i ];

		}

		return true;

	}

	uint32_t leading_zero_bits ( const uint8_t* digest ) {

		uint32_t count = 0;

		for ( int32_t i = 31; i >= 0; i-- ) {

			if ( !digest [ i ] ) {

				count += 8;

				continue;

			}

			for ( uint8_t mask = 0x80; mask && !( digest [ i ] & mask ); mask >>= 1 )
				count++;

			break;

		}

		return count;

	}

}