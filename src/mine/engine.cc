#include "mine/engine.hh"
#include "mine/gpu/device.hh"
#include "randomx/machine.hh"
#include "hash/aes.hh"
#include "hash/heavyhash.hh"
#include "hash/scrypt.hh"
#include "chain/target.hh"
#include "base/bits.hh"
#include "base/system/clock.hh"
#include "base/memory.hh"
#include "base/strings.hh"
#include "base/system/hardware.hh"

namespace mine {

	struct engine::tools {

		chain::header_hasher hasher;
		void* scratch = nullptr;
		randomx::machine* machine = nullptr;
		hash::heavy_hasher* heavy = nullptr;
		uint32_t generation = ~0u;

		~tools ( ) {

			base::release ( scratch );
			delete machine;
			delete heavy;

		}

	};

	static uint64_t lane_chunk ( uint8_t algorithm, bool full ) {

		if ( algorithm == chain::ALGORITHM_SCRYPT )
			return 1024;

		if ( algorithm == chain::ALGORITHM_RANDOMX )
			return full ? 64 : 8;

		if ( algorithm == chain::ALGORITHM_KHEAVYHASH )
			return 1ull << 14;

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
		wide_scrypt = cpu.avx2;

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
		gpus_started = 0;

		for ( gpu_slot& slot : gpus )
			slot = gpu_slot { };

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

		for ( uint32_t i = 0; i < gpus_started; i++ ) {

			base::join_thread ( gpus [ i ].thread );
			gpus [ i ].thread = nullptr;
			base::atomic_store ( gpus [ i ].phase, static_cast < uint32_t > ( GPU_OFF ) );

		}

		delete [ ] lane_list;
		lane_list = nullptr;
		lane_count = 0;
		active = false;

		if ( base::atomic_load ( rx_state ) == DATASET_BUILDING )
			base::atomic_store ( rx_state, static_cast < uint32_t > ( DATASET_NONE ) );

	}

	void engine::apply ( const limits& settings ) {

		base::atomic_store ( wanted_threads, base::min ( settings.threads, lane_count ) );
		base::atomic_store ( cpu_share, base::clamp ( settings.cpu_percent, 5u, 100u ) );
		base::atomic_store ( priority, static_cast < uint32_t > ( settings.priority ) );
		base::atomic_store ( pinning, settings.pin_threads ? 1u : 0u );
		base::atomic_store ( gpu_share, base::clamp ( settings.gpu_percent, 5u, 100u ) );
		base::atomic_store ( gpu_power, base::clamp ( settings.gpu_intensity, 16u, 26u ) );
		base::atomic_store ( gpu_heat_limit, settings.gpu_heat );
		base::atomic_store ( gpu_wanted, settings.gpu ? 1u : 0u );
		base::atomic_store ( rx_full, settings.full_dataset ? 1u : 0u );

		for ( uint32_t i = 0; i < max_gpus; i++ ) {

			const card_setting& card = settings.cards [ i ];
			gpu_slot& slot = gpus [ i ];

			base::atomic_store ( slot.on, !card.custom || card.on ? 1u : 0u );
			base::atomic_store ( slot.share, base::clamp ( card.custom ? card.percent : settings.gpu_percent, 5u, 100u ) );
			base::atomic_store ( slot.power, base::clamp ( card.custom ? card.intensity : settings.gpu_intensity, 16u, 26u ) );
			base::atomic_store ( slot.heat_limit, card.custom ? card.heat : settings.gpu_heat );

		}

		if ( !active || !settings.gpu || gpus_started )
			return;

		uint32_t count = base::clamp ( settings.adapter_count, 1u, max_gpus );

		for ( uint32_t i = 0; i < count; i++ ) {

			gpu_slot& slot = gpus [ i ];

			slot.owner = this;
			slot.adapter = settings.adapters [ i ];
			slot.phase = GPU_STARTING;
			slot.thread = base::start_thread ( &gpu_main, &slot );

		}

		gpus_started = count;

	}

	uint64_t engine::gpu_hashes ( ) const {

		uint64_t sum = 0;

		for ( uint32_t i = 0; i < gpus_started; i++ )
			sum += base::atomic_load ( gpus [ i ].done );

		return sum;

	}

	uint32_t engine::gpu_errors ( ) const {

		uint32_t sum = 0;

		for ( uint32_t i = 0; i < gpus_started; i++ )
			sum += base::atomic_load ( gpus [ i ].faults );

		return sum;

	}

