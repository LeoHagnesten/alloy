#ifdef ALLOY_DEBUG

#include "tests/harness.hh"
#include "randomx/machine.hh"
#include "hash/argon2.hh"
#include "hash/blake2b.hh"
#include "base/bits.hh"
#include "base/system/clock.hh"
#include "base/memory.hh"
#include "base/strings.hh"
#include "base/system/hardware.hh"
#include "base/system/threads.hh"
#include "base/text.hh"

namespace tests {

	struct variant_case {

		uint8_t kind;
		const char* key;
		const char* input;
		bool hex;
		const char* expected;

	};

	static const char lorem [ ] = "sed do eiusmod tempor incididunt ut labore et dolore magna aliqua";
	static const char ocean_key [ ] = "12345678901234567890123456789012";
	static const char block_blob [ ] = "0b0b98bea7e805e0010a2126d287a2a0cc833d312cb786385a7c2f9de69d25537f584a9bc9977b00000000666fd8753bf61a8631f12984e3fd44f4014eca629276817b56f32e9b68bd82f416";

	static const variant_case variant_cases [ ] = {

		{ randomx::VARIANT_RX2, "test key 000", "This is a test", false, "22ec6b861b3eb23686b2efbad69513c967ecfce80983df66c9c5b4fbfb4cdb6f" },
		{ randomx::VARIANT_RX2, "test key 000", "Lorem ipsum dolor sit amet", false, "9e2c772c12fd48f93c14c97fdc89d556264d9100597023f44d9163e279012ecf" },
		{ randomx::VARIANT_RX2, "test key 000", lorem, false, "4d6b063a1a603751d525f18a171336a4002f2f06df6c17e4b25fe17e17796e42" },
		{ randomx::VARIANT_ARQ, "test key 000", "This is a test", false, "27f66e4650eb5657513e76c140e09e59336786f21fbef1ed6ff40fc21538221e" },
		{ randomx::VARIANT_ARQ, "test key 000", "Lorem ipsum dolor sit amet", false, "6b04e883e07e4e6c072cb064d9aed0fa5a8f7cbbeb7fd3ba653d274ebd4925b0" },
		{ randomx::VARIANT_ARQ, "test key 000", lorem, false, "219d023039c92bdf98df769a46d2cea6f1f3ceafcba2334edfbd90499a4b516f" },
		{ randomx::VARIANT_ARQ, "test key 001", lorem, false, "665cc87ea512d5353c39e60472e8937373e59c648288b82846847acc226dfcb7" },
		{ randomx::VARIANT_ARQ, "test key 001", block_blob, true, "f7dac83c948cc4a3429723c2059e536dda6f1f9d5325f02216db7f1cf82f5aa8" },
		{ randomx::VARIANT_RX0, ocean_key, "This is a test", false, "38f638606c730dd6f271d037556b83988c71acc6980e22e25271b22389ecfce6" },
		{ randomx::VARIANT_RX0, ocean_key, "Lorem ipsum dolor sit amet", false, "86cb0f6306d536f373650bd196a5205a0293fba2ead85003d7aee4006afee147" },
		{ randomx::VARIANT_RX0, ocean_key, lorem, false, "375aa54e18029b6f04372dd51b7349f130810b1270e67a22f26c15dab6f93a65" },
		{ randomx::VARIANT_GRAFT, ocean_key, "This is a test", false, "08135aaeb098d86f269d0a037aba423093e4c48e9e31f16c13d2ed3dc4489ba7" },
		{ randomx::VARIANT_GRAFT, ocean_key, "Lorem ipsum dolor sit amet", false, "200047b130fd8fc97b59ddf4f720e6c7a3784c221a9c4453ad7f00dae310b576" },
		{ randomx::VARIANT_GRAFT, ocean_key, lorem, false, "a01b6795ed4063683f15b0f488d55e1aff164c66b0c1686f34a18b48445dbeaa" },
		{ randomx::VARIANT_KEVA, ocean_key, "This is a test", false, "745ac75a1d5d044ebd5cbc97269c1f75b1bfeb6ce2df4ed700be898356a879df" },
		{ randomx::VARIANT_KEVA, ocean_key, "Lorem ipsum dolor sit amet", false, "a17feef611cbb7ad1802f2af62961b4219f76952a1b08b7892cf1fc87b4870a3" },
		{ randomx::VARIANT_KEVA, ocean_key, lorem, false, "dc4eba2a3b047ea8d45545fddd9d1f23332a4e85a8d42f0691939ba61d85d469" },
		{ randomx::VARIANT_XEQ, ocean_key, "This is a test", false, "2d4dc87b3d1d8edf238b4adc546439388f1e9a796066f519f2c0cb2e9fe6fcd2" },
		{ randomx::VARIANT_XEQ, ocean_key, "Lorem ipsum dolor sit amet", false, "20d6111232c7b14a28b7385d1f9f5ed058bfa3ec2ef1a96d0bbb5f66609dfbfd" },
		{ randomx::VARIANT_XEQ, ocean_key, lorem, false, "6cf7cd127bf94742468b9d3c7ec6aa4f9b3600f8f4891fc7da6db9a2b7d3db23" },

	};

