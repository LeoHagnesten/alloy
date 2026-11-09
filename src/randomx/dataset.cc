#include "randomx/dataset.hh"
#include "base/system/clock.hh"
#include "base/memory.hh"
#include "base/system/threads.hh"

namespace randomx {

	static constexpr uint64_t mix_start = 6364136223846793005ull;
	static constexpr uint64_t mix_constants [ 7 ] = { 9298411001130361340ull, 12065312585734608966ull, 9306329213124626780ull, 5281919268842080866ull, 10536153434571861004ull, 3398623926847679864ull, 9549104520008361294ull };
	static constexpr uint64_t cache_lines = cache_bytes / 64;

	cache::~cache ( ) {

		base::release_pages ( blocks );
		delete [ ] programs;

	}

	bool cache::build ( const void* seed, size_t size, uint8_t which ) {

		const variant& rules = variant_of ( which );

		if ( !blocks )
			blocks = static_cast < hash::argon2_block* > ( base::allocate_large ( cache_bytes, large ) );

		if ( !programs )
			programs = new scalar_program [ cache_accesses ];

		if ( !blocks )
			return false;

		size = base::min ( size, static_cast < size_t > ( key_room ) );
		key_size = size;
		base::zero ( key, sizeof ( key ) );
		base::copy ( key, seed, size );
		built_kind = rules.id;

		hash::argon2_input input;

		input.password = seed;
		input.password_size = size;
		input.salt = rules.salt;
		input.salt_size = rules.salt_size;
		hash::argon2d_fill ( blocks, cache_blocks, rules.passes, rules.lanes, input );

		seed_generator random ( seed, size );

		for ( int32_t i = 0; i < cache_accesses; i++ )
			generate_superscalar ( programs [ i ], random, rules.latency );

		native.build ( programs, cache_accesses );

		return true;

	}

	bool cache::matches ( const void* seed, size_t size, uint8_t which ) const {

		return blocks && size == key_size && which == built_kind && base::same ( seed, key, size );

	}

	void cache::item ( uint64_t number, uint64_t* out ) const {

		const uint64_t* memory = words ( );
		uint64_t r [ 8 ];
		uint64_t index = number;

		r [ 0 ] = ( number + 1 ) * mix_start;

		for ( int32_t i = 0; i < 7; i++ )
			r [ i + 1 ] = r [ 0 ] ^ mix_constants [ i ];

		bool fast = native.ready ( ) && !interpret;

		for ( int32_t i = 0; i < cache_accesses; i++ ) {

			const uint64_t* line = memory + ( index & ( cache_lines - 1 ) ) * 8;

			if ( fast )
				native.program ( i ) ( r );
			else
				run_superscalar ( r, programs [ i ] );

			for ( int32_t k = 0; k < 8; k++ )
				r [ k ] ^= line [ k ];

			index = r [ programs [ i ].address_register ];

		}

		base::copy ( out, r, 64 );

	}

	struct fill_job {

		const cache* source;
		uint8_t* memory;
		uint64_t first;
		uint64_t last;
		volatile uint64_t* done;
		volatile uint32_t* cancel;

	};

	static unsigned long fill_main ( void* context ) {

		fill_job& job = *static_cast < fill_job* > ( context );

		base::set_thread_priority ( base::PRIORITY_LOW );

		for ( uint64_t at = job.first; at < job.last; at++ ) {

			job.source->item ( at, reinterpret_cast < uint64_t* > ( job.memory + at * 64 ) );

			if ( ( at & 4095 ) == 4095 ) {

				base::atomic_add ( *job.done, 4096ull );

				if ( job.cancel && base::atomic_load ( *job.cancel ) )
					return 0;

			}

		}

		return 0;

	}

	uint64_t items_for ( uint32_t megabytes ) {

		if ( megabytes >= dataset_megabytes )
			return dataset_items;

		return static_cast < uint64_t > ( megabytes ) * 16384;

	}

	dataset::~dataset ( ) {

		release ( );

	}

	void dataset::release ( ) {

		base::release_pages ( memory );
		memory = nullptr;
		held = 0;

	}

	bool dataset::build ( const cache& source, uint64_t items, uint32_t threads, volatile uint32_t* progress, volatile uint32_t* cancel ) {

		items = base::min ( items, dataset_items );

		if ( memory && items != held )
			release ( );

		if ( !memory && items )
			memory = static_cast < uint8_t* > ( base::allocate_large ( static_cast < size_t > ( items * 64 ), large ) );

		if ( !memory || !source.ready ( ) )
			return false;

		held = items;

		threads = base::clamp ( threads, 1u, 64u );

		fill_job jobs [ 64 ];
		void* handles [ 64 ] = { };
		volatile uint64_t done = 0;
		uint64_t share = items / threads;

		for ( uint32_t i = 0; i < threads; i++ ) {

			jobs [ i ] = fill_job { &source, memory, share * i, i + 1 == threads ? items : share * ( i + 1 ), &done, cancel };
			handles [ i ] = base::start_thread ( &fill_main, &jobs [ i ] );

		}

		bool waiting = true;

		while ( waiting ) {

			waiting = false;

			for ( uint32_t i = 0; i < threads; i++ ) {

				if ( handles [ i ] && !base::thread_finished ( handles [ i ] ) )
					waiting = true;

			}

			if ( progress )
				base::atomic_store ( *progress, static_cast < uint32_t > ( base::min ( base::atomic_load ( done ) * 1000 / items, 999ull ) ) );

			if ( waiting )
				base::sleep ( 20 );

		}

		for ( uint32_t i = 0; i < threads; i++ )
			base::join_thread ( handles [ i ] );

		if ( cancel && base::atomic_load ( *cancel ) )
			return false;

		if ( progress )
			base::atomic_store ( *progress, 1000u );

		return true;

	}

}
