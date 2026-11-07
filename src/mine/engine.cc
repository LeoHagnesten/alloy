#include "mine/engine.hh"
#include "mine/gpu.hh"
#include "randomx/machine.hh"
#include "hash/aes.hh"
#include "hash/scrypt.hh"
#include "chain/target.hh"
#include "base/bits.hh"
#include "base/clock.hh"
#include "base/memory.hh"
#include "base/strings.hh"
#include "base/system.hh"

namespace mine {

	struct engine::tools {

		chain::header_hasher hasher;
		void* scratch = nullptr;
		randomx::machine* machine = nullptr;
		uint32_t generation = ~0u;

		~tools ( ) {

			base::release ( scratch );
			delete machine;

		}

	};

	static uint64_t lane_chunk ( uint8_t algorithm, bool full ) {

		if ( algorithm == chain::ALGORITHM_SCRYPT )
			return 1024;

		if ( algorithm == chain::ALGORITHM_RANDOMX )
			return full ? 64 : 8;

		return 1ull << 18;

	}

	engine::~engine ( ) {

		stop ( );
		release_dataset ( );

	}

	bool engine::start ( feeder* feed, const limits& settings ) {

		if ( active )
			stop ( );

		base::processor cpu;

		base::detect ( cpu );
		hash::aes_prepare ( cpu.aes );

		bool extensions = cpu.sha && cpu.sse41 && cpu.ssse3;

		source = feed;
		compress = extensions && settings.choice != KERNEL_PORTABLE ? hash::sha256_block_extensions : hash::sha256_block;
		lane_count = base::min ( cpu.threads, max_lanes );
		level3 = cpu.level3;

		if ( !rx_ready )
			rx_lanes = lane_count;

		quit = 0;
		halt = 0;
		hungry = 0;
		paused = 0;
		have_unit = false;
		next_index = 0;
		cursor = 0;
		best_top = ~0ull;
		seen_algorithm = settings.algorithm;
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

		if ( base::atomic_load ( rx_state ) == DATASET_BUILDING )
			base::atomic_store ( rx_state, static_cast < uint32_t > ( DATASET_NONE ) );

	}

	void engine::apply ( const limits& settings ) {

		base::atomic_store ( wanted_threads, base::min ( settings.threads, lane_count ) );
		base::atomic_store ( cpu_share, base::clamp ( settings.cpu_percent, 5u, 100u ) );
		base::atomic_store ( priority, static_cast < uint32_t > ( settings.priority ) );
		base::atomic_store ( gpu_share, base::clamp ( settings.gpu_percent, 5u, 100u ) );
		base::atomic_store ( gpu_power, base::clamp ( settings.gpu_intensity, 16u, 26u ) );
		base::atomic_store ( gpu_wanted, settings.gpu ? 1u : 0u );
		base::atomic_store ( rx_full, settings.full_dataset ? 1u : 0u );

		if ( active && settings.gpu && !gpu_thread ) {

			base::atomic_store ( gpu_phase, static_cast < uint32_t > ( GPU_STARTING ) );
			gpu_thread = base::start_thread ( &gpu_main, this );

		}

	}

	void engine::set_paused ( bool value ) {

		base::atomic_store ( paused, value ? 1u : 0u );

	}