	static size_t input_of ( const variant_case& item, uint8_t* out ) {

		size_t size = static_cast < size_t > ( base::length ( item.input ) );

		if ( !item.hex ) {

			base::copy ( out, item.input, size );

			return size;

		}

		base::read_hex ( item.input, out, size / 2 );

		return size / 2;

	}

	static int32_t run_cases ( randomx::machine& vm, randomx::cache* light, randomx::dataset* fast, uint8_t kind, bool interpret ) {

		int32_t passed = 0;
		const char* built = fast ? "test key 000" : nullptr;

		for ( const variant_case& item : variant_cases ) {

			if ( item.kind != kind )
				continue;

			if ( !built || !base::same_text ( built, item.key ) ) {

				if ( fast )
					return passed;

				light->build ( item.key, static_cast < size_t > ( base::length ( item.key ) ), kind );
				built = item.key;

			}

			uint8_t input [ 128 ];
			uint8_t out [ 32 ];
			size_t size = input_of ( item, input );

			if ( !vm.open ( fast ? nullptr : light, fast, kind ) )
				return passed;

			vm.interpret = interpret;
			vm.hash_core ( input, size, out );
			vm.interpret = false;
			passed += hex_is ( out, 32, item.expected ) ? 1 : 0;

		}

		return passed;

	}

	static int32_t cases_for ( uint8_t kind ) {

		int32_t count = 0;

		for ( const variant_case& item : variant_cases )
			count += item.kind == kind ? 1 : 0;

		return count;

	}

	static bool modes_agree ( randomx::machine& vm, int32_t count ) {

		uint8_t input [ 76 ];
		uint8_t native [ 32 ];
		uint8_t plain [ 32 ];

		for ( int32_t i = 0; i < count; i++ ) {

			for ( int32_t k = 0; k < 76; k++ )
				input [ k ] = static_cast < uint8_t > ( k * 29 + i * 131 + 11 );

			vm.interpret = false;
			vm.hash ( input, 76, native );
			vm.interpret = true;
			vm.hash ( input, 76, plain );
			vm.interpret = false;

			if ( !base::same ( native, plain, 32 ) )
				return false;

		}

		return vm.compiled ( );

	}

