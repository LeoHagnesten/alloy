#ifdef ALLOY_DEBUG

#include "tests/harness.hh"
#include "hash/ethash.hh"
#include "mine/gpu/device.hh"
#include "pool/job.hh"
#include "base/formats/json.hh"
#include "base/bits.hh"
#include "base/system/clock.hh"
#include "base/memory.hh"
#include "base/strings.hh"
#include "base/text.hh"

namespace tests {

	void ethash_tests ( ) {

		section ( L"ethash" );

		check ( hash::ethash_cache_size ( 0 ) == 16776896ull && hash::ethash_cache_size ( 1 ) == 16907456ull && hash::ethash_dataset_size ( 0 ) == 1073739904ull && hash::ethash_dataset_size ( 1 ) == 1082130304ull, L"cache and dataset sizes for the first epochs match the reference tables" );

		uint8_t seed [ 32 ];
		uint8_t later [ 32 ];

		hash::ethash_seed ( 1, seed );
		hash::ethash_seed ( 428, later );
		check ( hex_is ( seed, 32, "290decd9548b62a8d60345a988386fc84ba6bc95484008f6362f93160ef3e563" ) && hash::ethash_seed_epoch ( later ) == 428, L"the epoch 1 seed is Keccak-256 of 32 zero bytes, and a seed maps back to its epoch" );

		{

			static const char line [ ] = "{\"id\":null,\"method\":\"mining.notify\",\"params\":[\"cc8d\",\"1680f1719a535586a494458ee42fdfd99b5e6c883e9b82aa86e84ca7dd7322af\",\"0192b1fda6fa0177614c3ef9cbee2f1a8116ccf16e9b8c18c9812bd46a3e4089\",true],\"height\":25700045,\"algo\":\"etchash\"}";
			base::json* message = new base::json;
			pool::ethash_job job;
			bool parsed = message->parse ( line, sizeof ( line ) - 1 ) && pool::parse_ethash_notify ( *message, message->member ( 0, "params" ), job );

			check ( parsed && base::same_text ( job.id, "cc8d" ) && job.clean && base::same_text ( job.algo, "etchash" ) && job.header [ 0 ] == 0x01 && pool::ethash_dataset_epoch ( job.seed, true ) == 428 && pool::ethash_dataset_epoch ( job.seed, false ) == 856, L"a real 2Miners Ethereum Classic job reads back with its seed in Etchash epoch 428, which plain Ethash would call 856" );
			check ( pool::etchash_by_height ( 856, 25730000 ) == 1 && pool::etchash_by_height ( 847, 25426829 ) == 0 && pool::etchash_by_height ( 856, 25700045 ) == -1, L"a job's height tells an Ethereum Classic seed from an EthereumPoW one where the two rules differ, and leaves the rest to the chosen coin" );
			delete message;

		}
		hash::ethash_cache* cache = new hash::ethash_cache;
		uint8_t header [ 32 ];
		uint8_t mix [ 32 ];
		uint8_t result [ 32 ];
		double began = base::seconds ( );

		hash::ethash_seed ( 0, seed );
		base::read_hex ( "372eca2454ead349c3df0ab5d00b0b706b23e49d469387db91811cee0358fc6d", header, 32 );

		bool built = cache->build ( 0, seed );
		double seconds = base::seconds ( ) - began;

		if ( built )
			cache->light ( header, 0x495732e0ed7a801cull, mix, result );

		base::text note;

		note.add ( L"light Ethash of block 22 gives its mix digest and a result under its difficulty, the 16 MB cache built in " ).decimal ( seconds, 2 ).add ( L" s" );
		check ( built && hex_is ( mix, 32, "2f74cdeb198af0b9abe65d22d372e22fb2d474371774a9583c1cc427a07939f5" ) && result [ 0 ] == 0 && result [ 1 ] == 0, note.get ( ) );

		mine::adapter_info adapters [ mine::adapter_room ];

		if ( !built || !mine::list_adapters ( adapters, mine::adapter_room ) ) {

			check ( true, L"no graphics adapter, so the Ethash shaders aren't tested" );
			delete cache;

			return;

		}

		mine::gpu_device* device = new mine::gpu_device;
		volatile uint32_t progress = 0;

		began = base::seconds ( );

		bool dag = device->open ( 0 ) && device->build_dag ( *cache, &progress, nullptr ) && device->dag_ready ( 0 );

		seconds = base::seconds ( ) - began;

		static const uint32_t probes [ ] = { 0, 1, 2, 63, 4096, 1000001, 8388607, 8388608, 16777185 };
		int32_t matching = 0;

		for ( uint32_t index : probes ) {

			uint32_t from_card [ 16 ] = { };
			uint32_t from_cpu [ 16 ] = { };

			cache->item ( index, from_cpu );

			if ( dag && device->dag_item ( index, from_card ) && base::same ( from_card, from_cpu, 64 ) )
				matching++;

		}

		note.clear ( );
		note.add ( L"the graphics card builds the 1 GB epoch 0 DAG in " ).decimal ( seconds, 2 ).add ( L" s and every probed item matches the CPU (" ).number ( static_cast < uint64_t > ( matching ) ).add ( L" of " ).number ( static_cast < uint64_t > ( base::count_of ( probes ) ) ).put ( L')' );
		check ( dag && progress == 1000 && matching == base::count_of ( probes ), note.get ( ) );

		uint64_t nonce = 0x495732e0ed7a801cull;
		uint64_t threshold = base::load_big64 ( result );
		int32_t slot = 0;
		uint32_t found [ 32 ] = { };
		uint32_t found_count = 0;
		bool hit = false;

		if ( dag && device->submit_ethash ( header, static_cast < uint32_t > ( nonce >> 32 ), static_cast < uint32_t > ( nonce ) - 2000, 4096, threshold, slot ) && device->collect ( slot, found, 32, found_count ) ) {

			for ( uint32_t i = 0; i < found_count; i++ )
				hit = hit || found [ i ] == static_cast < uint32_t > ( nonce );

		}

		check ( hit, L"the graphics card's Ethash search finds block 22's nonce among 4096 tries" );

		uint32_t tops [ 256 ] = { };
		int32_t agreeing = 0;

		if ( dag && device->sample_ethash ( header, 0x12345678, 1000, 256, tops ) ) {

			for ( uint32_t i = 0; i < 256; i++ ) {

				cache->light ( header, 0x1234567800000000ull + 1000 + i, mix, result );
				agreeing += base::load_big ( result ) == tops [ i ] ? 1 : 0;

			}

		}

		note.clear ( );
		note.add ( L"the graphics card's Ethash matches CPU light hashing on every sampled nonce (" ).number ( static_cast < uint64_t > ( agreeing ) ).add ( L" of 256)" );
		check ( agreeing == 256, note.get ( ) );

		if ( adapters [ 0 ].memory < ( 6ull << 30 ) ) {

			check ( true, L"the graphics card has under 6 GB, so the Ethereum Classic sized DAG isn't tested" );
			delete device;
			delete cache;

			return;

		}

		uint8_t classic [ 32 ];

		hash::ethash_seed ( 856, classic );
		began = base::seconds ( );
		built = cache->build ( 428, classic );
		dag = built && device->build_dag ( *cache, &progress, nullptr ) && device->dag_ready ( 428 );
		seconds = base::seconds ( ) - began;

		uint32_t rows = static_cast < uint32_t > ( cache->dataset_bytes ( ) / 128 );
		uint32_t split = static_cast < uint32_t > ( base::min ( base::max ( adapters [ 0 ].memory / 4, 128ull << 20 ), 2048ull << 20 ) / 128 );
		uint32_t edges [ ] = { 0, split * 2 - 1, split * 2, split * 4 - 1, split * 4, rows * 2 - 1 };
		int32_t edge_matches = 0;

		for ( uint32_t index : edges ) {

			uint32_t from_card [ 16 ] = { };
			uint32_t from_cpu [ 16 ] = { };

			cache->item ( index % ( rows * 2 ), from_cpu );

			if ( dag && device->dag_item ( index % ( rows * 2 ), from_card ) && base::same ( from_card, from_cpu, 64 ) )
				edge_matches++;

		}

		agreeing = 0;

		if ( dag && device->sample_ethash ( header, 0x0BADCAFE, 0, 256, tops ) ) {

			for ( uint32_t i = 0; i < 256; i++ ) {

				cache->light ( header, 0x0BADCAFE00000000ull + i, mix, result );
				agreeing += base::load_big ( result ) == tops [ i ] ? 1 : 0;

			}

		}

		note.clear ( );
		note.add ( L"at Ethereum Classic's epoch 428 the " ).decimal ( static_cast < double > ( cache->dataset_bytes ( ) ) / 1073741824.0, 2 ).add ( L" GB DAG builds in " ).decimal ( seconds, 1 ).add ( L" s across its buffers, items on both sides of every split match, and hashing agrees with the CPU (" ).number ( static_cast < uint64_t > ( agreeing ) ).add ( L" of 256)" );
		check ( dag && edge_matches == base::count_of ( edges ) && agreeing == 256, note.get ( ) );		delete device;
		delete cache;

	}

}

#endif