	gpu_state engine::gpu_status ( ) const {

		static const uint8_t rank [ ] = { 0, 2, 5, 1, 3, 4 };
		uint32_t best = GPU_OFF;

		for ( uint32_t i = 0; i < gpus_started; i++ ) {

			uint32_t phase = base::atomic_load ( gpus [ i ].phase );

			if ( rank [ phase ] > rank [ best ] )
				best = phase;

		}

		return static_cast < gpu_state > ( best );

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

		gpu_slot* self = static_cast < gpu_slot* > ( context );

		self->owner->run_gpu ( *self );

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

		if ( base::atomic_load ( rx_ready ) && rx_built_for == base::atomic_load ( rx_full ) && base::atomic_load ( rx_kind ) == work.variant && base::same ( rx_seed, work.seed, 32 ) ) {

			uint32_t generation = base::atomic_load ( rx_generation );

			if ( kit.generation == generation )
				return true;

			if ( !kit.machine )
				kit.machine = new randomx::machine;

			if ( kit.machine->open ( rx_cache, base::atomic_load ( rx_state ) == DATASET_FULL ? rx_dataset : nullptr, work.variant ) ) {

				kit.generation = generation;

				return true;

			}

		}

		base::atomic_add ( rx_users, ~0u );
		build_dataset ( work.seed, work.variant );

		return false;

	}

	void engine::leave_dataset ( ) {

		base::atomic_add ( rx_users, ~0u );

	}