	void variant_tests ( ) {

		section ( L"randomx variants" );

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
		check ( hex_is ( tag, 32, "512b391b6f1162975371d30919734294f868e3be3984f3c1a13a4db9fabe4acb" ), L"four-lane Argon2d matches the RFC 9106 vector, which Graft's two-lane cache relies on" );
		delete [ ] blocks;

		bool table_ok = true;

		for ( uint8_t i = 0; i < randomx::VARIANT_COUNT; i++ ) {

			const randomx::variant& rules = randomx::variant_of ( i );
			int32_t total = 0;

			for ( uint8_t weight : rules.frequency )
				total += weight;

			table_ok = table_ok && total == 256 && randomx::find_variant ( rules.key ) == i && rules.program_size <= randomx::program_room && rules.level3 <= randomx::scratchpad_room;

		}

		check ( table_ok && randomx::find_variant ( "RandomX" ) == randomx::VARIANT_RX0 && randomx::find_variant ( "rx/v2" ) == randomx::VARIANT_RX2 && randomx::find_variant ( "cn/r" ) < 0, L"every variant's instruction weights add up to 256 and its pool name maps back to it" );

		randomx::cache* light = new randomx::cache;
		randomx::machine* vm = new randomx::machine;
		base::text note;
		int32_t compiled = run_cases ( *vm, light, nullptr, randomx::VARIANT_RX2, false );
		int32_t interpreted = run_cases ( *vm, light, nullptr, randomx::VARIANT_RX2, true );

		note.add ( L"RandomX v2 hashes its three reference vectors compiled and interpreted (" ).number ( static_cast < uint64_t > ( compiled ) ).add ( L" and " ).number ( static_cast < uint64_t > ( interpreted ) ).add ( L" of 3)" );
		check ( compiled == 3 && interpreted == 3, note.get ( ) );

		uint8_t commitment [ 32 ];

		light->build ( "test key 000", 12, randomx::VARIANT_RX2 );
		vm->open ( light, nullptr, randomx::VARIANT_RX2 );
		vm->hash ( "This is a test", 14, commitment );
		check ( hex_is ( commitment, 32, "133be717399046b03ae82ce8ddd9d1ee4d3ea7fca03a50dec09b6848cbb98e18" ), L"RandomX v2 ends in the Blake2b commitment Monero checks against the target" );

		compiled = run_cases ( *vm, light, nullptr, randomx::VARIANT_ARQ, false );
		interpreted = run_cases ( *vm, light, nullptr, randomx::VARIANT_ARQ, true );
		note.clear ( );
		note.add ( L"RandomARQ hashes all five reference vectors compiled and interpreted (" ).number ( static_cast < uint64_t > ( compiled ) ).add ( L" and " ).number ( static_cast < uint64_t > ( interpreted ) ).add ( L" of " ).number ( static_cast < uint64_t > ( cases_for ( randomx::VARIANT_ARQ ) ) ).put ( L')' );
		check ( compiled == 5 && interpreted == 5, note.get ( ) );

		static const uint8_t ocean [ 4 ] = { randomx::VARIANT_RX0, randomx::VARIANT_GRAFT, randomx::VARIANT_KEVA, randomx::VARIANT_XEQ };
		static const wchar_t* const ocean_names [ 4 ] = { L"RandomX", L"RandomGRAFT, with its two-lane Argon2d and 280-instruction programs", L"RandomKEVA", L"RandomXEQ" };

		for ( int32_t i = 0; i < 4; i++ ) {

			compiled = run_cases ( *vm, light, nullptr, ocean [ i ], false );
			interpreted = run_cases ( *vm, light, nullptr, ocean [ i ], true );
			note.clear ( );
			note.add ( ocean_names [ i ] ).add ( L" matches MoneroOcean's pool vectors compiled and interpreted (" ).number ( static_cast < uint64_t > ( compiled ) ).add ( L" and " ).number ( static_cast < uint64_t > ( interpreted ) ).add ( L" of 3)" );
			check ( compiled == 3 && interpreted == 3, note.get ( ) );

		}

		bool agree = true;
		base::text failed;

		for ( uint8_t i = 0; i < randomx::VARIANT_COUNT; i++ ) {

			const randomx::variant& rules = randomx::variant_of ( i );

			light->build ( "alloy variant key", 17, i );

			bool same = vm->open ( light, nullptr, i ) && modes_agree ( *vm, 2 );

			if ( !same ) {

				wchar_t key [ 16 ];

				base::widen ( key, base::count_of ( key ), rules.key );
				failed.add ( failed.size ( ) ? L", " : L" (failed: " ).add ( key );

			}

			agree = agree && same;

		}

		if ( failed.size ( ) )
			failed.put ( L')' );

		note.clear ( );
		note.add ( L"compiled and interpreted light mode agree for all nine variants" ).add ( failed.get ( ) );
		check ( agree, note.get ( ) );

		randomx::dataset* fast = new randomx::dataset;
		base::processor cpu;

		base::detect ( cpu );
		light->build ( "test key 000", 12, randomx::VARIANT_RX2 );

		bool full = fast->build ( *light, cpu.threads, nullptr, nullptr );
		int32_t whole = full ? run_cases ( *vm, light, fast, randomx::VARIANT_RX2, false ) : 0;

		note.clear ( );
		note.add ( L"RandomX v2 in fast mode, with the AES mixing compiled into the loop, hashes the same vectors (" ).number ( static_cast < uint64_t > ( whole ) ).add ( L" of 3)" );
		check ( whole == 3 && vm->compiled ( ), note.get ( ) );
		check ( full && modes_agree ( *vm, 6 ), L"compiled and interpreted fast mode agree for RandomX v2" );

		delete fast;
		delete vm;
		delete light;

	}

