#include "mine/engine.hh"
#include "mine/gpu.hh"
#include "chain/target.hh"
#include "base/bits.hh"
#include "base/clock.hh"
#include "base/strings.hh"
#include "base/system.hh"

namespace mine {

	static constexpr uint64_t lane_chunk = 1ull << 18;
	static constexpr uint64_t batch = 4096;

	bool engine::start ( feeder* feed, const limits& settings ) {

		if ( active )
			stop ( );

		base::processor cpu;

		base::detect ( cpu );

		bool extensions = cpu.sha && cpu.sse41 && cpu.ssse3;

		source = feed;
		compress = extensions && settings.choice != KERNEL_PORTABLE ? hash::sha256_block_extensions : hash::sha256_block;
		lane_count = base::min ( cpu.threads, max_lanes );
		quit = 0;
		halt = 0;
		hungry = 0;
		paused = 0;
		have_unit = false;
		next_index = 0;
		cursor = 0;
		best_top = ~0ull;
		gpu_done = 0;
		gpu_faults = 0;
		gpu_phase = GPU_OFF;
		gpu_label [ 0 ] = 0;
		gpu_adapter = settings.adapter;
		lane_list = new lane [ lane_count ];
		active = true;
		apply ( settings );

		for ( uint32_t i = 0; i < lane_count; i++ ) {

			lane_list [ i ].owner = this;
			lane_list [ i ].index = i;
			lane_list [ i ].thread = base::start_thread ( &lane_main, &lane_list [ i ] );

		}

		return true;

	}

	void engine::stop ( ) {

		if ( !active )
			return;

		base::atomic_store ( quit, 1u );

		for ( uint32_t i = 0; i < lane_count; i++ )
			base::join_thread ( lane_list [ i ].thread );

		base::join_thread ( gpu_thread );
		gpu_thread = nullptr;
		delete [ ] lane_list;
		lane_list = nullptr;
		lane_count = 0;
		active = false;
		base::atomic_store ( gpu_phase, static_cast < uint32_t > ( GPU_OFF ) );

	}

	void engine::apply ( const limits& settings ) {

		base::atomic_store ( wanted_threads, base::min ( settings.threads, lane_count ) );
		base::atomic_store ( cpu_share, base::clamp ( settings.cpu_percent, 5u, 100u ) );
		base::atomic_store ( priority, static_cast < uint32_t > ( settings.priority ) );
		base::atomic_store ( gpu_share, base::clamp ( settings.gpu_percent, 5u, 100u ) );
		base::atomic_store ( gpu_power, base::clamp ( settings.gpu_intensity, 16u, 26u ) );
		base::atomic_store ( gpu_wanted, settings.gpu ? 1u : 0u );

		if ( active && settings.gpu && !gpu_thread ) {

			base::atomic_store ( gpu_phase, static_cast < uint32_t > ( GPU_STARTING ) );
			gpu_thread = base::start_thread ( &gpu_main, this );

		}

	}

	void engine::set_paused ( bool value ) {

		base::atomic_store ( paused, value ? 1u : 0u );

	}

	uint64_t engine::hashes ( ) const {

		uint64_t sum = gpu_hashes ( );

		for ( uint32_t i = 0; i < lane_count; i++ )
			sum += base::atomic_load ( lane_list [ i ].done );

		return sum;

	}

	uint64_t engine::lane_hashes ( uint32_t lane ) const {

		return lane < lane_count ? base::atomic_load ( lane_list [ lane ].done ) : 0;

	}

	unsigned long engine::lane_main ( void* context ) {

		lane* self = static_cast < lane* > ( context );

		self->owner->run_lane ( *self );

		return 0;

	}

	unsigned long engine::gpu_main ( void* context ) {

		static_cast < engine* > ( context )->run_gpu ( );

		return 0;

	}

	bool engine::claim ( uint64_t chunk, unit& work, uint64_t& from, uint64_t& to ) {

		base::guard hold ( claim_lock );

		uint32_t job = source->job ( );

		if ( !have_unit || current.job != job || cursor >= current.end ) {

			if ( have_unit && current.job != job )
				next_index = 0;

			if ( !source->fill ( next_index, current ) ) {

				have_unit = false;
				base::atomic_store ( hungry, 1u );

				return false;

			}

			next_index++;
			have_unit = true;
			cursor = current.first;
			base::atomic_store ( hungry, 0u );

		}

		work = current;
		from = cursor;
		to = base::min ( cursor + chunk, current.end );
		cursor = to;

		return true;

	}

	bool engine::stale ( const unit& work ) const {

		return source->job ( ) != work.job;

	}

	void engine::report ( const unit& work, uint32_t nonce, const uint8_t* digest ) {

		if ( !source->found ( work, nonce, digest ) )
			base::atomic_store ( halt, 1u );

	}

