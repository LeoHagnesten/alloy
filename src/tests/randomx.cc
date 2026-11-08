#ifdef ALLOY_DEBUG

#include "tests/harness.hh"
#include "randomx/machine.hh"
#include "hash/aes.hh"
#include "hash/blake2b.hh"
#include "base/bits.hh"
#include "base/system/clock.hh"
#include "base/memory.hh"
#include "base/strings.hh"
#include "base/system/hardware.hh"
#include "base/text.hh"

namespace tests {

	struct hash_case {

		const char* key;
		const char* input;
		bool hex;
		const char* expected;

	};

	static const hash_case hash_cases [ ] = {

		{ "test key 000", "This is a test", false, "639183aae1bf4c9a35884cb46b09cad9175f04efd7684e7262a0ac1c2f0b4e3f" },
		{ "test key 000", "Lorem ipsum dolor sit amet", false, "300a0adb47603dedb42228ccb2b211104f4da45af709cd7547cd049e9489c969" },
		{ "test key 000", "sed do eiusmod tempor incididunt ut labore et dolore magna aliqua", false, "c36d4ed4191e617309867ed66a443be4075014e2b061bcdaf9ce7b721d2b77a8" },
		{ "test key 001", "sed do eiusmod tempor incididunt ut labore et dolore magna aliqua", false, "e9ff4503201c0c2cca26d285c93ae883f9b1d30c9eb240b820756f2d5a7905fc" },
		{ "test key 001", "0b0b98bea7e805e0010a2126d287a2a0cc833d312cb786385a7c2f9de69d25537f584a9bc9977b00000000666fd8753bf61a8631f12984e3fd44f4014eca629276817b56f32e9b68bd82f416", true, "c56414121acda1713c2f2a819d8ae38aed7c80c35c2a769298d34f03833cd5f1" },

	};

	static bool hash_matches ( randomx::machine& vm, const hash_case& item ) {

		uint8_t input [ 128 ];
		size_t size = static_cast < size_t > ( base::length ( item.input ) );
		uint8_t out [ 32 ];

		if ( item.hex ) {

			size /= 2;
			base::read_hex ( item.input, input, size );

		} else
			base::copy ( input, item.input, size );

		vm.hash ( input, size, out );

		return hex_is ( out, 32, item.expected );

	}

	static bool agrees ( randomx::machine& vm, int32_t count ) {

		uint8_t input [ 76 ];
		uint8_t native [ 32 ];
		uint8_t plain [ 32 ];
		bool same = true;

		for ( int32_t i = 0; i < count && same; i++ ) {

			for ( int32_t k = 0; k < 76; k++ )
				input [ k ] = static_cast < uint8_t > ( k * 31 + i * 97 + 5 );

			vm.interpret = false;
			vm.hash ( input, 76, native );
			vm.interpret = true;
			vm.hash ( input, 76, plain );
			same = base::same ( native, plain, 32 );

		}

		vm.interpret = false;

		return same && vm.compiled ( );

	}

	static double hash_rate ( randomx::machine& vm, bool interpret, double span ) {

		uint8_t input [ 76 ] = { };
		uint8_t out [ 32 ];
		uint32_t count = 0;
		double began = base::seconds ( );

		vm.interpret = interpret;

		while ( base::seconds ( ) - began < span ) {

			base::store_little ( input + 39, count++ );
			vm.hash ( input, 76, out );

		}

		vm.interpret = false;

		return count / ( base::seconds ( ) - began );

	}

