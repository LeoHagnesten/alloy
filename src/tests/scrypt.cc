#ifdef ALLOY_DEBUG

#include "tests/harness.hh"
#include "hash/scrypt.hh"
#include "chain/header.hh"
#include "chain/target.hh"
#include "base/bits.hh"
#include "base/system/clock.hh"
#include "base/memory.hh"
#include "base/strings.hh"
#include "base/system/hardware.hh"
#include "base/text.hh"

namespace tests {

	void scrypt_tests ( ) {

		section ( L"scrypt" );

		uint8_t block [ 64 ];
		uint32_t words [ 16 ];

		base::read_hex ( "7e879a214f3ec9867ca940e641718f26baee555b8c61c1b50df846116dcd3b1dee24f319df9b3d8514121e4b5ac5aa3276021d2909c74829edebc68db8b8c25e", block, 64 );

		for ( int32_t i = 0; i < 16; i++ )
			words [ i ] = base::load_little ( block + i * 4 );

		hash::salsa20_8 ( words );

		for ( int32_t i = 0; i < 16; i++ )
			base::store_little ( block + i * 4, words [ i ] );

		check ( hex_is ( block, 64, "a41f859c6608cc993b81cacb020cef05044b2181a2fd337dfd7b1c6396682f29b4393168e3c9e6bcfe6bc5b7a06d96bae424cc102c91745c24ad673dc7618f81" ), L"the Salsa20/8 core matches the RFC 7914 vector" );

		uint8_t derived [ 64 ];

		hash::pbkdf2_sha256 ( "passwd", 6, "salt", 4, 1, derived, 64 );
		check ( hex_is ( derived, 64, "55ac046e56e3089fec1691c22544b605f94185216dde0465e68b9d57c20dacbc49ca9cccf179b645991664b39d77ef317c71b845b1e30bd509112041d3a19783" ), L"PBKDF2-HMAC-SHA256 matches the RFC 7914 vector" );

		bool small = hash::scrypt ( "", 0, "", 0, 16, 1, 1, derived, 64 );

		check ( small && hex_is ( derived, 64, "77d6576238657b203b19ca42c18a0497f16b4844e3074ae8dfdffa3fede21442fcd0069ded0948f8326a753a0fc81f17e8d3e0fb2e0d3628cf35e20c38d18906" ), L"scrypt with N 16, r 1, p 1 matches the RFC 7914 vector" );

		bool wide = hash::scrypt ( "password", 8, "NaCl", 4, 1024, 8, 16, derived, 64 );

		check ( wide && hex_is ( derived, 64, "fdbabe1c9d3472007856e7190d01e9fe7c6ad7cbc8237830e77376634b3731622eaf30d92e22a3886ff109279d9830dac727afb94a83ee6d8360cbdfa2cc0640" ), L"scrypt with N 1024, r 8, p 16 matches the RFC 7914 vector" );

		chain::header genesis;
		uint8_t digest [ 32 ];
		uint8_t general [ 32 ];
		void* scratch = base::allocate ( hash::scrypt_scratch_bytes );

		chain::litecoin_genesis ( genesis );
		check ( genesis.time ( ) == 1317972665 && genesis.bits ( ) == 0x1E0FFFF0 && genesis.nonce ( ) == 2084524493, L"the Litecoin genesis header reads back as October 7 2011, 1e0ffff0 and nonce 2084524493" );
		hash::sha256_twice ( genesis.bytes, chain::header_size, digest );
		base::reverse_bytes ( digest, 32 );
		check ( hex_is ( digest, 32, "12a765e31ffd4059bada1e25190f6e98c99d9714d334efa41a195a7e7e04bfe2" ), L"the Litecoin genesis header hashes to block 12a765e3..." );

		hash::scrypt_header ( genesis.bytes, digest, scratch );
		hash::scrypt ( genesis.bytes, chain::header_size, genesis.bytes, chain::header_size, 1024, 1, 1, general, 32 );
		base::reverse_bytes ( digest, 32 );
		check ( hex_is ( digest, 32, "0000050c34a64b415b6b15b37f2216634b5b1669cb9a2e38d76f7213b0671e00" ), L"its scrypt proof of work is 0000050c34a64b41..." );

		base::reverse_bytes ( digest, 32 );
		check ( base::same ( digest, general, 32 ) && chain::meets_bits ( digest, genesis.bits ( ) ), L"the mining path agrees with general scrypt and meets bits 1e0ffff0" );

		genesis.set_nonce ( genesis.nonce ( ) - 1 );
		hash::scrypt_header ( genesis.bytes, digest, scratch );
		check ( !chain::meets_bits ( digest, 0x1E0FFFF0 ), L"the nonce before it misses the target" );

		void* pair_scratch = base::allocate ( hash::scrypt_pair_scratch_bytes );
		chain::header other = genesis;
		uint8_t paired [ 2 ] [ 32 ];
		uint8_t single [ 2 ] [ 32 ];
		bool pairs = true;

		for ( uint32_t i = 0; i < 6 && pairs; i++ ) {

			genesis.set_nonce ( 2084524493u - 3 + i * 2 );
			other.set_nonce ( genesis.nonce ( ) + 1 );
			hash::scrypt_header_pair ( genesis.bytes, other.bytes, paired [ 0 ], paired [ 1 ], pair_scratch );
			hash::scrypt_header ( genesis.bytes, single [ 0 ], scratch );
			hash::scrypt_header ( other.bytes, single [ 1 ], scratch );
			pairs = base::same ( paired, single, sizeof ( paired ) );

		}

		check ( pairs, L"hashing two nonces side by side gives the same results as one at a time" );

		double began = base::seconds ( );
		uint32_t count = 0;

		while ( base::seconds ( ) - began < 0.25 ) {

			genesis.set_nonce ( count++ );
			hash::scrypt_header ( genesis.bytes, digest, scratch );

		}

		double alone = count / ( base::seconds ( ) - began );

		began = base::seconds ( );
		count = 0;

		while ( base::seconds ( ) - began < 0.25 ) {

			genesis.set_nonce ( count );
			other.set_nonce ( count + 1 );
			hash::scrypt_header_pair ( genesis.bytes, other.bytes, paired [ 0 ], paired [ 1 ], pair_scratch );
			count += 2;

		}

		double together = count / ( base::seconds ( ) - began );
		base::text note;

		note.add ( L"one thread hashes scrypt at " ).decimal ( together / 1000.0, 2 ).add ( L" kH/s in pairs against " ).decimal ( alone / 1000.0, 2 ).add ( L" kH/s alone" );
		check ( together > alone, note.get ( ) );
		base::release ( pair_scratch );

		base::processor cpu;

		base::detect ( cpu );

		if ( cpu.avx2 ) {

			void* quad_scratch = base::allocate ( hash::scrypt_quad_scratch_bytes );
			chain::header group [ 4 ];
			uint8_t wide [ 4 ] [ 32 ];
			uint8_t lone [ 32 ];
			bool quads = true;

			for ( uint32_t round = 0; round < 3 && quads; round++ ) {

				const uint8_t* inputs [ 4 ];
				uint8_t* outputs [ 4 ];

				for ( int32_t i = 0; i < 4; i++ ) {

					group [ i ] = genesis;
					group [ i ].set_nonce ( 2084524493u - 2 + round * 4 + static_cast < uint32_t > ( i ) );
					inputs [ i ] = group [ i ].bytes;
					outputs [ i ] = wide [ i ];

				}

				hash::scrypt_header_quad ( inputs, outputs, quad_scratch );

				for ( int32_t i = 0; i < 4 && quads; i++ ) {

					hash::scrypt_header ( group [ i ].bytes, lone, scratch );
					quads = base::same ( lone, wide [ i ], 32 );

				}

			}

			check ( quads, L"four nonces at once with AVX2 give the same results as one at a time" );

			began = base::seconds ( );
			count = 0;

			while ( base::seconds ( ) - began < 0.25 ) {

				const uint8_t* inputs [ 4 ];
				uint8_t* outputs [ 4 ];

				for ( int32_t i = 0; i < 4; i++ ) {

					group [ i ].set_nonce ( count + static_cast < uint32_t > ( i ) );
					inputs [ i ] = group [ i ].bytes;
					outputs [ i ] = wide [ i ];

				}

				hash::scrypt_header_quad ( inputs, outputs, quad_scratch );
				count += 4;

			}

			note.clear ( );
			note.add ( L"with AVX2 one thread hashes scrypt at " ).decimal ( count / ( base::seconds ( ) - began ) / 1000.0, 2 ).add ( L" kH/s" );
			check ( count > 0, note.get ( ) );
			base::release ( quad_scratch );

		}
		base::release ( scratch );

	}

}

#endif