	void engine::build_dataset ( const uint8_t* seed, uint8_t kind ) {

		if ( !base::atomic_replace ( rx_building, 0u, 1u ) )
			return;

		uint32_t full = base::atomic_load ( rx_full );

		if ( base::atomic_load ( rx_ready ) && rx_built_for == full && base::atomic_load ( rx_kind ) == kind && base::same ( rx_seed, seed, 32 ) ) {

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

		bool built = rx_cache->build ( seed, 32, kind );
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

		uint64_t fitting = level3 * ( rx_large ? 4 : 3 ) / 4 / randomx::variant_of ( kind ).level3;

		base::atomic_store ( rx_lanes, level3 ? static_cast < uint32_t > ( base::clamp ( fitting, 1ull, static_cast < uint64_t > ( lane_count ) ) ) : lane_count );
		base::copy ( rx_seed, seed, 32 );
		base::atomic_store ( rx_kind, static_cast < uint32_t > ( kind ) );
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

		if ( base::atomic_load ( pinning ) )
			base::pin_thread ( self.index );

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
					kit.scratch = base::allocate ( hash::scrypt_quad_scratch_bytes );

				chain::header headers [ 4 ] = { work.header, work.header, work.header, work.header };
				uint8_t results [ 4 ] [ 32 ];
				uint64_t ready_from = nonce_space;
				uint32_t group = wide_scrypt ? 4 : 2;

				auto hash_one = [ & ] ( uint32_t nonce, uint8_t* digest ) {

					if ( nonce >= ready_from && nonce < ready_from + group ) {

						base::copy ( digest, results [ nonce - ready_from ], 32 );

						return;

					}

					for ( uint32_t i = 0; i < group; i++ )
						headers [ i ].set_nonce ( nonce + i );

					if ( group == 4 ) {

						const uint8_t* inputs [ 4 ] = { headers [ 0 ].bytes, headers [ 1 ].bytes, headers [ 2 ].bytes, headers [ 3 ].bytes };
						uint8_t* outputs [ 4 ] = { results [ 0 ], results [ 1 ], results [ 2 ], results [ 3 ] };

						hash::scrypt_header_quad ( inputs, outputs, kit.scratch );

					} else
						hash::scrypt_header_pair ( headers [ 0 ].bytes, headers [ 1 ].bytes, results [ 0 ], results [ 1 ], kit.scratch );

					ready_from = nonce;
					base::copy ( digest, results [ 0 ], 32 );

				};

				sweep ( self, work, from, to, 128, debt, hash_one );

			} else if ( work.algorithm == chain::ALGORITHM_KHEAVYHASH ) {

				if ( !kit.heavy )
					kit.heavy = new hash::heavy_hasher;

				uint64_t high = static_cast < uint64_t > ( work.nonce_high ) << 32;

				kit.heavy->prepare ( work.blob, work.timestamp );

				auto hash_one = [ & ] ( uint32_t nonce, uint8_t* digest ) {

					kit.heavy->hash ( high | nonce, digest );

				};

				sweep ( self, work, from, to, 1024, debt, hash_one );

			} else {

				kit.hasher.prepare ( work.header, compress );

				auto hash_one = [ & ] ( uint32_t nonce, uint8_t* digest ) {

					kit.hasher.hash ( nonce, digest );

				};

				sweep ( self, work, from, to, 4096, debt, hash_one );

			}

		}

	}

	void engine::run_gpu ( gpu_slot& self ) {

		static constexpr double backoff [ ] = { 2.0, 5.0, 15.0, 30.0, 60.0 };
		static constexpr uint32_t restart_limit = 6;
		static constexpr double restart_window = 600.0;
		static constexpr double watchdog_margin = 0.5;

		struct batch {

			bool live = false;
			int32_t slot = 0;
			unit work;
			uint64_t from = 0;
			uint64_t to = 0;
			double sent = 0.0;

		};

		gpu_device* device = new gpu_device;
		chain::header_hasher checker;
		hash::heavy_hasher* heavy_send = nullptr;
		hash::heavy_hasher* heavy_check = nullptr;
		void* scratch = nullptr;
		uint8_t digest [ 32 ];
		uint32_t found [ 32 ];
		double debt = 0.0;
		double measured = 0.0;
		double last_collect = base::seconds ( );
		double window_began = 0.0;
		uint32_t window_restarts = 0;
		uint32_t trim = 0;
		uint32_t settled = 0;
		bool cooling = false;
		bool broken = false;
		bool gave_up = false;
		batch* waiting = new batch;
		batch* next = new batch;

		base::set_thread_priority ( base::PRIORITY_HIGH );

		auto rest_quietly = [ & ] ( double seconds ) {

			double until = base::seconds ( ) + seconds;

			while ( !base::atomic_load ( quit ) && base::seconds ( ) < until )
				base::sleep ( 100 );

		};

		auto allowed_again = [ & ] ( ) {

			double now = base::seconds ( );

			if ( now - window_began > restart_window ) {

				window_began = now;
				window_restarts = 0;

			}

			return ++window_restarts <= restart_limit;

		};

		auto settle = [ & ] ( batch& done ) {

			uint32_t found_count = 0;

			done.live = false;

			if ( !device->collect ( done.slot, found, base::count_of ( found ), found_count ) ) {

				broken = true;

				return;

			}

			double took = base::seconds ( ) - done.sent;

			if ( ++settled > 4 && took > watchdog_margin && trim < 6 ) {

				trim++;
				base::atomic_store ( self.trimmed, trim );

			}

			bool scrypt = done.work.algorithm == chain::ALGORITHM_SCRYPT;
			bool heavy = done.work.algorithm == chain::ALGORITHM_KHEAVYHASH;
			uint32_t threshold = base::load_little ( done.work.target + 28 );
			chain::header header = done.work.header;

			if ( found_count && heavy ) {

				if ( !heavy_check )
					heavy_check = new hash::heavy_hasher;

				heavy_check->prepare ( done.work.blob, done.work.timestamp );

			} else if ( found_count && !scrypt )
				checker.prepare ( done.work.header, compress );

			if ( found_count && scrypt && !scratch )
				scratch = base::allocate ( hash::scrypt_scratch_bytes );

			for ( uint32_t i = 0; i < found_count; i++ ) {

				if ( scrypt ) {

					header.set_nonce ( found [ i ] );
					hash::scrypt_header ( header.bytes, digest, scratch );

				} else if ( heavy )
					heavy_check->hash ( static_cast < uint64_t > ( done.work.nonce_high ) << 32 | found [ i ], digest );
				else
					checker.hash ( found [ i ], digest );

				if ( base::load_little ( digest + 28 ) > threshold ) {

					base::atomic_add ( self.faults, 1u );

					continue;

				}

				base::atomic_lower ( best_top, chain::top_bits ( digest ) );

				if ( chain::meets ( digest, done.work.target ) )
					report ( done.work, found [ i ], digest );

			}

			base::atomic_add ( self.done, done.to - done.from );

		};

		while ( !base::atomic_load ( quit ) && !gave_up ) {

			bool opened = device->open ( self.adapter );

			base::copy_text ( self.label, base::count_of ( self.label ), device->name ( ) );

			if ( !opened ) {

				device->close ( );

				if ( !base::atomic_load ( self.restarts ) || !allowed_again ( ) ) {

					gave_up = true;

					break;

				}

				base::atomic_store ( self.phase, static_cast < uint32_t > ( GPU_STARTING ) );
				rest_quietly ( backoff [ base::min ( window_restarts, 5u ) - 1 ] );

				continue;

			}

			broken = false;
			waiting->live = false;
			next->live = false;
			last_collect = base::seconds ( );
			base::atomic_store ( self.phase, static_cast < uint32_t > ( GPU_RUNNING ) );

			while ( !base::atomic_load ( quit ) && !broken ) {

				double now = base::seconds ( );

				if ( base::atomic_exchange ( sabotage, 0u ) ) {

					broken = true;

					break;

				}

				if ( now - measured >= 1.0 ) {

					float celsius = 0.0f;
					uint32_t limit = base::atomic_load ( self.heat_limit );

					measured = now;

					if ( device->temperature ( celsius ) ) {

						base::atomic_store ( self.heat, static_cast < uint32_t > ( celsius * 10.0f ) );

						if ( limit && celsius >= static_cast < float > ( limit ) )
							cooling = true;
						else if ( !limit || celsius <= static_cast < float > ( limit ) - 4.0f )
							cooling = false;

					}

				}

				if ( !base::atomic_load ( gpu_wanted ) || !base::atomic_load ( self.on ) || base::atomic_load ( paused ) || base::atomic_load ( halt ) || cooling ) {

					if ( waiting->live )
						settle ( *waiting );

					if ( cooling )
						base::atomic_store ( self.phase, static_cast < uint32_t > ( GPU_COOLING ) );

					base::sleep ( 30 );
					last_collect = base::seconds ( );

					continue;

				}

				uint8_t kind = algorithm ( );
				uint32_t power = base::max ( base::atomic_load ( self.power ) - base::min ( trim, base::atomic_load ( self.power ) - 16u ), 16u );
				uint32_t scrypt_room = kind == chain::ALGORITHM_SCRYPT ? base::max ( device->capacity ( kind ) >> trim, 256u ) : 0u;
				uint32_t heavy_room = kind == chain::ALGORITHM_KHEAVYHASH && device->capacity ( kind ) ? 1u << ( power - 2 ) : 0u;
				uint32_t rooms [ chain::ALGORITHM_COUNT ] = { 1u << power, scrypt_room, 0u, heavy_room };
				uint32_t share = base::atomic_load ( self.share );
				bool overlap = share >= 100;

				base::atomic_store ( self.phase, static_cast < uint32_t > ( rooms [ kind ] ? GPU_RUNNING : GPU_UNSUPPORTED ) );

				next->live = claim ( true, rooms, next->work, next->from, next->to );

				bool claimed = next->live;

				if ( next->live ) {

					uint32_t count = static_cast < uint32_t > ( next->to - next->from );

					if ( next->work.algorithm != chain::ALGORITHM_SCRYPT )
						count = ( count + 255 ) & ~255u;

					next->sent = base::seconds ( );

					bool sent = false;

					if ( next->work.algorithm == chain::ALGORITHM_KHEAVYHASH ) {

						if ( !heavy_send )
							heavy_send = new hash::heavy_hasher;

						heavy_send->prepare ( next->work.blob, next->work.timestamp );
						sent = device->submit_heavy ( *heavy_send, next->work.nonce_high, static_cast < uint32_t > ( next->from ), count, base::load_little ( next->work.target + 28 ), next->slot );

					} else
						sent = device->submit ( next->work.algorithm, next->work.header, static_cast < uint32_t > ( next->from ), count, base::load_little ( next->work.target + 28 ), next->slot );

					if ( !sent ) {

						broken = true;

						break;

					}

				}

				if ( waiting->live )
					settle ( *waiting );

				if ( next->live && !overlap && !broken )
					settle ( *next );

				batch* kept = waiting;

				waiting = next;
				next = kept;

				if ( !claimed && !waiting->live ) {

					base::sleep ( rooms [ kind ] ? 20 : 100 );
					last_collect = base::seconds ( );

					continue;

				}

				double finished = base::seconds ( );

				rest ( debt, finished - last_collect, share );
				last_collect = base::seconds ( );

			}

			if ( !broken ) {

				if ( waiting->live )
					settle ( *waiting );

				break;

			}

			device->close ( );
			delete device;
			device = new gpu_device;
			base::atomic_add ( self.restarts, 1u );

			if ( !allowed_again ( ) ) {

				gave_up = true;

				break;

			}

			base::atomic_store ( self.phase, static_cast < uint32_t > ( GPU_STARTING ) );
			rest_quietly ( backoff [ base::min ( window_restarts, 5u ) - 1 ] );

		}

		if ( gave_up )
			base::atomic_store ( self.phase, static_cast < uint32_t > ( GPU_FAILED ) );

		delete waiting;
		delete next;
		delete heavy_send;
		delete heavy_check;
		base::release ( scratch );
		device->close ( );
		delete device;

	}

}