	void randomx_tests ( ) {

		section ( L"randomx" );

		uint8_t digest [ 64 ];

		hash::blake2b_once ( digest, 64, "abc", 3 );
		check ( hex_is ( digest, 64, "ba80a53f981c4d0d6a2797b69f12f6e94c212f14685ac4b74b12bb6fdbffa2d17d87c5392aab792dc252d5de4533cc9518d38aa8dbf1925ab92386edd4009923" ), L"Blake2b-512 of \"abc\" matches RFC 7693" );
		hash::blake2b_once ( digest, 64, "", 0 );
		check ( hex_is ( digest, 64, "786a02f742015903c6c6fd852552d272912f4740e15847618a86e217f71f5419d25e1031afee585313896444934eb04b903a685b1448b755d56f701afe9be2ce" ), L"Blake2b-512 of the empty string matches" );

		{

			uint8_t key [ 64 ];

			for ( int32_t i = 0; i < 64; i++ )
				key [ i ] = static_cast < uint8_t > ( i );

			hash::blake2b keyed ( 64, key, 64 );

			keyed.end ( digest );
			check ( hex_is ( digest, 64, "10ebb67700b1868efb4417987acf4690ae9d972fb7a590c2f02871799aaa4786b5e996e8f0f4eb981fc214b005f42d2ff4233499391653df7aefcbc13fc51568" ), L"keyed Blake2b-512 matches the reference answer for an empty message" );

		}

		base::processor cpu;

		base::detect ( cpu );

		uint8_t state [ 64 ] = { };
		uint8_t soft [ 64 ] = { };

		base::read_hex ( "6c19536eb2de31b6c0065f7f116e86f960d8af0c57210a6584c3237b9d064dc7", state, 32 );
		base::copy ( soft, state, 64 );
		hash::aes_prepare ( false );
		hash::aes_fill_1r ( soft, soft, 64 );
		check ( hex_is ( soft, 32, "fa89397dd6ca422513aeadba3f124b5540324c4ad4b6db434394307a17c833ab" ), L"the software AES round reproduces the AesGenerator1R vector" );

		if ( cpu.aes ) {

			uint8_t block [ 4096 ];
			uint8_t software [ 2 ] [ 4096 ];
			uint8_t hardware [ 2 ] [ 4096 ];
			uint8_t hashed [ 2 ] [ 64 ];

			for ( int32_t i = 0; i < 4096; i++ )
				block [ i ] = static_cast < uint8_t > ( i * 151 + 7 );

			hash::aes_prepare ( false );
			hash::aes_fill_4r ( block, software [ 0 ], 4096 );
			base::copy ( software [ 1 ], block, 64 );
			hash::aes_fill_1r ( software [ 1 ], software [ 1 ], 4096 );
			hash::aes_hash_1r ( block, 4096, hashed [ 0 ] );
			hash::aes_prepare ( true );
			hash::aes_fill_4r ( block, hardware [ 0 ], 4096 );
			base::copy ( hardware [ 1 ], block, 64 );
			hash::aes_fill_1r ( hardware [ 1 ], hardware [ 1 ], 4096 );
			hash::aes_hash_1r ( block, 4096, hashed [ 1 ] );
			check ( base::same ( software, hardware, sizeof ( software ) ) && base::same ( hashed [ 0 ], hashed [ 1 ], 64 ), L"AES-NI and the software rounds agree on all three generators" );

		}

		hash::aes_prepare ( cpu.aes );

		check ( randomx::reciprocal ( 3 ) == 12297829382473034410ull && randomx::reciprocal ( 13 ) == 11351842506898185609ull && randomx::reciprocal ( 33 ) == 17887751829051686415ull && randomx::reciprocal ( 65537 ) == 18446462603027742720ull && randomx::reciprocal ( 15000001 ) == 10316166306300415204ull && randomx::reciprocal ( 3845182035u ) == 10302264209224146340ull && randomx::reciprocal ( 0xFFFFFFFFu ) == 9223372039002259456ull, L"reciprocals match the reference values" );

		static const char* const programs [ 10 ] = {

			"d3a4a6623738756f77e6104469102f082eff2a3e60be7ad696285ef7dfc72a61",
			"f5e7e0bbc7e93c609003d6359208688070afb4a77165a552ff7be63b38dfbc86",
			"85ed8b11734de5b3e9836641413a8f36e99e89694f419c8cd25c3f3f16c40c5a",
			"5dd956292cf5d5704ad99e362d70098b2777b2a1730520be52f772ca48cd3bc0",
			"6f14018ca7d519e9b48d91af094c0f2d7e12e93af0228782671a8640092af9e5",
			"134be097c92e2c45a92f23208cacd89e4ce51f1009a0b900dbe83b38de11d791",
			"268f9392c20c6e31371a5131f82bd7713d3910075f2f0468baafaa1abd2f3187",
			"c668a05fd909714ed4a91e8d96d67b17e44329e88bc71e0672b529a3fc16be47",
			"99739351315840963011e4c5d8e90ad0bfed3facdcb713fe8f7138fbf01c4c94",
			"14ab53d61880471f66e80183968d97effd5492b406876060e595fcf9682f9295",

		};

		randomx::scalar_program* program = new randomx::scalar_program;
		randomx::seed_generator random ( "test key 000", 12 );
		int32_t matching = 0;

		for ( int32_t i = 0; i < 10; i++ ) {

			uint8_t print [ 32 ];

			randomx::generate_superscalar ( *program, random );
			hash::blake2b_once ( print, 32, program->code, sizeof ( randomx::instruction ) * static_cast < size_t > ( program->size ) );
			matching += hex_is ( print, 32, programs [ i ] ) ? 1 : 0;

		}

		delete program;

		base::text note;

		note.add ( L"SuperscalarHash generates the 10 reference programs (" ).number ( static_cast < uint64_t > ( matching ) ).add ( L" of 10 match)" );
		check ( matching == 10, note.get ( ) );

		randomx::cache* light = new randomx::cache;
		double began = base::seconds ( );
		bool built = light->build ( "test key 000", 12 );
		double argon_time = base::seconds ( ) - began;

		note.clear ( );
		note.add ( L"Argon2d fills the 256 MiB cache with the reference words in " ).decimal ( argon_time, 2 ).add ( L" s" );
		check ( built && light->words ( ) [ 0 ] == 0x191e0e1d23c02186ull && light->words ( ) [ 1568413 ] == 0xf1b62fe6210bf8b1ull && light->words ( ) [ 33554431 ] == 0x1f47f056d05cd99bull, note.get ( ) );

		uint64_t item [ 8 ];
		bool items = built;
		static const uint64_t numbers [ 4 ] = { 0, 10000000, 20000000, 30000000 };
		static const uint64_t firsts [ 4 ] = { 0x680588a85ae222dbull, 0x7943a1f6186ffb72ull, 0x9035244d718095e1ull, 0x145a5091f7853099ull };

		for ( int32_t i = 0; i < 4 && items; i++ ) {

			light->item ( numbers [ i ], item );
			items = item [ 0 ] == firsts [ i ];

		}

		check ( items, L"dataset items 0, 10M, 20M and 30M match the reference" );

		bool agree = built && light->compiled ( ).ready ( );

		for ( uint64_t i = 0; i < 20000 && agree; i++ ) {

			uint64_t number = i * 2654435761ull % randomx::dataset_items;
			uint64_t native [ 8 ];

			light->interpret = true;
			light->item ( number, item );
			light->interpret = false;
			light->item ( number, native );
			agree = base::same ( item, native, 64 );

		}

		light->interpret = false;
		check ( agree, L"compiled SuperscalarHash matches the interpreter on 20000 items" );

		randomx::machine* vm = new randomx::machine;
		int32_t passed = 0;

		if ( built && vm->open ( light, nullptr ) ) {

			for ( int32_t i = 0; i < 3; i++ )
				passed += hash_matches ( *vm, hash_cases [ i ] ) ? 1 : 0;

			light->build ( "test key 001", 12 );

			for ( int32_t i = 3; i < 5; i++ )
				passed += hash_matches ( *vm, hash_cases [ i ] ) ? 1 : 0;

		}

		note.clear ( );
		note.add ( L"compiled light mode hashes all five reference vectors, including a Monero block blob (" ).number ( static_cast < uint64_t > ( passed ) ).add ( L" of 5)" );
		check ( passed == 5 && vm->compiled ( ), note.get ( ) );
		check ( agrees ( *vm, 4 ), L"compiled and interpreted light mode agree on four more inputs" );

		randomx::dataset* fast = new randomx::dataset;

		began = base::seconds ( );
		light->build ( "test key 000", 12 );

		bool full = fast->build ( *light, cpu.threads, nullptr, nullptr );
		double dataset_time = base::seconds ( ) - began;

		passed = 0;

		if ( full && vm->open ( nullptr, fast ) ) {

			for ( int32_t i = 0; i < 3; i++ )
				passed += hash_matches ( *vm, hash_cases [ i ] ) ? 1 : 0;

		}

		note.clear ( );
		note.add ( L"the full 2 GiB dataset builds in " ).decimal ( dataset_time, 1 ).add ( L" s and compiled fast mode hashes the same vectors" );
		check ( passed == 3 && vm->compiled ( ), note.get ( ) );

		bool fast_agrees = full && agrees ( *vm, 16 );
		double native_rate = full ? hash_rate ( *vm, false, 1.0 ) : 0.0;
		double plain_rate = full ? hash_rate ( *vm, true, 1.0 ) : 0.0;

		note.clear ( );
		note.add ( L"compiled fast mode matches the interpreter on 16 inputs and runs " ).decimal ( plain_rate > 0.0 ? native_rate / plain_rate : 0.0, 1 ).add ( L"x faster on one thread (" ).decimal ( native_rate, 0 ).add ( L" against " ).decimal ( plain_rate, 0 ).add ( L" H/s)" );
		check ( fast_agrees && native_rate > plain_rate * 2.0, note.get ( ) );

		uint8_t* bytes = new uint8_t [ 128 + 8 * randomx::program_room ];
		base::code_view view;
		randomx::program_shape shape;
		uint8_t seed [ 64 ] = { 1, 2, 3 };
		bool emitted = base::open_code_view ( view, randomx::native_room );
		double emit_began = base::seconds ( );
		int32_t emits = 0;

		shape.whole_loop = true;

		for ( ; emits < 2000 && emitted; emits++ ) {

			hash::aes_fill_4r ( seed, bytes, 128 + 8 * 256 );
			emitted = randomx::emit_program ( bytes + 128, shape, view.write, randomx::native_room );

		}

		double emit_time = ( base::seconds ( ) - emit_began ) / base::max ( emits, 1 ) * 1e6;

		note.clear ( );
		note.add ( L"generating and compiling one program takes " ).decimal ( emit_time, 1 ).add ( L" us" );
		check ( emitted && emit_time < 100.0, note.get ( ) );
		base::close_code_view ( view );
		delete [ ] bytes;

		delete vm;
		delete fast;
		delete light;

	}

}

#endif
