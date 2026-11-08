#include "app/self_test.hh"
#include "version.hh"
#include "chain/header.hh"
#include "chain/target.hh"
#include "hash/argon2.hh"
#include "hash/aes.hh"
#include "hash/scrypt.hh"
#include "hash/heavyhash.hh"
#include "mine/gpu/device.hh"
#include "randomx/machine.hh"
#include "base/bits.hh"
#include "base/system/clock.hh"
#include "base/system/console.hh"
#include "base/memory.hh"
#include "base/strings.hh"
#include "base/system/hardware.hh"
#include "base/text.hh"

namespace app {

	static constexpr uint32_t bitcoin_nonce = 2083236893u;
	static constexpr uint32_t litecoin_nonce = 2084524493u;

	static bool same_hex ( const uint8_t* bytes, size_t count, const char* expected, bool reversed ) {

		uint8_t wanted [ 64 ];

		if ( count > sizeof ( wanted ) || !base::read_hex ( expected, wanted, count ) )
			return false;

		if ( reversed )
			base::reverse_bytes ( wanted, count );

		return base::same ( bytes, wanted, count );

	}

	template < typename test >
	static void record ( self_report& out, const wchar_t* name, test&& body ) {

		if ( out.count >= check_room )
			return;

		check_result& item = out.checks [ out.count++ ];
		double began = base::seconds ( );

		base::copy_text ( item.name, base::count_of ( item.name ), name );
		item.passed = body ( item );
		item.seconds = base::seconds ( ) - began;

		if ( !item.passed && !item.skipped )
			out.failed++;

	}

	static void skip ( self_report& out, const wchar_t* name ) {

		if ( out.count >= check_room )
			return;

		check_result& item = out.checks [ out.count++ ];

		base::copy_text ( item.name, base::count_of ( item.name ), name );
		item.skipped = true;

	}

	static bool gpu_finds ( mine::gpu_device& device, uint8_t algorithm, const chain::header& header, uint32_t nonce, uint32_t count ) {

		uint8_t target [ 32 ];
		uint32_t found [ 32 ];
		uint32_t found_count = 0;

		chain::target_of ( header.bits ( ), target );

		if ( !device.search ( algorithm, header, nonce - count / 2, count, base::load_little ( target + 28 ), found, base::count_of ( found ), found_count ) )
			return false;

		for ( uint32_t i = 0; i < found_count; i++ ) {

			if ( found [ i ] == nonce )
				return true;

		}

		return false;

	}

