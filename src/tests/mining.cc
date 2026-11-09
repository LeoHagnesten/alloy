#ifdef ALLOY_DEBUG

#include "tests/harness.hh"
#include "mine/engine.hh"
#include "mine/gpu/device.hh"
#include "mine/offline.hh"
#include "hash/scrypt.hh"
#include "chain/target.hh"
#include "base/bits.hh"
#include "base/system/clock.hh"
#include "base/memory.hh"
#include "base/system/hardware.hh"
#include "base/text.hh"

namespace tests {

	static constexpr uint32_t genesis_nonce = 2083236893u;

	static bool wait_for_solution ( mine::genesis_work& origin, double limit ) {

		double began = base::seconds ( );

		while ( !origin.solved ( ) && base::seconds ( ) - began < limit )
			base::sleep ( 10 );

		return origin.solved ( );

	}

	static double measure ( mine::engine& engine, double span ) {

		uint64_t before = engine.hashes ( );
		double began = base::seconds ( );

		base::sleep ( static_cast < uint32_t > ( span * 1000.0 ) );

		return static_cast < double > ( engine.hashes ( ) - before ) / ( base::seconds ( ) - began );

	}

	void mining_tests ( ) {

		section ( L"mining engine" );

		mine::engine* engine = new mine::engine;
		mine::benchmark_work bench;
		mine::genesis_work origin;
		mine::limits limits;
		base::processor cpu;

		base::detect ( cpu );
		limits.threads = 2;
		limits.gpu = false;
		engine->start ( &bench, limits );
		base::sleep ( 400 );

		uint64_t total = engine->hashes ( );
		bool idle_lanes = engine->lanes ( ) < 3 || engine->lane_hashes ( 2 ) == 0;

		engine->stop ( );
		check ( total > 100000 && idle_lanes, L"two threads hash the benchmark header and the other lanes stay parked" );

		origin.reset ( genesis_nonce - 3000000 );
		limits.threads = base::min ( cpu.threads, 8u );
		engine->start ( &origin, limits );

		bool solved = wait_for_solution ( origin, 20.0 );

		engine->stop ( );
		check ( solved && origin.nonce ( ) == genesis_nonce && engine->uses_extensions ( ) == ( cpu.sha && cpu.sse41 && cpu.ssse3 ), L"the CPU rediscovers the genesis nonce from 3 million below it" );

		origin.reset ( genesis_nonce - 400000 );
		limits.choice = mine::KERNEL_PORTABLE;
		engine->start ( &origin, limits );
		solved = wait_for_solution ( origin, 20.0 );
		engine->stop ( );
		check ( solved && origin.nonce ( ) == genesis_nonce && !engine->uses_extensions ( ), L"the portable kernel finds it too" );

		limits.choice = mine::KERNEL_AUTO;
		limits.threads = 1;
		engine->start ( &bench, limits );
		base::sleep ( 200 );

		double full = measure ( *engine, 1.0 );

		limits.cpu_percent = 30;
		engine->apply ( limits );
		base::sleep ( 300 );

		double limited = measure ( *engine, 1.5 );
		double ratio = limited / full;
		base::text note;

		note.add ( L"a 30% CPU limit runs one thread at " ).number ( static_cast < uint64_t > ( ratio * 100.0 + 0.5 ) ).add ( L"% of its full speed" );
		check ( ratio > 0.15 && ratio < 0.45, note.get ( ) );

		limits.cpu_percent = 100;
		limits.threads = base::min ( cpu.threads, 3u );
		engine->apply ( limits );
		base::sleep ( 300 );

		uint64_t woken = engine->lane_hashes ( limits.threads - 1 );

		engine->stop ( );
		check ( woken > 0, L"raising the thread count while running wakes parked lanes" );

		chain::header coin;

		chain::litecoin_genesis ( coin );
		origin.reset ( coin.nonce ( ) - 30000, chain::ALGORITHM_SCRYPT );
		limits.threads = cpu.threads;

		double scrypt_began = base::seconds ( );

		engine->start ( &origin, limits );
		solved = wait_for_solution ( origin, 30.0 );

		double scrypt_rate = static_cast < double > ( engine->hashes ( ) ) / ( base::seconds ( ) - scrypt_began );

		engine->stop ( );
		note.clear ( );
		note.add ( L"the CPU rediscovers Litecoin's genesis nonce with scrypt from 30,000 below it at " ).scaled ( scrypt_rate, L"H/s" );
		check ( solved && origin.nonce ( ) == coin.nonce ( ), note.get ( ) );

		auto wait_dataset = [ & ] ( mine::dataset_state wanted, double limit ) {

			double began = base::seconds ( );

			while ( engine->dataset_status ( ) != wanted && base::seconds ( ) - began < limit )
				base::sleep ( 10 );

			return engine->dataset_status ( ) == wanted;

		};

		bench.set_algorithm ( chain::ALGORITHM_RANDOMX );
		limits.threads = cpu.threads;
		limits.dataset_megabytes = 0;
		engine->start ( &bench, limits );

		bool light = wait_dataset ( mine::DATASET_LIGHT, 15.0 );
		double light_build = engine->dataset_seconds ( );

		base::sleep ( 300 );

		double light_rate = measure ( *engine, 2.0 );

		note.clear ( );
		note.add ( L"RandomX light mode builds its cache in " ).decimal ( light_build, 2 ).add ( L" s and hashes at " ).scaled ( light_rate, L"H/s" );
		check ( light && light_rate > 0.0, note.get ( ) );

		limits.dataset_megabytes = 1600;
		engine->apply ( limits );

		bool part_ready = wait_dataset ( mine::DATASET_PART, 60.0 );

		base::sleep ( 300 );

		double part_rate = measure ( *engine, 2.0 );

		note.clear ( );
		note.add ( L"a 1600 MB slice of the dataset builds in " ).decimal ( engine->dataset_seconds ( ), 1 ).add ( L" s and hashes at " ).scaled ( part_rate, L"H/s" ).add ( L", between light and full" );
		check ( part_ready && engine->dataset_megabytes ( ) == 1600 && part_rate > light_rate * 1.3, note.get ( ) );

		limits.dataset_megabytes = 2080;
		engine->apply ( limits );

		bool full_ready = wait_dataset ( mine::DATASET_FULL, 60.0 );
		double full_build = engine->dataset_seconds ( );

		base::sleep ( 300 );

		double full_rate = measure ( *engine, 2.0 );

		engine->stop ( );
		note.clear ( );
		note.add ( L"switching to the full dataset rebuilds it in " ).decimal ( full_build, 1 ).add ( L" s and hashes at " ).scaled ( full_rate, L"H/s" );
		check ( full_ready && full_rate > light_rate * 2.0 && full_rate > part_rate, note.get ( ) );
		engine->release_dataset ( );
		bench.set_algorithm ( chain::ALGORITHM_SHA256D );

		section ( L"gpu kernel" );

		mine::adapter_info adapters [ mine::adapter_room ];
		int32_t adapter_count = mine::list_adapters ( adapters, mine::adapter_room );

		if ( !adapter_count ) {

			check ( true, L"no graphics adapter, so the GPU kernel isn't tested" );
			delete engine;

			return;

		}

		mine::gpu_device* device = new mine::gpu_device;
		bool opened = device->open ( 0 );

		note.clear ( );
		note.add ( L"the compute kernel loads on " ).add ( device->name ( ) );
		check ( opened, note.get ( ) );

		if ( !opened ) {

			delete device;
			delete engine;

			return;

		}

		chain::header source;
		chain::header_hasher hasher;
		uint32_t* tops = new uint32_t [ 4096 ];
		uint8_t digest [ 32 ];

		chain::genesis ( source );

		bool same = device->sample ( chain::ALGORITHM_SHA256D, source, genesis_nonce - 2048, 4096, tops );
		hasher.prepare ( source, hash::sha256_block );

		for ( uint32_t i = 0; i < 4096 && same; i++ ) {

			hasher.hash ( genesis_nonce - 2048 + i, digest );
			same = tops [ i ] == base::load_little ( digest + 28 );

		}

		check ( same && tops [ 2048 ] == 0, L"the GPU's top hash word matches the CPU for 4096 nonces around the genesis nonce" );
		delete [ ] tops;

		uint32_t found [ 32 ];
		uint32_t found_count = 0;
		bool searched = device->search ( chain::ALGORITHM_SHA256D, source, genesis_nonce - ( 1u << 20 ), 1u << 21, 0, found, 32, found_count );
		bool hit = false;

		for ( uint32_t i = 0; i < found_count; i++ )
			hit = hit || found [ i ] == genesis_nonce;

		check ( searched && hit, L"a GPU search over 2 million nonces reports the genesis nonce" );

		uint32_t room = device->capacity ( chain::ALGORITHM_SCRYPT );
		double gpu_scrypt_rate = 0.0;

		note.clear ( );
		note.add ( L"the three scrypt kernels load with room for " ).grouped ( room ).add ( L" hashes at 64 KB each" );
		check ( room >= 1024, note.get ( ) );

		if ( room >= 1024 ) {

			uint32_t* scrypt_tops = new uint32_t [ 1024 ];
			void* scratch = base::allocate ( hash::scrypt_scratch_bytes );

			chain::litecoin_genesis ( coin );

			uint32_t coin_nonce = coin.nonce ( );
			bool agree = device->sample ( chain::ALGORITHM_SCRYPT, coin, coin_nonce - 512, 1024, scrypt_tops );

			for ( uint32_t i = 0; i < 1024 && agree; i++ ) {

				coin.set_nonce ( coin_nonce - 512 + i );
				hash::scrypt_header ( coin.bytes, digest, scratch );
				agree = scrypt_tops [ i ] == base::load_little ( digest + 28 );

			}

			check ( agree && scrypt_tops [ 512 ] == 0x0000050C, L"the GPU's scrypt matches the CPU for 1024 nonces around Litecoin's genesis nonce" );

			uint8_t coin_target [ 32 ];

			chain::target_of ( 0x1E0FFFF0, coin_target );
			found_count = 0;
			searched = device->search ( chain::ALGORITHM_SCRYPT, coin, coin_nonce - room / 2, room, base::load_little ( coin_target + 28 ), found, 32, found_count );
			hit = false;

			for ( uint32_t i = 0; i < found_count; i++ )
				hit = hit || found [ i ] == coin_nonce;

			double began = base::seconds ( );
			uint32_t rounds = 0;

			while ( base::seconds ( ) - began < 1.0 && device->search ( chain::ALGORITHM_SCRYPT, coin, rounds * room, room, 0, found, 32, found_count ) )
				rounds++;

			gpu_scrypt_rate = rounds * static_cast < double > ( room ) / ( base::seconds ( ) - began );
			note.clear ( );
			note.add ( L"a GPU scrypt search finds Litecoin's genesis nonce and runs at " ).scaled ( gpu_scrypt_rate, L"H/s" );
			check ( searched && hit, note.get ( ) );
			base::release ( scratch );
			delete [ ] scrypt_tops;

		}

		delete device;

		origin.reset ( genesis_nonce - 200000000 );
		limits.threads = 0;
		limits.gpu = true;
		limits.gpu_intensity = 22;
		engine->start ( &origin, limits );
		solved = wait_for_solution ( origin, 30.0 );

		double ran = engine->gpu_hashes ( );
		uint32_t faults = engine->gpu_errors ( );

		engine->stop ( );
		note.clear ( );
		note.add ( L"the engine finds the genesis nonce on the GPU alone after " ).decimal ( ran / 1e6, 0 ).add ( L" M hashes" );
		check ( solved && origin.nonce ( ) == genesis_nonce && faults == 0, note.get ( ) );

		if ( room >= 1024 ) {

			chain::litecoin_genesis ( coin );
			origin.reset ( coin.nonce ( ) - room * 6, chain::ALGORITHM_SCRYPT );
			engine->start ( &origin, limits );
			solved = wait_for_solution ( origin, 30.0 );
			faults = engine->gpu_errors ( );
			engine->stop ( );
			check ( solved && origin.nonce ( ) == coin.nonce ( ) && faults == 0, L"the engine finds Litecoin's genesis nonce with scrypt on the GPU alone, every result checked on the CPU" );

		}

		limits.gpu_percent = 100;
		engine->start ( &bench, limits );
		base::sleep ( 500 );
		full = measure ( *engine, 1.0 );
		limits.gpu_percent = 40;
		engine->apply ( limits );
		base::sleep ( 500 );
		limited = measure ( *engine, 1.5 );
		engine->stop ( );
		ratio = limited / full;
		note.clear ( );
		note.add ( L"the GPU runs at " ).scaled ( full, L"H/s" ).add ( L" and a 40% limit holds it to " ).number ( static_cast < uint64_t > ( ratio * 100.0 + 0.5 ) ).put ( L'%' );
		check ( full > 1e8 && ratio > 0.25 && ratio < 0.55, note.get ( ) );

		mine::gpu_device* card = new mine::gpu_device;
		float celsius = 0.0f;
		bool warm = card->open ( 0 ) && card->temperature ( celsius );

		note.clear ( );
		note.add ( L"the driver reports the card's temperature, " ).decimal ( celsius, 0 ).add ( L" degrees" );
		check ( warm && celsius > 5.0f && celsius < 120.0f, note.get ( ) );
		delete card;

		mine::gpu_device* software = new mine::gpu_device;
		bool warp = software->open ( mine::software_adapter );
		uint32_t* software_tops = new uint32_t [ 256 ];
		bool portable = warp && software->sample ( chain::ALGORITHM_SHA256D, source, genesis_nonce - 128, 256, software_tops );

		for ( uint32_t i = 0; i < 256 && portable; i++ ) {

			hasher.prepare ( source, hash::sha256_block );
			hasher.hash ( genesis_nonce - 128 + i, digest );
			portable = software_tops [ i ] == base::load_little ( digest + 28 );

		}

		check ( portable, L"Microsoft's WARP renderer runs the SHA-256d kernel and matches the CPU on 256 nonces" );

		if ( room >= 1024 && warp ) {

			void* scratch = base::allocate ( hash::scrypt_scratch_bytes );
			bool agree = software->sample ( chain::ALGORITHM_SCRYPT, coin, coin.nonce ( ) - 16, 32, software_tops );
			chain::header probe = coin;

			for ( uint32_t i = 0; i < 32 && agree; i++ ) {

				probe.set_nonce ( coin.nonce ( ) - 16 + i );
				hash::scrypt_header ( probe.bytes, digest, scratch );
				agree = software_tops [ i ] == base::load_little ( digest + 28 );

			}

			check ( agree, L"WARP runs the three scrypt kernels and matches the CPU on 32 nonces" );
			base::release ( scratch );

		}

		delete [ ] software_tops;
		delete software;

		origin.reset ( genesis_nonce - 120000000 );
		limits.gpu_percent = 100;
		limits.adapter_count = 2;
		limits.adapters [ 0 ] = 0;
		limits.adapters [ 1 ] = mine::software_adapter;
		limits.gpu_intensity = 20;
		engine->start ( &origin, limits );
		solved = wait_for_solution ( origin, 40.0 );

		uint64_t first_card = engine->gpu_hashes ( 0 );
		uint64_t second_card = engine->gpu_hashes ( 1 );

		faults = engine->gpu_errors ( );
		engine->stop ( );
		note.clear ( );
		note.add ( L"two devices mine together and find the genesis nonce: " ).decimal ( first_card / 1e6, 0 ).add ( L" M hashes on the card, " ).decimal ( second_card / 1e6, 1 ).add ( L" M on WARP" );
		check ( solved && origin.nonce ( ) == genesis_nonce && first_card > 0 && second_card > 0 && faults == 0, note.get ( ) );
		limits.adapter_count = 2;
		limits.adapters [ 0 ] = 0;
		limits.adapters [ 1 ] = mine::software_adapter;
		limits.cards [ 1 ].custom = true;
		limits.cards [ 1 ].on = false;
		engine->start ( &bench, limits );
		base::sleep ( 1500 );

		uint64_t kept_on = engine->gpu_hashes ( 0 );
		uint64_t kept_off = engine->gpu_hashes ( 1 );

		engine->stop ( );
		check ( kept_on > 0 && kept_off == 0, L"a card switched off on its own does no work while the other keeps mining" );
		limits.cards [ 1 ] = mine::card_setting { };
		limits.adapter_count = 1;
		limits.adapters [ 0 ] = 0;
		limits.gpu_intensity = 22;
		engine->start ( &bench, limits );
		base::sleep ( 800 );
		engine->break_gpu ( );

		double recover_began = base::seconds ( );
		bool restarted = false;

		while ( base::seconds ( ) - recover_began < 12.0 && !restarted ) {

			base::sleep ( 100 );
			restarted = engine->gpu_restarts ( 0 ) == 1 && engine->gpu_status ( 0 ) == mine::GPU_RUNNING;

		}

		uint64_t before_more = engine->gpu_hashes ( 0 );

		base::sleep ( 600 );

		uint64_t after_more = engine->gpu_hashes ( 0 );

		engine->stop ( );
		note.clear ( );
		note.add ( L"after the card stops responding, the engine rebuilds its device in " ).decimal ( base::seconds ( ) - recover_began - 0.6, 1 ).add ( L" s and keeps hashing" );
		check ( restarted && after_more > before_more, note.get ( ) );

		delete engine;

	}

}

#endif