	struct bench_share {

		const randomx::dataset* fast = nullptr;
		uint8_t kind = 0;
		uint32_t first = 0;
		uint32_t step = 1;
		uint32_t count = 0;
		uint64_t mixed = 0;
		uint64_t raw = 0;

	};

	static unsigned long bench_main ( void* context ) {

		bench_share& job = *static_cast < bench_share* > ( context );
		randomx::machine* vm = new randomx::machine;
		uint8_t blob [ 112 ] = { };
		uint8_t out [ 32 ];

		vm->open ( nullptr, job.fast, job.kind );

		for ( uint32_t nonce = job.first; nonce < job.count; nonce += job.step ) {

			base::store_little ( blob + 39, nonce );
			vm->hash_core ( blob, sizeof ( blob ), out );
			job.raw ^= base::load_little64 ( out + 24 );

			if ( randomx::variant_of ( job.kind ).v2 ) {

				hash::blake2b commitment ( 32 );
				uint8_t result [ 32 ];

				commitment.add ( blob, sizeof ( blob ) );
				commitment.add ( out, 32 );
				commitment.end ( result );
				job.mixed ^= base::load_little64 ( result + 24 );

			} else
				job.mixed ^= base::load_little64 ( out + 24 );

		}

		delete vm;

		return 0;

	}

	void benchmark_tests ( ) {

		section ( L"xmrig benchmark checksums" );

		struct expectation {

			uint8_t kind;
			uint64_t value;

		};

		static const expectation expected [ ] = {

			{ randomx::VARIANT_RX0, 0x7D6054757BB08A63ull },
			{ randomx::VARIANT_WOW, 0xC7F712C9603E2603ull },
			{ randomx::VARIANT_RX2, 0x18cf741a71484072ull },

		};

		base::processor cpu;
		randomx::cache* light = new randomx::cache;
		randomx::dataset* fast = new randomx::dataset;
		uint8_t seed [ 32 ] = { };

		base::detect ( cpu );

		for ( const expectation& item : expected ) {

			const randomx::variant& rules = randomx::variant_of ( item.kind );
			double began = base::seconds ( );

			light->build ( seed, 32, item.kind );

			bool full = fast->build ( *light, cpu.threads, nullptr, nullptr );
			uint32_t threads = base::clamp ( cpu.threads, 1u, 64u );
			bench_share shares [ 64 ];
			void* handles [ 64 ] = { };
			uint64_t mixed = 0;
			uint64_t raw = 0;

			for ( uint32_t i = 0; i < threads && full; i++ ) {

				shares [ i ].fast = fast;
				shares [ i ].kind = item.kind;
				shares [ i ].first = i;
				shares [ i ].step = threads;
				shares [ i ].count = 250000;
				handles [ i ] = base::start_thread ( &bench_main, &shares [ i ] );

			}

			for ( uint32_t i = 0; i < threads && full; i++ ) {

				base::join_thread ( handles [ i ] );
				mixed ^= shares [ i ].mixed;
				raw ^= shares [ i ].raw;

			}

			wchar_t key [ 16 ];
			base::text note;

			base::widen ( key, base::count_of ( key ), rules.key );
			note.add ( key ).add ( L": 250,000 hashes of XMRig's benchmark blob fold to " ).hex64 ( mixed ).add ( L" in " ).decimal ( base::seconds ( ) - began, 0 ).add ( L" s" );

			if ( mixed != item.value && raw == item.value )
				note.add ( L" (the raw hash matches, not the commitment)" );

			check ( full && mixed == item.value, note.get ( ) );

		}

		delete fast;
		delete light;

	}

}

#endif
