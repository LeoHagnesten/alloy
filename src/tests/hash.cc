#ifdef ALLOY_DEBUG

#include "tests/harness.hh"
#include "hash/sha256.hh"
#include "base/memory.hh"
#include "base/system.hh"

namespace tests {

	void hash_tests ( ) {

		section ( L"sha-256" );

		uint8_t digest [ 32 ];

		hash::sha256_once ( "", 0, digest );
		check ( hex_is ( digest, 32, "e3b0c44298fc1c149afbf4c8996fb92427ae41e4649b934ca495991b7852b855" ), L"the empty message hashes to the FIPS 180 value" );

		hash::sha256_once ( "abc", 3, digest );
		check ( hex_is ( digest, 32, "ba7816bf8f01cfea414140de5dae2223b00361a396177a9cb410ff61f20015ad" ), L"\"abc\" hashes to the FIPS 180 value" );

		static const char two_blocks [ ] = "abcdbcdecdefdefgefghfghighijhijkijkljklmklmnlmnomnopnopq";

		hash::sha256_once ( two_blocks, sizeof ( two_blocks ) - 1, digest );
		check ( hex_is ( digest, 32, "248d6a61d20638b8e5c026930c3e6039a33ce45964ff2167f6ecedd419db06c1" ), L"the 448-bit message spills its length into a second block correctly" );

		hash::sha256 million;
		uint8_t chunk [ 1000 ];

		base::fill ( chunk, 'a', sizeof ( chunk ) );

		for ( int32_t i = 0; i < 1000; i++ )
			million.add ( chunk, sizeof ( chunk ) );

		million.end ( digest );
		check ( hex_is ( digest, 32, "cdc76e5c9914fb9281a1c7e284d73e67f1809a48a497200e046d39ccc7112cd0" ), L"a million \"a\"s fed in pieces hash to the FIPS 180 value" );

		hash::sha256 trickle;
		uint8_t whole [ 32 ];

		for ( size_t i = 0; i < sizeof ( two_blocks ) - 1; i++ )
			trickle.add ( two_blocks + i, 1 );

		trickle.end ( digest );
		hash::sha256_once ( two_blocks, sizeof ( two_blocks ) - 1, whole );
		check ( base::same ( digest, whole, 32 ), L"feeding one byte at a time matches feeding it all at once" );

		hash::sha256_twice ( "hello", 5, digest );
		check ( hex_is ( digest, 32, "9595c9df90075148eb06860365df33584b75bff782a510c6cd4883a419833d50" ), L"sha256d of \"hello\" matches the known value" );

		base::processor cpu;

		base::detect ( cpu );

		if ( !cpu.sha || !cpu.sse41 || !cpu.ssse3 ) {

			check ( true, L"this processor has no SHA extensions, so only the portable kernel is tested" );

			return;

		}

		uint8_t block [ 64 ] = { 'a', 'b', 'c', 0x80 };
		uint32_t state [ 8 ];

		block [ 63 ] = 24;

		for ( int32_t i = 0; i < 8; i++ )
			state [ i ] = hash::sha256_start [ i ];

		hash::sha256_block_extensions ( state, block );
		hash::sha256_digest ( state, digest );
		check ( hex_is ( digest, 32, "ba7816bf8f01cfea414140de5dae2223b00361a396177a9cb410ff61f20015ad" ), L"the SHA extensions kernel hashes \"abc\" to the FIPS 180 value" );

		uint64_t seed = 0x9E3779B97F4A7C15ull;
		bool agrees = true;

		for ( int32_t round = 0; round < 2000 && agrees; round++ ) {

			uint32_t portable [ 8 ];
			uint32_t fast [ 8 ];

			for ( int32_t i = 0; i < 64; i++ ) {

				seed ^= seed << 13;
				seed ^= seed >> 7;
				seed ^= seed << 17;
				block [ i ] = static_cast < uint8_t > ( seed );

			}

			for ( int32_t i = 0; i < 8; i++ ) {

				portable [ i ] = static_cast < uint32_t > ( seed >> ( i * 4 ) ) * 2654435761u;
				fast [ i ] = portable [ i ];

			}

			hash::sha256_block ( portable, block );
			hash::sha256_block_extensions ( fast, block );
			agrees = base::same ( portable, fast, sizeof ( fast ) );

		}

		check ( agrees, L"the SHA extensions kernel matches the portable one on 2000 random blocks and states" );

	}

}

#endif
