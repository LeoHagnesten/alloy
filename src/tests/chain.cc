#ifdef ALLOY_DEBUG

#include "tests/harness.hh"
#include "chain/header.hh"
#include "hash/sha256.hh"

namespace tests {

	void chain_tests ( ) {

		section ( L"bitcoin headers" );

		chain::header source;
		uint8_t full [ 32 ];
		uint8_t fast [ 32 ];
		uint8_t expected [ 32 ];

		chain::genesis ( source );
		hash::sha256_twice ( source.bytes, chain::header_size, full );
		base::read_hex ( "6fe28c0ab6f1b372c1a6a246ae63f74f931e8365e15a089c68d6190000000000", expected, 32 );
		check ( base::same ( full, expected, 32 ), L"the genesis header hashes to block 000000000019d668..." );
		check ( source.version ( ) == 1 && source.time ( ) == 1231006505 && source.bits ( ) == 0x1D00FFFF && source.nonce ( ) == 2083236893, L"the genesis header's fields read back as version 1, January 3 2009, 1d00ffff and nonce 2083236893" );

		chain::header_hasher hasher;

		hasher.prepare ( source );
		hasher.hash ( source.nonce ( ), fast );
		check ( base::same ( fast, full, 32 ), L"the midstate path gives the same hash as hashing all 80 bytes" );

		bool agrees = true;

		for ( uint32_t nonce = 0; nonce < 64 && agrees; nonce++ ) {

			chain::header copy = source;

			copy.set_nonce ( nonce * 2654435761u );
			hash::sha256_twice ( copy.bytes, chain::header_size, full );
			hasher.hash ( nonce * 2654435761u, fast );
			agrees = base::same ( fast, full, 32 );

		}

		check ( agrees, L"the midstate path agrees with the plain one for 64 scattered nonces" );

		uint8_t target [ 32 ];
		uint8_t wanted [ 32 ];

		chain::target_of ( 0x1D00FFFF, target );
		base::read_hex ( "0000000000000000000000000000000000000000000000000000ffff00000000", wanted, 32 );
		check ( base::same ( target, wanted, 32 ), L"bits 1d00ffff expand to the difficulty 1 target" );

		hasher.hash ( source.nonce ( ), fast );
		check ( chain::meets ( fast, source.bits ( ) ), L"the genesis hash meets its own target" );

		check ( chain::meets ( fast, 0x1E00FFFF ) && !chain::meets ( fast, 0x1B00FFFF ), L"the genesis hash passes an easier target and fails one that needs 48 zero bits" );

		hasher.hash ( source.nonce ( ) + 1, fast );
		check ( !chain::meets ( fast, source.bits ( ) ), L"the next nonce misses difficulty 1" );

		hasher.hash ( source.nonce ( ), fast );
		check ( chain::leading_zero_bits ( fast ) == 43, L"the genesis hash starts with 43 zero bits" );

	}

}

#endif