#ifdef ALLOY_DEBUG

#include "tests/harness.hh"
#include "hash/heavyhash.hh"
#include "mine/gpu/device.hh"
#include "base/bits.hh"
#include "base/memory.hh"
#include "base/text.hh"

namespace tests {

	void kaspa_tests ( ) {

		section ( L"kaspa" );

		uint8_t out [ 32 ];
		static const uint8_t counting [ 4 ] = { 0, 1, 2, 3 };

		hash::cshake ( 136, "", "", nullptr, 0, out, 32 );
		check ( hex_is ( out, 32, "46b9dd2b0ba88d13233b3feb743eeb243fcd52ea62b81b82b50c27646ed5762f" ), L"SHAKE256 of nothing matches FIPS 202" );
		hash::cshake ( 168, "", "Email Signature", counting, 4, out, 32 );
		check ( hex_is ( out, 32, "c1c36925b6409a04f1b504fcbca9d82b4017277cb5ed2b2065fc1d3814d5aaf5" ), L"cSHAKE128 matches NIST SP 800-185 sample 1" );

		uint64_t pow [ 25 ];
		uint64_t finish [ 25 ];

		hash::heavy_start_states ( pow, finish );
		check ( pow [ 0 ] == 1242148031264380989ull && pow [ 1 ] == 3008272977830772284ull && pow [ 10 ] == 1123092876221303306ull && pow [ 16 ] == 1082795874807940378ull, L"the ProofOfWorkHash start state matches Kaspa's, padding included" );
		check ( finish [ 0 ] == 4239941492252378377ull && finish [ 1 ] == 8746723911537738262ull && finish [ 4 ] == 16654558671554924250ull && finish [ 16 ] == 570094237299545110ull, L"the HeavyHash start state matches Kaspa's, padding included" );

		hash::xoshiro256 random;

		random.state [ 0 ] = 1;
		random.state [ 1 ] = 2;
		random.state [ 2 ] = 3;
		random.state [ 3 ] = 4;
		check ( random.next ( ) == 41943041ull, L"xoshiro256++ gives the reference first value" );

		hash::heavy_matrix* grid = new hash::heavy_matrix;
		uint8_t seed [ 32 ];

		for ( int32_t i = 0; i < 32; i++ )
			seed [ i ] = static_cast < uint8_t > ( i * 7 + 1 );

		hash::heavy_matrix_from ( seed, *grid );

		bool full = hash::heavy_rank ( *grid ) == 64;

		base::copy ( grid->cells [ 5 ], grid->cells [ 9 ], 64 );
		check ( full && hash::heavy_rank ( *grid ) == 63, L"the matrix comes out at full rank, and a repeated row drops it to 63" );
		delete grid;

		hash::heavy_hasher* hasher = new hash::heavy_hasher;
		uint32_t* tops = new uint32_t [ mine::sample_room ];
		uint8_t digest [ 32 ];

		hasher->prepare ( seed, 1700000000123ull );

		auto compare = [ & ] ( uint32_t adapter, uint32_t count, const wchar_t* what ) {

			mine::gpu_device* device = new mine::gpu_device;
			bool opened = device->open ( adapter );
			bool sampled = opened && device->sample_heavy ( *hasher, 0x1234, 5000, count, tops );
			uint32_t agree = 0;

			for ( uint32_t i = 0; sampled && i < count; i++ ) {

				hasher->hash ( 0x123400000000ull | ( 5000 + i ), digest );
				agree += base::load_little ( digest + 28 ) == tops [ i ] ? 1 : 0;

			}

			base::text note;

			note.add ( what ).add ( L" (" ).number ( agree ).add ( L" of " ).number ( count ).put ( L')' );

			if ( !opened )
				note.add ( L", no adapter" );

			check ( !opened || agree == count, note.get ( ) );
			delete device;

		};

		compare ( 0, 4096, L"the graphics card's kHeavyHash matches the CPU on every nonce, top word for word" );
		compare ( mine::software_adapter, 64, L"WARP runs the kHeavyHash kernel and matches the CPU" );
		delete [ ] tops;
		delete hasher;

	}

}

#endif