	void engine::release_dataset ( ) {

		if ( active )
			return;

		delete rx_dataset;
		delete rx_cache;
		rx_dataset = nullptr;
		rx_cache = nullptr;
		rx_ready = 0;
		rx_state = DATASET_NONE;
		rx_progress = 0;

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

	bool engine::claim ( bool gpu, const uint32_t* gpu_rooms, unit& work, uint64_t& from, uint64_t& to ) {

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
			base::atomic_store ( seen_algorithm, static_cast < uint32_t > ( current.algorithm ) );

		}

		uint64_t chunk = gpu ? gpu_rooms [ current.algorithm % chain::ALGORITHM_COUNT ] : lane_chunk ( current.algorithm, base::atomic_load ( rx_full ) != 0 );

		if ( !chunk )
			return false;

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

	template < typename hasher >
	void engine::sweep ( lane& self, const unit& work, uint64_t from, uint64_t to, uint64_t step, double& debt, hasher& hash_one ) {

		uint8_t digest [ 32 ];
		uint64_t limit = chain::top_bits ( work.target );
		uint64_t best_seen = best ( );
		uint64_t nonce = from;

		while ( nonce < to ) {

			uint32_t share = base::atomic_load ( cpu_share );
			double started = share < 100 ? base::seconds ( ) : 0.0;
			uint64_t stop_at = base::min ( nonce + step, to );
			uint64_t count = stop_at - nonce;

			for ( ; nonce < stop_at; nonce++ ) {

				hash_one ( static_cast < uint32_t > ( nonce ), digest );

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

	bool engine::enter_dataset ( const unit& work, tools& kit ) {

		base::atomic_add ( rx_users, 1u );

		if ( base::atomic_load ( rx_ready ) && rx_built_for == base::atomic_load ( rx_full ) && base::same ( rx_seed, work.seed, 32 ) ) {

			uint32_t generation = base::atomic_load ( rx_generation );

			if ( kit.generation == generation )
				return true;

			if ( !kit.machine )
				kit.machine = new randomx::machine;

			if ( kit.machine->open ( rx_cache, base::atomic_load ( rx_state ) == DATASET_FULL ? rx_dataset : nullptr ) ) {

				kit.generation = generation;

				return true;

			}

		}

		base::atomic_add ( rx_users, ~0u );
		build_dataset ( work.seed );

		return false;

	}

	void engine::leave_dataset ( ) {

		base::atomic_add ( rx_users, ~0u );

	}

	void engine::build_dataset ( const uint8_t* seed ) {

		if ( !base::atomic_replace ( rx_building, 0u, 1u ) )
			return;

		uint32_t full = base::atomic_load ( rx_full );

		if ( base::atomic_load ( rx_ready ) && rx_built_for == full && base::same ( rx_seed, seed, 32 ) ) {

			base::atomic_store ( rx_building, 0u );

			return;

		}

		double began = base::seconds ( );

		base::atomic_exchange ( rx_ready, 0u );

		while ( base::atomic_load ( rx_users ) && !base::atomic_load ( quit ) )
			base::sleep ( 1 );

		base::atomic_store ( rx_progress, 0u );
		base::atomic_store ( rx_state, static_cast < uint32_t > ( DATASET_BUILDING ) );

		if ( !rx_cache )
			rx_cache = new randomx::cache;

		bool built = rx_cache->build ( seed, 32 );
		uint32_t state = DATASET_LIGHT;

		if ( built && full ) {

			if ( !rx_dataset )
				rx_dataset = new randomx::dataset;

			if ( rx_dataset->build ( *rx_cache, base::max ( base::atomic_load ( wanted_threads ), 1u ), &rx_progress, &quit ) )
				state = DATASET_FULL;
			else {

				rx_dataset->release ( );
				state = DATASET_SHORT;

			}

		} else if ( rx_dataset ) {

			delete rx_dataset;
			rx_dataset = nullptr;

		}

		bool cancelled = base::atomic_load ( quit ) != 0;

		rx_large = state == DATASET_FULL && rx_dataset->large_pages ( );

		uint64_t fitting = level3 * ( rx_large ? 4 : 3 ) / 4 / randomx::scratchpad_bytes;

		base::atomic_store ( rx_lanes, level3 ? static_cast < uint32_t > ( base::clamp ( fitting, 1ull, static_cast < uint64_t > ( lane_count ) ) ) : lane_count );
		base::copy ( rx_seed, seed, 32 );
		rx_built_for = full;
		rx_seconds = base::seconds ( ) - began;
		base::atomic_add ( rx_generation, 1u );
		base::atomic_store ( rx_progress, 1000u );
		base::atomic_store ( rx_state, built && !cancelled ? state : static_cast < uint32_t > ( DATASET_NONE ) );
		base::atomic_store ( rx_ready, built && !cancelled ? 1u : 0u );
		base::atomic_store ( rx_building, 0u );

	}

	void engine::run_lane ( lane& self ) {

		tools kit;
		uint32_t applied = ~0u;
		double debt = 0.0;

		while ( !base::atomic_load ( quit ) ) {

			bool spare = algorithm ( ) == chain::ALGORITHM_RANDOMX && self.index >= base::atomic_load ( rx_lanes );

			if ( self.index >= base::atomic_load ( wanted_threads ) || spare || base::atomic_load ( paused ) || base::atomic_load ( halt ) ) {

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

			if ( !claim ( false, nullptr, work, from, to ) ) {

				base::sleep ( 20 );

				continue;

			}

			if ( work.algorithm == chain::ALGORITHM_RANDOMX ) {

				if ( !enter_dataset ( work, kit ) ) {

					base::sleep ( 20 );

					continue;

				}

				uint8_t blob [ blob_room ];
				uint32_t size = base::min ( work.blob_size, blob_room );
				uint32_t at = base::min ( work.nonce_at, size >= 4 ? size - 4 : 0u );

				base::copy ( blob, work.blob, size );

				auto hash_one = [ & ] ( uint32_t nonce, uint8_t* digest ) {

					base::store_little ( blob + at, nonce );
					kit.machine->hash ( blob, size, digest );

				};

				sweep ( self, work, from, to, 4, debt, hash_one );
				leave_dataset ( );

			} else if ( work.algorithm == chain::ALGORITHM_SCRYPT ) {

				if ( !kit.scratch )
					kit.scratch = base::allocate ( hash::scrypt_pair_scratch_bytes );

				chain::header first = work.header;
				chain::header second = work.header;
				uint64_t waiting = nonce_space;
				uint8_t spare [ 32 ];

				auto hash_one = [ & ] ( uint32_t nonce, uint8_t* digest ) {

					if ( nonce == waiting ) {

						base::copy ( digest, spare, 32 );

						return;

					}

					first.set_nonce ( nonce );
					second.set_nonce ( nonce + 1 );
					hash::scrypt_header_pair ( first.bytes, second.bytes, digest, spare, kit.scratch );
					waiting = static_cast < uint64_t > ( nonce ) + 1;

				};

				sweep ( self, work, from, to, 128, debt, hash_one );

			} else {

				kit.hasher.prepare ( work.header, compress );

				auto hash_one = [ & ] ( uint32_t nonce, uint8_t* digest ) {

					kit.hasher.hash ( nonce, digest );

				};

				sweep ( self, work, from, to, 4096, debt, hash_one );

			}

		}

	}

	void engine::run_gpu ( ) {

		gpu_device device;
		chain::header_hasher checker;
		void* scratch = nullptr;
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

			uint8_t kind = algorithm ( );
			uint32_t rooms [ chain::ALGORITHM_COUNT ] = { 1u << base::atomic_load ( gpu_power ), kind == chain::ALGORITHM_SCRYPT ? device.capacity ( kind ) : 0u, 0u };

			base::atomic_store ( gpu_phase, static_cast < uint32_t > ( rooms [ kind ] ? GPU_RUNNING : GPU_UNSUPPORTED ) );

			unit work;
			uint64_t from = 0;
			uint64_t to = 0;

			if ( !claim ( true, rooms, work, from, to ) ) {

				base::sleep ( rooms [ kind ] ? 20 : 100 );

				continue;

			}

			bool scrypt = work.algorithm == chain::ALGORITHM_SCRYPT;
			uint32_t threshold = base::load_little ( work.target + 28 );
			uint32_t count = static_cast < uint32_t > ( to - from );
			uint32_t found_count = 0;
			double started = base::seconds ( );

			if ( !scrypt )
				count = ( count + 255 ) & ~255u;

			if ( !device.search ( work.algorithm, work.header, static_cast < uint32_t > ( from ), count, threshold, found, base::count_of ( found ), found_count ) ) {

				base::atomic_store ( gpu_phase, static_cast < uint32_t > ( GPU_FAILED ) );

				break;

			}

			chain::header header = work.header;

			if ( found_count && !scrypt )
				checker.prepare ( work.header, compress );

			if ( found_count && scrypt && !scratch )
				scratch = base::allocate ( hash::scrypt_scratch_bytes );

			for ( uint32_t i = 0; i < found_count; i++ ) {

				if ( scrypt ) {

					header.set_nonce ( found [ i ] );
					hash::scrypt_header ( header.bytes, digest, scratch );

				} else
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

		base::release ( scratch );
		device.close ( );

	}

}