	void run_self_test ( self_report& out, bool deep ) {

		base::processor cpu;
		double began = base::seconds ( );

		out = self_report { };
		base::detect ( cpu );

		if ( !hash::aes_ready ( ) )
			hash::aes_prepare ( cpu.aes );

		record ( out, L"SHA-256 of \"abc\" matches FIPS 180-4", [ & ] ( check_result& ) {

			uint8_t digest [ 32 ];

			hash::sha256_once ( "abc", 3, digest );

			return same_hex ( digest, 32, "ba7816bf8f01cfea414140de5dae2223b00361a396177a9cb410ff61f20015ad", false );

		} );

		chain::header bitcoin;
		chain::header litecoin;

		chain::genesis ( bitcoin );
		chain::litecoin_genesis ( litecoin );

		record ( out, L"the portable SHA-256d kernel hashes Bitcoin's genesis block", [ & ] ( check_result& ) {

			chain::header_hasher hasher;
			uint8_t digest [ 32 ];

			hasher.prepare ( bitcoin, hash::sha256_block );
			hasher.hash ( bitcoin_nonce, digest );

			return same_hex ( digest, 32, "000000000019d6689c085ae165831e934ff763ae46a2a6c172b3f1b60a8ce26f", true );

		} );

		if ( cpu.sha && cpu.sse41 && cpu.ssse3 ) {

			record ( out, L"the SHA extensions kernel hashes Bitcoin's genesis block", [ & ] ( check_result& ) {

				chain::header_hasher hasher;
				uint8_t digest [ 32 ];

				hasher.prepare ( bitcoin, hash::sha256_block_extensions );
				hasher.hash ( bitcoin_nonce, digest );
				out.sha_extensions = same_hex ( digest, 32, "000000000019d6689c085ae165831e934ff763ae46a2a6c172b3f1b60a8ce26f", true );

				return out.sha_extensions;

			} );

		} else
			skip ( out, L"this processor has no SHA extensions" );

		record ( out, L"scrypt matches RFC 7914 (N 1024, r 8, p 16)", [ & ] ( check_result& ) {

			uint8_t key [ 64 ];

			return hash::scrypt ( "password", 8, "NaCl", 4, 1024, 8, 16, key, 64 ) && same_hex ( key, 64, "fdbabe1c9d3472007856e7190d01e9fe7c6ad7cbc8237830e77376634b3731622eaf30d92e22a3886ff109279d9830dac727afb94a83ee6d8360cbdfa2cc0640", false );

		} );

		record ( out, L"every scrypt kernel finds Litecoin's genesis nonce", [ & ] ( check_result& ) {

			void* scratch = base::allocate ( hash::scrypt_quad_scratch_bytes );
			uint8_t target [ 32 ];
			uint8_t single [ 32 ];
			uint8_t pair [ 2 ] [ 32 ];
			uint8_t quad [ 4 ] [ 32 ];
			chain::header headers [ 4 ] = { litecoin, litecoin, litecoin, litecoin };

			for ( uint32_t i = 0; i < 4; i++ )
				headers [ i ].set_nonce ( litecoin_nonce - 1 + i );

			chain::target_of ( litecoin.bits ( ), target );
			hash::scrypt_header ( headers [ 1 ].bytes, single, scratch );
			hash::scrypt_header_pair ( headers [ 1 ].bytes, headers [ 2 ].bytes, pair [ 0 ], pair [ 1 ], scratch );

			bool good = chain::meets ( single, target ) && base::same ( single, pair [ 0 ], 32 );

			if ( cpu.avx2 ) {

				const uint8_t* inputs [ 4 ] = { headers [ 0 ].bytes, headers [ 1 ].bytes, headers [ 2 ].bytes, headers [ 3 ].bytes };
				uint8_t* outputs [ 4 ] = { quad [ 0 ], quad [ 1 ], quad [ 2 ], quad [ 3 ] };

				hash::scrypt_header_quad ( inputs, outputs, scratch );
				good = good && base::same ( quad [ 1 ], single, 32 ) && base::same ( quad [ 2 ], pair [ 1 ], 32 );

			}

			base::release ( scratch );

			return good;

		} );

		record ( out, L"Argon2d matches the RFC 9106 vector", [ & ] ( check_result& ) {

			hash::argon2_block* blocks = new hash::argon2_block [ 32 ];
			uint8_t password [ 32 ];
			uint8_t salt [ 16 ];
			uint8_t secret [ 8 ];
			uint8_t data [ 12 ];
			uint8_t tag [ 32 ];
			hash::argon2_input input;

			base::fill ( password, 1, sizeof ( password ) );
			base::fill ( salt, 2, sizeof ( salt ) );
			base::fill ( secret, 3, sizeof ( secret ) );
			base::fill ( data, 4, sizeof ( data ) );
			input.password = password;
			input.password_size = sizeof ( password );
			input.salt = salt;
			input.salt_size = sizeof ( salt );
			input.secret = secret;
			input.secret_size = sizeof ( secret );
			input.data = data;
			input.data_size = sizeof ( data );
			input.tag_size = 32;
			hash::argon2d_fill ( blocks, 32, 3, 4, input );
			hash::argon2d_tag ( blocks, 32, 4, 32, tag );
			delete [ ] blocks;

			return same_hex ( tag, 32, "512b391b6f1162975371d30919734294f868e3be3984f3c1a13a4db9fabe4acb", false );

		} );

		record ( out, L"kHeavyHash's cSHAKE256 start states match Kaspa's", [ & ] ( check_result& ) {

			uint64_t pow [ 25 ];
			uint64_t finish [ 25 ];

			hash::heavy_start_states ( pow, finish );

			return pow [ 0 ] == 1242148031264380989ull && pow [ 16 ] == 1082795874807940378ull && finish [ 0 ] == 4239941492252378377ull && finish [ 16 ] == 570094237299545110ull;

		} );

		if ( deep ) {

			record ( out, L"RandomX hashes the reference vector compiled and interpreted", [ & ] ( check_result& ) {

				randomx::cache* light = new randomx::cache;
				randomx::machine* vm = new randomx::machine;
				uint8_t compiled [ 32 ] = { };
				uint8_t plain [ 32 ] = { };
				bool good = light->build ( "test key 000", 12 ) && vm->open ( light, nullptr );

				if ( good ) {

					vm->hash ( "This is a test", 14, compiled );
					vm->interpret = true;
					vm->hash ( "This is a test", 14, plain );

				}

				delete vm;
				delete light;

				return good && same_hex ( compiled, 32, "639183aae1bf4c9a35884cb46b09cad9175f04efd7684e7262a0ac1c2f0b4e3f", false ) && base::same ( compiled, plain, 32 );

			} );

		}

		mine::adapter_info adapters [ mine::adapter_room ];
		int32_t cards = mine::list_adapters ( adapters, mine::adapter_room );

		for ( int32_t i = 0; i < cards; i++ ) {

			mine::gpu_device* device = new mine::gpu_device;
			base::text name;

			if ( !device->open ( static_cast < uint32_t > ( i ) ) ) {

				name.add ( adapters [ i ].name ).add ( L" couldn't start Direct3D 11 compute" );
				skip ( out, name.get ( ) );
				delete device;

				continue;

			}

			name.add ( adapters [ i ].name ).add ( L" finds Bitcoin's genesis nonce with the SHA-256d shader" );
			record ( out, name.get ( ), [ & ] ( check_result& ) {

				bool good = gpu_finds ( *device, chain::ALGORITHM_SHA256D, bitcoin, bitcoin_nonce, 1u << 16 );

				out.broken_sha_cards |= good ? 0u : 1u << i;

				return good;

			} );

			name.clear ( );
			name.add ( adapters [ i ].name ).add ( L" finds Litecoin's genesis nonce with the scrypt shaders" );
			record ( out, name.get ( ), [ & ] ( check_result& item ) {

				if ( !device->capacity ( chain::ALGORITHM_SCRYPT ) ) {

					item.skipped = true;

					return false;

				}

				bool good = gpu_finds ( *device, chain::ALGORITHM_SCRYPT, litecoin, litecoin_nonce, 4096 );

				out.broken_scrypt_cards |= good ? 0u : 1u << i;

				return good;

			} );

			name.clear ( );
			name.add ( adapters [ i ].name ).add ( L" matches the CPU on kHeavyHash for 1024 nonces" );
			record ( out, name.get ( ), [ & ] ( check_result& item ) {

				if ( !device->capacity ( chain::ALGORITHM_KHEAVYHASH ) ) {

					item.skipped = true;

					return false;

				}

				hash::heavy_hasher* hasher = new hash::heavy_hasher;
				uint32_t* tops = new uint32_t [ 1024 ];
				uint8_t seed [ 32 ];
				uint8_t digest [ 32 ];

				hash::sha256_twice ( "alloy self-test", 15, seed );
				hasher->prepare ( seed, 1700000000000ull );

				bool good = device->sample_heavy ( *hasher, 7, 100, 1024, tops );

				for ( uint32_t k = 0; good && k < 1024; k++ ) {

					hasher->hash ( 7ull << 32 | ( 100 + k ), digest );
					good = base::load_little ( digest + 28 ) == tops [ k ];

				}

				delete [ ] tops;
				delete hasher;
				out.broken_heavy_cards |= good ? 0u : 1u << i;

				return good;

			} );

			delete device;

		}

		out.seconds = base::seconds ( ) - began;

	}

