#ifdef ALLOY_DEBUG

#include "tests/harness.hh"
#include "hash/sha256.hh"

namespace tests {

	static bool digest_is ( const uint8_t* digest, const char* expected ) {

		uint8_t wanted [ 32 ];

		return base::read_hex ( expected, wanted, 32 ) && base::same ( digest, wanted, 32 );

	}

	void hash_tests ( ) {

		section ( L"sha-256" );

		uint8_t digest [ 32 ];

		hash::sha256_once ( "", 0, digest );
		check ( digest_is ( digest, "e3b0c44298fc1c149afbf4c8996fb92427ae41e4649b934ca495991b7852b855" ), L"the empty message hashes to the FIPS 180 value" );

		hash::sha256_once ( "abc", 3, digest );
		check ( digest_is ( digest, "ba7816bf8f01cfea414140de5dae2223b00361a396177a9cb410ff61f20015ad" ), L"\"abc\" hashes to the FIPS 180 value" );

		static const char two_blocks [ ] = "abcdbcdecdefdefgefghfghighijhijkijkljklmklmnlmnomnopnopq";

		hash::sha256_once ( two_blocks, sizeof ( two_blocks ) - 1, digest );
		check ( digest_is ( digest, "248d6a61d20638b8e5c026930c3e6039a33ce45964ff2167f6ecedd419db06c1" ), L"the 448-bit message spills its length into a second block correctly" );

		hash::sha256 million;
		uint8_t chunk [ 1000 ];

		base::fill ( chunk, 'a', sizeof ( chunk ) );

		for ( int32_t i = 0; i < 1000; i++ )
			million.add ( chunk, sizeof ( chunk ) );

		million.end ( digest );
		check ( digest_is ( digest, "cdc76e5c9914fb9281a1c7e284d73e67f1809a48a497200e046d39ccc7112cd0" ), L"a million \"a\"s fed in pieces hash to the FIPS 180 value" );

		hash::sha256 trickle;
		uint8_t whole [ 32 ];

		for ( size_t i = 0; i < sizeof ( two_blocks ) - 1; i++ )
			trickle.add ( two_blocks + i, 1 );

		trickle.end ( digest );
		hash::sha256_once ( two_blocks, sizeof ( two_blocks ) - 1, whole );
		check ( base::same ( digest, whole, 32 ), L"feeding one byte at a time matches feeding it all at once" );

		hash::sha256_twice ( "hello", 5, digest );
		check ( digest_is ( digest, "9595c9df90075148eb06860365df33584b75bff782a510c6cd4883a419833d50" ), L"sha256d of \"hello\" matches the known value" );

	}

}

#endif