	void engine::rest ( double& debt, double busy, uint32_t percent ) {

		if ( percent >= 100 ) {

			debt = 0.0;

			return;

		}

		debt += busy * static_cast < double > ( 100 - percent ) / static_cast < double > ( percent );

		if ( debt < 0.015 )
			return;

		double before = base::seconds ( );

		base::sleep ( static_cast < uint32_t > ( base::min ( debt, 0.5 ) * 1000.0 ) );
		debt = base::max ( debt - ( base::seconds ( ) - before ), -0.05 );

	}

	void engine::run_lane ( lane& self ) {

		chain::header_hasher hasher;
		uint8_t digest [ 32 ];
		uint32_t applied = ~0u;
		double debt = 0.0;

		while ( !base::atomic_load ( quit ) ) {

			if ( self.index >= base::atomic_load ( wanted_threads ) || base::atomic_load ( paused ) || base::atomic_load ( halt ) ) {

				base::sleep ( 30 );

				continue;

			}

			uint32_t level = base::atomic_load ( priority );

			if ( level != applied ) {

				base::set_thread_priority ( static_cast < base::thread_priority > ( level ) );
				applied = level;

			}

			unit work;
			uint64_t from = 0;
			uint64_t to = 0;

			if ( !claim ( lane_chunk, work, from, to ) ) {

				base::sleep ( 20 );

				continue;

			}

			hasher.prepare ( work.header, compress );

			uint64_t limit = chain::top_bits ( work.target );
			uint64_t best_seen = best ( );
			uint64_t nonce = from;

			while ( nonce < to ) {

				uint32_t share = base::atomic_load ( cpu_share );
				double started = share < 100 ? base::seconds ( ) : 0.0;
				uint64_t stop_at = base::min ( nonce + batch, to );
				uint64_t count = stop_at - nonce;

				for ( ; nonce < stop_at; nonce++ ) {

					hasher.hash ( static_cast < uint32_t > ( nonce ), digest );

					uint64_t top = chain::top_bits ( digest );

					if ( top < best_seen ) {

						best_seen = top;
						base::atomic_lower ( best_top, top );

					}

					if ( top <= limit && chain::meets ( digest, work.target ) )
						report ( work, static_cast < uint32_t > ( nonce ), digest );

				}

				base::atomic_add ( self.done, count );

				if ( share < 100 )
					rest ( debt, base::seconds ( ) - started, share );

				if ( base::atomic_load ( quit ) || base::atomic_load ( halt ) || stale ( work ) )
					break;

			}

		}

	}

	void engine::run_gpu ( ) {

		gpu_device device;
		chain::header_hasher checker;
		uint8_t digest [ 32 ];
		uint32_t found [ 32 ];
		double debt = 0.0;

		base::set_thread_priority ( base::PRIORITY_HIGH );

		if ( !device.open ( base::atomic_load ( gpu_adapter ) ) ) {

			base::copy_text ( gpu_label, base::count_of ( gpu_label ), device.name ( ) );
			base::atomic_store ( gpu_phase, static_cast < uint32_t > ( GPU_FAILED ) );

			return;

		}

		base::copy_text ( gpu_label, base::count_of ( gpu_label ), device.name ( ) );
		base::atomic_store ( gpu_phase, static_cast < uint32_t > ( GPU_RUNNING ) );

		while ( !base::atomic_load ( quit ) ) {

			if ( !base::atomic_load ( gpu_wanted ) || base::atomic_load ( paused ) || base::atomic_load ( halt ) ) {

				base::sleep ( 30 );

				continue;

			}

			unit work;
			uint64_t from = 0;
			uint64_t to = 0;

			if ( !claim ( 1ull << base::atomic_load ( gpu_power ), work, from, to ) ) {

				base::sleep ( 20 );

				continue;

			}

			uint32_t threshold = base::load_little ( work.target + 28 );
			uint32_t count = static_cast < uint32_t > ( ( to - from + 255 ) & ~255ull );
			uint32_t found_count = 0;
			double started = base::seconds ( );

			if ( !device.search ( work.header, static_cast < uint32_t > ( from ), count, threshold, found, base::count_of ( found ), found_count ) ) {

				base::atomic_store ( gpu_phase, static_cast < uint32_t > ( GPU_FAILED ) );

				break;

			}

			if ( found_count )
				checker.prepare ( work.header, compress );

			for ( uint32_t i = 0; i < found_count; i++ ) {

				checker.hash ( found [ i ], digest );

				if ( base::load_little ( digest + 28 ) > threshold ) {

					base::atomic_add ( gpu_faults, 1u );

					continue;

				}

				base::atomic_lower ( best_top, chain::top_bits ( digest ) );

				if ( chain::meets ( digest, work.target ) )
					report ( work, found [ i ], digest );

			}

			base::atomic_add ( gpu_done, to - from );
			rest ( debt, base::seconds ( ) - started, base::atomic_load ( gpu_share ) );

		}

		device.close ( );

	}

}