	uint32_t self_test_command ( ) {

		self_report* report = new self_report;
		base::text line;

		base::print_line ( L"Alloy " ALLOY_VERSION_TEXT L" self-test" );
		base::print_line ( L"" );
		run_self_test ( *report, true );

		for ( int32_t i = 0; i < report->count; i++ ) {

			const check_result& item = report->checks [ i ];

			line.clear ( );
			line.add ( item.skipped ? L"  skip  " : ( item.passed ? L"  pass  " : L"  FAIL  " ) ).add ( item.name );

			if ( !item.skipped )
				line.add ( L" (" ).decimal ( item.seconds * 1000.0, 0 ).add ( L" ms)" );

			base::print_line ( line.get ( ) );

		}

		line.clear ( );
		line.add ( L"" );
		base::print_line ( L"" );
		line.number ( static_cast < uint64_t > ( report->count - report->failed ) ).add ( L" of " ).number ( static_cast < uint64_t > ( report->count ) ).add ( report->failed ? L" checks passed. The failed kernels give wrong answers on this machine, so Alloy won't mine with them." : L" checks passed in " );

		if ( !report->failed )
			line.decimal ( report->seconds, 1 ).add ( L" s" );

		base::print_line ( line.get ( ) );

		uint32_t code = report->failed ? 1 : 0;

		delete report;

		return code;

	}

}
