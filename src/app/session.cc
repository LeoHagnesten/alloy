#include "app/session.hh"
#include "app/journal.hh"
#include "chain/coinbase.hh"
#include "randomx/variant.hh"
#include "base/system/processes.hh"
#include "version.hh"
#include "chain/target.hh"
#include "mine/gpu/device.hh"
#include "base/system/clock.hh"
#include "base/memory.hh"
#include "base/strings.hh"
#include "base/text.hh"

namespace app {

	static uint8_t algorithm_of ( const ui::preferences& settings ) {

		uint8_t value = static_cast < uint8_t > ( settings.algorithm % chain::ALGORITHM_COUNT );

		if ( settings.mode == ui::MODE_GENESIS && value == chain::ALGORITHM_RANDOMX )
			return chain::ALGORITHM_SHA256D;

		return value;

	}

	static uint8_t coin_in_use ( uint8_t chosen, uint8_t kind, uint8_t flavour ) {

		if ( kind == chain::ALGORITHM_RANDOMX && ( !chain::coin_mines ( chosen, kind ) || chain::coin_of ( chosen ).variant != flavour ) )
			return chain::coin_of_variant ( flavour );

		return chain::coin_mines ( chosen, kind ) ? chosen : chain::first_coin ( kind );

	}

	static void real_genesis ( uint8_t algorithm, chain::header& out ) {

		if ( algorithm == chain::ALGORITHM_SCRYPT )
			chain::litecoin_genesis ( out );
		else
			chain::genesis ( out );

	}

	static uint32_t today_number ( ) {

		return base::day_number ( base::local_calendar ( ) );

	}

	static mine::limits limits_of ( const ui::preferences& settings, int32_t adapters ) {

		mine::limits out;

		out.threads = settings.cpu ? settings.threads : 0;
		out.cpu_percent = settings.cpu_limit;
		out.priority = static_cast < base::thread_priority > ( base::min ( settings.priority, static_cast < uint8_t > ( 2 ) ) );
		out.choice = static_cast < mine::kernel > ( base::min ( settings.kernel, static_cast < uint8_t > ( 2 ) ) );
		out.gpu = settings.gpu;
		out.gpu_percent = settings.gpu_limit;
		out.gpu_intensity = settings.intensity;
		out.gpu_heat = settings.gpu_heat;
		out.pin_threads = settings.pin_threads;
		out.dataset_megabytes = settings.dataset_megabytes;
		out.algorithm = algorithm_of ( settings );

		if ( settings.adapter == ui::all_adapters && adapters > 1 ) {

			out.adapter_count = static_cast < uint32_t > ( base::min ( adapters, static_cast < int32_t > ( mine::max_gpus ) ) );

			for ( uint32_t i = 0; i < out.adapter_count; i++ )
				out.adapters [ i ] = i;

		} else {

			out.adapter_count = 1;
			out.adapters [ 0 ] = settings.adapter == ui::all_adapters ? 0 : settings.adapter;

		}

		for ( uint32_t i = 0; i < out.adapter_count && settings.adapter == ui::all_adapters; i++ ) {

			if ( out.adapters [ i ] >= static_cast < uint32_t > ( ui::adapter_room ) )
				continue;

			const ui::card_tuning& card = settings.cards [ out.adapters [ i ] ];

			out.cards [ i ].custom = card.custom;
			out.cards [ i ].on = card.on;
			out.cards [ i ].percent = card.limit;
			out.cards [ i ].intensity = card.intensity;
			out.cards [ i ].heat = card.heat;

		}

		return out;

	}

	static uint8_t dataset_phase_of ( mine::dataset_state state ) {

		switch ( state ) {

			case mine::DATASET_BUILDING:
				return ui::DATASET_BUILDING;

			case mine::DATASET_FULL:
				return ui::DATASET_FULL;

			case mine::DATASET_LIGHT:
				return ui::DATASET_LIGHT;

			case mine::DATASET_SHORT:
				return ui::DATASET_SHORT;

			case mine::DATASET_PART:
				return ui::DATASET_PART;

			default:
				return ui::DATASET_NONE;

		}

	}

	static uint8_t phase_of ( pool::link_state state ) {

		switch ( state ) {

			case pool::LINK_CONNECTING:
				return ui::PHASE_CONNECTING;

			case pool::LINK_SUBSCRIBING:
			case pool::LINK_AUTHORIZING:
				return ui::PHASE_SIGNING_IN;

			case pool::LINK_READY:
				return ui::PHASE_READY;

			case pool::LINK_WAITING:
				return ui::PHASE_RETRYING;

			case pool::LINK_REFUSED:
				return ui::PHASE_REFUSED;

			default:
				return ui::PHASE_OFF;

		}

	}

	static uint8_t gpu_phase_of ( mine::gpu_state state ) {

		if ( state == mine::GPU_STARTING )
			return ui::GPU_STARTING;

		if ( state == mine::GPU_RUNNING )
			return ui::GPU_WORKING;

		if ( state == mine::GPU_UNSUPPORTED )
			return ui::GPU_UNSUPPORTED;

		if ( state == mine::GPU_COOLING )
			return ui::GPU_COOLING;

		return state == mine::GPU_FAILED ? ui::GPU_BROKEN : ui::GPU_IDLE;

	}

	static const wchar_t* mode_name ( uint8_t mode ) {

		if ( mode == ui::MODE_BENCHMARK )
			return L"benchmark";

		return mode == ui::MODE_GENESIS ? L"genesis search" : L"pool mining";

	}

	session::session ( ) {

		shown = new ui::snapshot;
		base::detect ( cpu );
		base::copy_text ( shown->cpu_name, base::count_of ( shown->cpu_name ), cpu.label );
		shown->cores = cpu.cores;
		shown->threads = cpu.threads;
		shown->sha = cpu.sha && cpu.sse41 && cpu.ssse3;
		shown->aes = cpu.aes;
		shown->pages_granted = base::large_pages_granted ( );

		mine::adapter_info adapters [ ui::adapter_room ];

		shown->adapter_count = mine::list_adapters ( adapters, ui::adapter_room );

		for ( int32_t i = 0; i < shown->adapter_count; i++ )
			base::copy_text ( shown->adapters [ i ], base::count_of ( shown->adapters [ i ] ), adapters [ i ].name );

		base::copy_text ( shown->status, base::count_of ( shown->status ), L"Ready" );

	}

	session::~session ( ) {

		stop ( );

		if ( checker ) {

			base::join_thread ( checker );
			checker = nullptr;

		}

		online.stop ( );
		watched.stop ( );
		save_history ( );
		delete checked;
		delete shown;

	}

	mine::limits session::limits_for ( const ui::preferences& settings ) const {

		mine::limits out = limits_of ( settings, shown->adapter_count );
		uint32_t broken = broken_cards [ out.algorithm % chain::ALGORITHM_COUNT ];
		uint32_t kept = 0;

		if ( portable_only )
			out.choice = mine::KERNEL_PORTABLE;

		for ( uint32_t i = 0; i < out.adapter_count; i++ ) {

			if ( !( broken & 1u << out.adapters [ i ] ) )
				out.adapters [ kept++ ] = out.adapters [ i ];

		}

		out.adapter_count = kept;
		out.gpu = out.gpu && kept > 0;

		return out;

	}

	unsigned long session::check_main ( void* context ) {

		session* self = static_cast < session* > ( context );

		base::set_thread_priority ( base::PRIORITY_LOW );
		run_self_test ( *self->checked, false );

		return 0;

	}

	void session::check_kernels ( ) {

		if ( checker )
			return;

		checked = new self_report;
		checker = base::start_thread ( &check_main, this );

	}

	void session::finish_check ( const ui::preferences& settings ) {

		base::join_thread ( checker );
		checker = nullptr;

		const self_report& report = *checked;
		base::text line;

		if ( !report.failed ) {

			line.add ( L"Self-test passed: " ).number ( static_cast < uint64_t > ( report.count ) ).add ( L" kernel checks in " ).decimal ( report.seconds, 1 ).add ( L" s" );
			log ( ui::LEVEL_INFO, line.get ( ) );

		} else {

			for ( int32_t i = 0; i < report.count; i++ ) {

				const check_result& item = report.checks [ i ];

				if ( item.passed || item.skipped )
					continue;

				line.clear ( );
				line.add ( L"Self-test failed: " ).add ( item.name ).add ( L". Alloy won't use that kernel." );
				log ( ui::LEVEL_ERROR, line.get ( ) );
				announce ( L"A hash kernel gave a wrong answer", line.get ( ) );

			}

		}

		bool restrict = !report.sha_extensions || report.broken_sha_cards || report.broken_scrypt_cards;

		portable_only = !report.sha_extensions;
		broken_cards [ chain::ALGORITHM_SHA256D ] = report.broken_sha_cards;
		broken_cards [ chain::ALGORITHM_SCRYPT ] = report.broken_scrypt_cards;
		broken_cards [ chain::ALGORITHM_KHEAVYHASH ] = report.broken_heavy_cards;
		delete checked;
		checked = nullptr;

		if ( restrict && active )
			start ( settings );

	}

	bool session::start ( const ui::preferences& settings ) {

		stop ( );
		sync_api ( settings );

		ui::snapshot& view = *shown;

		mode = settings.mode;
		adapter = settings.adapter;
		kernel = settings.kernel;
		variant = settings.variant;
		algorithm = algorithm_of ( settings );
		coin = coin_in_use ( settings.coin ( ), algorithm, variant );
		view.coin = coin;
		view.mode = mode;
		view.algorithm = algorithm;
		view.height = 0;
		view.blob_size = 0;
		base::zero ( view.seed, 32 );
		view.rate = 0.0;
		view.average = 0.0;
		view.peak = 0.0;
		view.uptime = 0.0;
		view.best = 0.0;
		view.hashes = 0;
		view.accepted = 0;
		view.rejected = 0;
		view.stale = 0;
		view.faults = 0;
		view.history_count = 0;
		view.history_first = 0;
		view.history_serial++;
		view.history_time = 0.0;
		view.live_rate = 0.0;
		live_count = 0;
		view.cpu_rate = 0.0;
		view.cpu_hashes = 0;
		view.lane_count = 0;
		view.gpu_rate = 0.0;
		view.gpu_hashes = 0;
		view.gpu_state = ui::GPU_IDLE;
		view.gpu_name [ 0 ] = 0;
		view.link = ui::PHASE_OFF;
		view.difficulty = 0.0;
		view.latency = 0.0;
		view.has_job = false;
		view.job [ 0 ] = 0;
		view.progress = 0.0;
		view.remaining = 0.0;
		view.solved = false;
		view.nonce = 0;
		view.from = 0;
		view.share_count = 0;
		view.share_serial++;
		view.paused = false;
		view.pause_reason [ 0 ] = 0;

		mine::feeder* feed = &benchmark;

		if ( algorithm != chain::ALGORITHM_RANDOMX )
			engine.release_dataset ( );

		benchmark.set_algorithm ( algorithm );
		benchmark.set_variant ( settings.variant );
		view.variant = settings.variant;

		if ( mode == ui::MODE_POOL ) {

			pool::settings link;
			const ui::pool_profile& profile = settings.pool ( );

			link.algorithm = algorithm;
			link.variant = settings.variant;
			link.coin = coin;
			base::copy_text ( link.address, base::count_of ( link.address ), profile.address );
			base::copy_text ( link.backup, base::count_of ( link.backup ), profile.backup );
			base::copy_text ( link.pin, sizeof ( link.pin ), profile.pin );
			base::copy_text ( link.backup_pin, sizeof ( link.backup_pin ), profile.backup_pin );
			base::copy_text ( link.worker, base::count_of ( link.worker ), profile.worker );
			base::copy_text ( link.password, base::count_of ( link.password ), profile.password );
			client.start ( link );
			feed = &client;

		} else if ( mode == ui::MODE_GENESIS ) {

			genesis.reset ( genesis_from, algorithm );
			feed = &genesis;

		}

		engine.start ( feed, limits_for ( settings ) );
		active = true;
		paused = false;
		held = false;
		view.run_earned = 0.0;
		started = base::seconds ( );
		last_sample = started;
		last_tick = started;
		last_total = 0;
		last_gpu = 0;
		base::zero ( gpu_last, sizeof ( gpu_last ) );
		base::zero ( gpu_restarts_seen, sizeof ( gpu_restarts_seen ) );
		base::zero ( gpu_trims_seen, sizeof ( gpu_trims_seen ) );
		base::zero ( gpu_phase_seen, sizeof ( gpu_phase_seen ) );
		reject_streak = 0;
		pin_serial = 0;
		pin_waiting = false;
		view.gpu_count = 0;
		base::zero ( lane_last, sizeof ( lane_last ) );
		view.running = true;
		view.lane_count = static_cast < int32_t > ( engine.lanes ( ) );
		view.active_threads = engine.working_threads ( );
		view.extensions = engine.uses_extensions ( );

		base::text line;

		line.add ( L"Started " ).add ( chain::algorithm_name ( algorithm ) ).put ( L' ' ).add ( mode_name ( mode ) );

		if ( settings.cpu && settings.threads ) {

			line.add ( L" on " ).number ( base::min ( settings.threads, engine.lanes ( ) ) ).add ( settings.threads == 1 ? L" thread" : L" threads" );

			if ( algorithm == chain::ALGORITHM_SHA256D )
				line.add ( view.extensions ? L" (SHA extensions)" : L" (portable kernel)" );
			else if ( algorithm == chain::ALGORITHM_RANDOMX )
				line.add ( cpu.aes ? L" (AES-NI)" : L" (software AES)" );

		}

		if ( settings.gpu && algorithm != chain::ALGORITHM_RANDOMX )
			line.add ( settings.cpu && settings.threads ? L" and the GPU" : L" on the GPU" );

		log ( ui::LEVEL_INFO, line.get ( ) );
		describe ( settings );

		return true;

	}

	void session::stop ( ) {

		if ( !active )
			return;

		double ran = base::seconds ( ) - started;
		uint64_t total = engine.hashes ( );

		engine.stop ( );
		client.stop ( );
		drain_pool ( );
		save_history ( );
		active = false;
		shown->running = false;
		shown->paused = false;
		shown->rate = 0.0;
		shown->cpu_rate = 0.0;
		shown->gpu_rate = 0.0;
		shown->gpu_state = ui::GPU_IDLE;
		shown->link = ui::PHASE_OFF;

		for ( int32_t i = 0; i < shown->lane_count; i++ )
			shown->lane_rates [ i ] = 0.0f;

		base::text line;

		line.add ( L"Stopped after " ).duration ( ran ).add ( L", " ).compact ( static_cast < double > ( total ) ).add ( L" hashes" );
		log ( ui::LEVEL_INFO, line.get ( ) );
		base::copy_text ( shown->status, base::count_of ( shown->status ), L"Ready" );

	}

	void session::apply ( const ui::preferences& settings ) {

		sync_api ( settings );

		if ( active && ( settings.mode != mode || settings.adapter != adapter || settings.kernel != kernel || algorithm_of ( settings ) != algorithm || ( algorithm == chain::ALGORITHM_RANDOMX && settings.variant != variant ) ) ) {

			start ( settings );

			return;

		}

		if ( active ) {

			engine.apply ( limits_for ( settings ) );
			shown->extensions = engine.uses_extensions ( );

		}

		describe ( settings );

	}

	void session::log ( uint8_t level, const wchar_t* text ) {

		ui::snapshot& view = *shown;

		if ( view.log_count == ui::log_room ) {

			base::move ( view.logs, view.logs + 1, sizeof ( ui::log_row ) * ( ui::log_room - 1 ) );
			view.log_count--;

		}

		ui::log_row& row = view.logs [ view.log_count++ ];

		row.time = base::local_time ( );
		row.level = level;
		base::copy_text ( row.text, base::count_of ( row.text ), text );
		view.log_serial++;
		journal ( level, text );

	}

	void session::clear_log ( ) {

		shown->log_count = 0;
		shown->log_serial++;

	}

	void session::drain_pool ( ) {

		pool::event item;
		ui::snapshot& view = *shown;

		while ( client.next_event ( item ) ) {

			uint8_t level = ui::LEVEL_INFO;

			if ( item.kind == pool::EVENT_WARNING || item.kind == pool::EVENT_REJECTED )
				level = ui::LEVEL_WARNING;
			else if ( item.kind == pool::EVENT_ERROR )
				level = ui::LEVEL_ERROR;
			else if ( item.kind == pool::EVENT_ACCEPTED )
				level = ui::LEVEL_GOOD;

			if ( item.kind == pool::EVENT_ACCEPTED || item.kind == pool::EVENT_REJECTED ) {

				if ( view.share_count == ui::share_room ) {

					base::move ( view.shares, view.shares + 1, sizeof ( ui::share_row ) * ( ui::share_room - 1 ) );
					view.share_count--;

				}

				ui::share_row& row = view.shares [ view.share_count++ ];

				row.time = item.time;
				row.accepted = item.kind == pool::EVENT_ACCEPTED;
				row.difficulty = item.difficulty;
				row.latency = item.latency;
				row.nonce = item.nonce;
				row.reason [ 0 ] = 0;

				if ( !row.accepted ) {

					const wchar_t* reason = item.text;

					for ( int32_t i = 0; item.text [ i ]; i++ ) {

						if ( item.text [ i ] == L':' && item.text [ i + 1 ] == L' ' )
							reason = item.text + i + 2;

					}

					base::copy_text ( row.reason, base::count_of ( row.reason ), reason == item.text ? L"Rejected" : reason );

				}

				view.share_serial++;

				book.add_share ( today_number ( ), row.accepted, view.coin, view.share_value );

				if ( row.accepted ) {

					reject_streak = 0;
					view.run_earned += view.share_value;

					continue;

				}

				if ( ++reject_streak == 3 ) {

					base::text line;

					line.add ( L"The pool turned down three shares in a row: " ).add ( row.reason );
					announce ( L"Shares are being rejected", line.get ( ) );

				}

			}

			log ( level, item.text );
			view.logs [ view.log_count - 1 ].time = item.time;

			if ( base::starts_with ( item.text, L"Switching to the backup pool" ) )
				announce ( L"Switched to the backup pool", item.text );
			else if ( base::starts_with ( item.text, L"The node accepted a block" ) )
				announce ( L"Block found", item.text );
			else if ( item.kind == pool::EVENT_ERROR )
				announce ( L"The pool needs attention", item.text );

		}

	}

	void session::finish_genesis ( ) {

		ui::snapshot& view = *shown;
		uint64_t space = mine::nonce_space - genesis.start ( );

		if ( genesis.solved ( ) ) {

			sample ( base::seconds ( ) );

			double ran = base::seconds ( ) - started;
			chain::header real;

			real_genesis ( algorithm, real );
			view.solved = true;
			view.nonce = genesis.nonce ( );
			view.progress = base::min ( static_cast < double > ( genesis.nonce ( ) - genesis.start ( ) + 1 ) / static_cast < double > ( space ), 1.0 );
			view.remaining = 0.0;
			base::copy ( view.block_hash, genesis.digest ( ), 32 );
			engine.stop ( );
			active = false;
			view.running = false;
			view.rate = 0.0;
			view.gpu_state = ui::GPU_IDLE;

			base::text line;

			line.add ( L"Found nonce " ).number ( view.nonce ).add ( L" in " ).duration ( ran );

			if ( view.nonce != real.nonce ( ) )
				line.add ( L", which differs from the real block" );
			else
				line.add ( algorithm == chain::ALGORITHM_SCRYPT ? L", the same nonce as Litecoin's first block on October 7, 2011" : L", the same nonce Satoshi found on January 3, 2009" );
			log ( view.nonce == real.nonce ( ) ? ui::LEVEL_GOOD : ui::LEVEL_ERROR, line.get ( ) );
			announce ( L"Genesis block found", line.get ( ) );
			base::copy_text ( view.status, base::count_of ( view.status ), L"Genesis block found" );

			return;

		}

		if ( engine.hashes ( ) >= space ) {

			engine.stop ( );
			active = false;
			view.running = false;
			log ( ui::LEVEL_WARNING, L"No nonce in the range met the genesis target" );
			base::copy_text ( view.status, base::count_of ( view.status ), L"Search finished without a match" );

		}

	}

	void session::sample ( double now ) {

		ui::snapshot& view = *shown;
		uint64_t total = engine.hashes ( );

		view.uptime = now - started;
		view.hashes = total;

		if ( !live_count || now - live_times [ ( live_count - 1 ) % 64 ] >= 0.03 ) {

			live_times [ live_count % 64 ] = now;
			live_totals [ live_count % 64 ] = total;
			live_count++;

		}

		for ( int32_t back = base::min ( live_count, 64 ); back > 0; back-- ) {

			int32_t slot = ( live_count - back ) % 64;
			double span = now - live_times [ slot ];

			if ( span > 1.05 || span < 0.25 )
				continue;

			view.live_rate = static_cast < double > ( total - live_totals [ slot ] ) / span;

			break;

		}

		if ( now - last_sample >= 1.0 ) {

			double span = now - last_sample;
			float rate = static_cast < float > ( static_cast < double > ( total - last_total ) / span );

			if ( view.history_count == ui::history_room ) {

				base::move ( view.history, view.history + 1, sizeof ( float ) * ( ui::history_room - 1 ) );
				view.history_count--;
				view.history_first++;

			}

			view.history [ view.history_count++ ] = rate;
			view.history_serial++;
			view.history_time = now;
			ledger_carry += span;

			uint32_t whole = static_cast < uint32_t > ( ledger_carry );

			ledger_carry -= whole;

			if ( !paused && whole )
				book.add_time ( today_number ( ), whole, total - last_total );

			double cpu_total = 0.0;

			for ( int32_t i = 0; i < view.lane_count && i < ui::lane_room; i++ ) {

				uint64_t done = engine.lane_hashes ( static_cast < uint32_t > ( i ) );

				view.lane_rates [ i ] = static_cast < float > ( static_cast < double > ( done - lane_last [ i ] ) / span );
				cpu_total += view.lane_rates [ i ];
				lane_last [ i ] = done;

			}

			uint64_t gpu_total = engine.gpu_hashes ( );

			view.cpu_rate = cpu_total;
			view.gpu_rate = static_cast < double > ( gpu_total - last_gpu ) / span;
			last_gpu = gpu_total;

			for ( uint32_t i = 0; i < engine.gpu_count ( ) && i < static_cast < uint32_t > ( ui::gpu_room ); i++ ) {

				uint64_t done = engine.gpu_hashes ( i );

				view.gpu_rates [ i ] = static_cast < float > ( static_cast < double > ( done - gpu_last [ i ] ) / span );
				gpu_last [ i ] = done;

			}
			last_total = total;
			last_sample = now;

			int32_t window = base::min ( view.history_count, 5 );
			double sum = 0.0;

			for ( int32_t i = view.history_count - window; i < view.history_count; i++ )
				sum += view.history [ i ];

			view.rate = window ? sum / window : 0.0;
			view.peak = base::max ( view.peak, static_cast < double > ( rate ) );

		}

		view.gpu_hashes = engine.gpu_hashes ( );
		view.cpu_hashes = 0;

		for ( uint32_t i = 0; i < engine.lanes ( ); i++ )
			view.cpu_hashes += engine.lane_hashes ( i );

		view.average = view.uptime > 0.5 ? static_cast < double > ( total ) / view.uptime : 0.0;
		view.best = engine.best ( ) == ~0ull ? 0.0 : chain::difficulty_of ( engine.best ( ), algorithm );
		view.faults = engine.gpu_errors ( );
		view.gpu_state = gpu_phase_of ( engine.gpu_status ( ) );
		view.active_threads = engine.working_threads ( );
		view.large_pages = engine.dataset_large ( );
		view.gpu_count = static_cast < int32_t > ( base::min ( engine.gpu_count ( ), static_cast < uint32_t > ( ui::gpu_room ) ) );
		base::copy_text ( view.gpu_name, base::count_of ( view.gpu_name ), engine.gpu_name ( ) );

		for ( int32_t i = 0; i < view.gpu_count; i++ ) {

			view.gpu_states [ i ] = gpu_phase_of ( engine.gpu_status ( static_cast < uint32_t > ( i ) ) );
			view.gpu_heat [ i ] = engine.gpu_temperature ( static_cast < uint32_t > ( i ) );

			if ( view.gpu_states [ i ] == ui::GPU_COOLING && gpu_phase_seen [ i ] != ui::GPU_COOLING ) {

				base::text line;

				line.add ( engine.gpu_name ( static_cast < uint32_t > ( i ) ) ).add ( L" reached its temperature limit and rests until it cools four degrees" );
				log ( ui::LEVEL_WARNING, line.get ( ) );
				announce ( L"Graphics card is resting", line.get ( ) );

			}

			gpu_phase_seen [ i ] = view.gpu_states [ i ];

			uint32_t restarts = engine.gpu_restarts ( static_cast < uint32_t > ( i ) );
			uint32_t trims = engine.gpu_trimmed ( static_cast < uint32_t > ( i ) );

			if ( restarts > gpu_restarts_seen [ i ] ) {

				base::text line;

				gpu_restarts_seen [ i ] = restarts;
				line.add ( engine.gpu_name ( static_cast < uint32_t > ( i ) ) ).add ( L" stopped responding, so Alloy is restarting it" );
				log ( ui::LEVEL_WARNING, line.get ( ) );

			}

			if ( trims > gpu_trims_seen [ i ] ) {

				base::text line;

				gpu_trims_seen [ i ] = trims;
				line.add ( L"Halved the batch on " ).add ( engine.gpu_name ( static_cast < uint32_t > ( i ) ) ).add ( L" so each one stays well under Windows' two-second GPU timeout" );
				log ( ui::LEVEL_WARNING, line.get ( ) );

			}
			base::copy_text ( view.gpu_names [ i ], base::count_of ( view.gpu_names [ i ] ), engine.gpu_name ( static_cast < uint32_t > ( i ) ) );

		}

		uint8_t dataset = algorithm == chain::ALGORITHM_RANDOMX ? dataset_phase_of ( engine.dataset_status ( ) ) : static_cast < uint8_t > ( ui::DATASET_NONE );

		if ( dataset != view.dataset ) {

			base::text line;

			if ( dataset == ui::DATASET_BUILDING )
				line.add ( L"Building the RandomX dataset" );
			else if ( dataset == ui::DATASET_FULL ) {

				line.add ( L"The 2 GB RandomX dataset is ready after " ).decimal ( engine.dataset_seconds ( ), 1 ).add ( L" s" );
				line.add ( engine.dataset_large ( ) ? L", in large pages" : L". Alloy.exe --allow-large-pages makes RandomX faster" );

			}
			else if ( dataset == ui::DATASET_LIGHT )
				line.add ( L"RandomX runs in light mode from the 256 MB cache" );
			else if ( dataset == ui::DATASET_PART ) {

				line.add ( L"RandomX keeps " ).number ( engine.dataset_megabytes ( ) ).add ( L" MB of the dataset in memory and works out the rest from the cache, ready after " ).decimal ( engine.dataset_seconds ( ), 1 ).add ( L" s" );
				line.add ( engine.dataset_large ( ) ? L", in large pages" : L"" );

			}
			else if ( dataset == ui::DATASET_SHORT ) {

				line.add ( L"Not enough free memory for " );

				if ( engine.dataset_megabytes ( ) >= ui::dataset_full )
					line.add ( L"the 2 GB dataset" );
				else
					line.add ( L"" ).number ( engine.dataset_megabytes ( ) ).add ( L" MB of the dataset" );

				line.add ( L", so RandomX runs in light mode" );

			}

			if ( line.size ( ) )
				log ( dataset == ui::DATASET_SHORT ? ui::LEVEL_WARNING : ui::LEVEL_INFO, line.get ( ) );

			view.dataset = dataset;

		}

		view.dataset_progress = engine.dataset_progress ( );
		view.memory_free = static_cast < uint32_t > ( base::available_memory ( ) >> 20 );
		view.dataset_megabytes = dataset == ui::DATASET_FULL || dataset == ui::DATASET_PART ? engine.dataset_megabytes ( ) : 0u;
		view.dataset_seconds = engine.dataset_seconds ( );

		if ( mode == ui::MODE_POOL ) {

			pool::status link;

			client.read ( link );
			view.link = phase_of ( link.state );
			view.difficulty = link.difficulty;
			view.latency = link.latency;
			view.accepted = link.accepted;
			view.rejected = link.rejected;
			view.stale = link.stale;
			view.has_job = link.has_job;
			view.version = link.version;
			view.bits = link.bits;
			view.time = link.time;
			view.branches = link.branches;
			view.coinbase_size = link.coinbase_size;
			pool_reward = link.reward;
			view.height = link.height;
			view.on_backup = link.on_backup;

			if ( link.pin_serial != pin_serial ) {

				pin_serial = link.pin_serial;
				pin_backup = link.learned_backup;
				base::copy_text ( pin_learned, sizeof ( pin_learned ), link.learned_pin );
				pin_waiting = true;

			}

			view.nicehash = link.nicehash;
			view.blob_size = link.blob_size;

			if ( link.has_job && algorithm == chain::ALGORITHM_RANDOMX )
				view.variant = link.variant;

			base::copy ( view.seed, link.seed, 32 );
			base::copy ( view.previous, link.previous, 32 );
			base::copy_text ( view.job, base::count_of ( view.job ), link.job );

		}

		if ( mode == ui::MODE_GENESIS ) {

			double space = static_cast < double > ( mine::nonce_space - genesis.start ( ) );

			view.from = genesis.start ( );
			view.progress = base::min ( static_cast < double > ( total ) / space, 1.0 );
			view.remaining = view.rate > 0.0 ? ( space - static_cast < double > ( total ) ) / view.rate : 0.0;

		}

	}

	void session::describe ( const ui::preferences& settings ) {

		ui::snapshot& view = *shown;
		base::text line;

		const wchar_t* address = settings.pool ( ).address;

		if ( !active )
			line.add ( view.solved ? L"Genesis block found" : L"Ready" );
		else if ( paused )
			line.add ( view.pause_reason );
		else if ( view.dataset == ui::DATASET_BUILDING )
			line.add ( L"Building the RandomX dataset, " ).number ( view.dataset_progress / 10 ).put ( L'%' );
		else if ( algorithm == chain::ALGORITHM_RANDOMX && !settings.cpu )
			line.add ( L"RandomX needs the CPU turned on" );
		else if ( mode == ui::MODE_BENCHMARK )
			line.add ( L"Benchmarking " ).add ( chain::algorithm_name ( algorithm ) );
		else if ( mode == ui::MODE_GENESIS )
			line.add ( L"Searching for the genesis block's nonce" );
		else if ( view.link == ui::PHASE_READY )
			line.add ( L"Mining " ).add ( chain::algorithm_name ( algorithm ) ).add ( L" on " ).add ( address );
		else if ( view.link == ui::PHASE_SIGNING_IN )
			line.add ( L"Signing in to " ).add ( address );
		else if ( view.link == ui::PHASE_RETRYING )
			line.add ( L"Waiting to reconnect to " ).add ( address );
		else if ( view.link == ui::PHASE_REFUSED )
			line.add ( L"The pool refused the worker" );
		else
			line.add ( L"Connecting to " ).add ( address );

		base::copy_text ( view.status, base::count_of ( view.status ), line.get ( ) );

	}

	bool session::tick ( double now, const ui::preferences& settings ) {

		uint64_t logs = shown->log_serial;
		uint64_t shares = shown->share_serial;
		bool changed = false;

		if ( checker && base::thread_finished ( checker ) )
			finish_check ( settings );

		drain_pool ( );

		if ( active ) {

			const wchar_t* reason = pause_reason ( now, settings );

			if ( ( reason != nullptr ) != paused || ( reason && !base::same_text ( reason, shown->pause_reason ) ) ) {

				bool was = paused;

				paused = reason != nullptr;
				engine.set_paused ( paused );
				shown->paused = paused;
				base::copy_text ( shown->pause_reason, base::count_of ( shown->pause_reason ), reason ? reason : L"" );

				if ( paused || was )
					log ( ui::LEVEL_INFO, paused ? reason : L"Resumed" );

				changed = true;

			}

			if ( now - last_tick >= 0.25 ) {

				sample ( now );
				last_tick = now;
				changed = true;

			}

			if ( mode == ui::MODE_GENESIS )
				finish_genesis ( );

			describe ( settings );

		}

		if ( now - accounted >= 1.0 ) {

			account ( now, settings );
			changed = true;

		}

		switch ( api.take_command ( ) ) {

			case API_PAUSE:
				if ( active ) {

					held = true;
					log ( ui::LEVEL_INFO, L"Paused from the API" );

				}

				break;

			case API_RESUME:
				if ( held )
					log ( ui::LEVEL_INFO, L"Resumed from the API" );

				held = false;
				break;

			case API_START:
				if ( !active ) {

					log ( ui::LEVEL_INFO, L"Started from the API" );
					start ( settings );

				}

				break;

			case API_STOP:
				if ( active ) {

					log ( ui::LEVEL_INFO, L"Stopped from the API" );
					stop ( );

				}

				break;

			default:
				break;

		}

		if ( api.running ( ) && now - published >= 1.0 ) {

			published = now;
			publish ( settings );

		}

		return changed || logs != shown->log_serial || shares != shown->share_serial;

	}

	bool within_schedule ( const base::calendar& now, uint32_t from, uint32_t to, uint32_t days ) {

		uint32_t hour = now.hour;
		bool overnight = from % 24 > to % 24;
		bool late = overnight && hour < to % 24;
		uint32_t weekday = ( now.weekday + 6u - ( late ? 1u : 0u ) ) % 7u;
		bool in_hours = from % 24 == to % 24 || ( overnight ? hour >= from % 24 || hour < to % 24 : hour >= from % 24 && hour < to % 24 );

		return in_hours && ( days >> weekday & 1u );

	}

	bool session::inside_schedule ( const ui::preferences& settings ) const {

		return within_schedule ( base::local_calendar ( ), settings.schedule_from, settings.schedule_to, settings.schedule_days );

	}

	const wchar_t* session::pause_reason ( double now, const ui::preferences& settings ) {

		if ( held )
			return L"Paused by you";

		if ( sleeping )
			return L"Paused while the computer sleeps";

		if ( settings.schedule && !inside_schedule ( settings ) )
			return L"Paused outside the mining schedule";

		if ( ( settings.pause_fullscreen || settings.pause_apps [ 0 ] ) && now - apps_checked >= 2.0 ) {

			wchar_t which [ 64 ] = { };

			apps_checked = now;
			blocking [ 0 ] = 0;

			if ( settings.pause_apps [ 0 ] && base::find_process ( settings.pause_apps, which, base::count_of ( which ) ) ) {

				base::writer < wchar_t, 96 > text;

				text.add ( L"Paused while " ).add ( which ).add ( L" runs" );
				base::copy_text ( blocking, base::count_of ( blocking ), text.get ( ) );

			} else if ( settings.pause_fullscreen && base::fullscreen_busy ( ) )
				base::copy_text ( blocking, base::count_of ( blocking ), L"Paused while something runs full screen" );

		}

		if ( ( settings.pause_fullscreen || settings.pause_apps [ 0 ] ) && blocking [ 0 ] )
			return blocking;

		if ( settings.pause_active && base::idle_seconds ( ) < static_cast < double > ( settings.idle_minutes ) * 60.0 )
			return L"Paused while the computer is in use";

		if ( settings.pause_battery && base::on_battery ( ) )
			return L"Paused on battery power";

		return nullptr;

	}

	void session::set_sleeping ( bool value ) {

		if ( sleeping == value )
			return;

		sleeping = value;

		if ( !value )
			network_changed ( );

	}

	void session::set_held ( bool value ) {

		held = value;

	}

	void session::start_lookups ( ) {

		book.load ( );
		online.start ( );
		lookups_on = true;

	}

	void session::save_history ( ) {

		if ( lookups_on && book.dirty ( ) )
			book.save ( );

	}

	void session::account ( double now, const ui::preferences& settings ) {

		ui::snapshot& view = *shown;
		uint8_t kind = active ? algorithm : algorithm_of ( settings );
		uint8_t flavour = active ? view.variant : settings.variant;
		uint8_t current_coin = coin_in_use ( active ? coin : settings.coin ( ), kind, flavour );
		lookup_result found;
		lookup_request wanted;
		const ui::pool_profile& profile = settings.pool ( );

		accounted = now;
		wanted.algorithm = kind;
		wanted.variant = flavour;
		base::copy_text ( wanted.pool, base::count_of ( wanted.pool ), view.on_backup ? profile.backup : profile.address );
		base::copy_text ( wanted.worker, base::count_of ( wanted.worker ), profile.worker );
		wanted.balances = settings.lookups;
		wanted.prices = settings.lookups && settings.show_fiat;

		if ( lookups_on ) {

			online.ask ( wanted );
			online.read ( found );

		}

		double difficulty = 0.0;
		double reward = 0.0;
		uint64_t height = view.height;
		bool known = false;

		if ( chain::uses_header ( kind ) && view.bits && pool_reward ) {

			difficulty = chain::difficulty_of_bits ( view.bits );
			reward = static_cast < double > ( pool_reward ) / 1e8;
			known = difficulty > 0.0;

		} else if ( kind == chain::ALGORITHM_RANDOMX && flavour <= 1 && found.network ) {

			difficulty = found.difficulty;
			reward = found.reward;
			height = found.height;
			known = difficulty > 0.0;

		}

		double block = chain::block_hashes ( kind, difficulty );
		double spacing = chain::coin_of ( current_coin ).block_seconds;
		double rate = view.running ? view.rate : 0.0;
		double share = view.difficulty > 0.0 ? chain::share_hashes ( kind, view.difficulty ) : 0.0;

		view.coin = current_coin;
		view.network_known = known;
		view.network_difficulty = difficulty;
		view.network_height = height;
		view.block_reward = reward;
		view.network_rate = known ? block / spacing : 0.0;
		view.share_value = known && share > 0.0 ? reward * share / block : 0.0;
		view.per_day = known && rate > 0.0 ? reward * rate * 86400.0 / block : 0.0;
		view.block_seconds = known && rate > 0.0 ? block / rate : 0.0;
		view.shares_per_hour = share > 0.0 && rate > 0.0 ? rate * 3600.0 / share : 0.0;
		view.today = today_number ( );

		int32_t first = base::max ( 0, book.count ( ) - ui::day_view_room );

		view.day_count = 0;

		for ( int32_t i = book.count ( ) - 1; i >= first; i-- ) {

			const day_record& row = book.at ( i );
			ui::day_row& slot = view.days [ view.day_count++ ];

			slot.day = row.day;
			slot.hashes = row.hashes;
			slot.accepted = row.accepted;
			slot.rejected = row.rejected;
			slot.seconds = row.seconds;
			base::copy ( slot.earned, row.earned, sizeof ( slot.earned ) );

		}

		const day_record& total = book.lifetime ( );

		view.lifetime.hashes = total.hashes;
		view.lifetime.accepted = total.accepted;
		view.lifetime.rejected = total.rejected;
		view.lifetime.seconds = total.seconds;
		base::copy ( view.lifetime.earned, total.earned, sizeof ( view.lifetime.earned ) );

		view.balance_known = found.balance;
		view.balance_due = found.due;
		view.balance_paid = found.paid;
		view.pool_rate = found.pool_rate;
		view.pool_best = found.best;
		view.pool_shares = found.pool_shares;
		base::copy_text ( view.balance_source, base::count_of ( view.balance_source ), found.source );
		base::copy_text ( view.lookup_note, base::count_of ( view.lookup_note ), found.note );

		view.price_known = settings.show_fiat && found.priced && found.prices [ current_coin ] [ settings.currency % CURRENCY_COUNT ] > 0.0;
		view.price = view.price_known ? found.prices [ current_coin ] [ settings.currency % CURRENCY_COUNT ] : 0.0;

		watched.configure ( settings.rigs, ui::rig_room );
		watched.read ( view.rigs, view.rig_count );

		if ( lookups_on && book.dirty ( ) && now - ledger_saved > 60.0 ) {

			ledger_saved = now;
			book.save ( );

		}

	}

	void session::announce ( const wchar_t* title, const wchar_t* text ) {

		notice& slot = notices [ notices_pushed++ % base::count_of ( notices ) ];

		base::copy_text ( slot.title, base::count_of ( slot.title ), title );
		base::copy_text ( slot.text, base::count_of ( slot.text ), text );

		if ( notices_pushed - notices_read > base::count_of ( notices ) )
			notices_read = notices_pushed - base::count_of ( notices );

	}

	bool session::next_notice ( notice& out ) {

		if ( notices_read >= notices_pushed )
			return false;

		out = notices [ notices_read++ % base::count_of ( notices ) ];

		return true;

	}

	void session::network_changed ( ) {

		if ( active && mode == ui::MODE_POOL )
			client.reconnect ( );

	}

	void session::sync_api ( const ui::preferences& settings ) {

		app::api_options wanted;

		wanted.port = static_cast < uint16_t > ( settings.api_port );
		wanted.everyone = settings.api_lan;
		wanted.control = settings.api_control;
		base::copy_text ( wanted.token, sizeof ( wanted.token ), settings.api_token );

		const app::api_options& current = api.options ( );
		bool same = settings.api_port == api_port && current.everyone == wanted.everyone && current.control == wanted.control && base::same_text ( current.token, wanted.token );

		if ( same )
			return;

		api.stop ( );
		api_port = settings.api_port;

		if ( api_port && !api.start ( wanted ) )
			log ( ui::LEVEL_WARNING, L"Couldn't open the stats API, the port is probably taken" );

		published = 0.0;

	}

	void session::publish ( const ui::preferences& settings ) {

		static const char* const modes [ ] = { "pool", "benchmark", "genesis" };
		static const char* const gpu_states [ ] = { "idle", "starting", "working", "failed", "unsupported", "cooling" };
		const ui::snapshot& view = *shown;
		base::line* out = new base::line;
		char text [ 512 ];

		auto quoted = [ & ] ( const wchar_t* value ) {

			base::narrow ( text, sizeof ( text ), value );
			base::add_json_string ( *out, text );

		};

		wchar_t computer [ 64 ] = { };

		base::computer_name ( computer, base::count_of ( computer ) );
		out->add ( "{\"version\":\"" ALLOY_VERSION_TEXT "\",\"name\":" );
		quoted ( computer );
		out->add ( ",\"algorithm\":" );
		quoted ( chain::algorithm_key ( view.running ? view.algorithm : settings.algorithm ) );
		uint8_t kind = view.running ? view.algorithm : settings.algorithm;

		out->add ( ",\"variant\":" );

		if ( kind == chain::ALGORITHM_RANDOMX )
			out->put ( '"' ).add ( randomx::variant_of ( view.variant ).key ).put ( '"' );
		else
			quoted ( chain::algorithm_key ( kind ) );

		out->add ( ",\"coin\":" );
		quoted ( chain::coin_of ( view.running ? view.coin : settings.coin ( ) ).symbol );

		out->add ( ",\"control\":" ).add ( settings.api_control ? "true" : "false" ).add ( ",\"pause_reason\":" );
		quoted ( view.paused ? view.pause_reason : L"" );
		out->add ( ",\"mode\":\"" ).add ( modes [ ( view.running ? view.mode : settings.mode ) % 3 ] );
		out->add ( "\",\"running\":" ).add ( view.running ? "true" : "false" ).add ( ",\"paused\":" ).add ( view.paused ? "true" : "false" );
		out->add ( ",\"uptime\":" ).decimal ( view.uptime, 1 ).add ( ",\"hashes\":" ).number ( view.hashes );
		out->add ( ",\"hashrate\":{\"total\":" ).decimal ( view.rate, 1 ).add ( ",\"average\":" ).decimal ( view.average, 1 ).add ( ",\"peak\":" ).decimal ( view.peak, 1 );
		out->add ( ",\"cpu\":" ).decimal ( view.cpu_rate, 1 ).add ( ",\"gpu\":" ).decimal ( view.gpu_rate, 1 ).add ( "},\"threads\":" ).number ( view.active_threads );
		out->add ( ",\"shares\":{\"accepted\":" ).number ( view.accepted ).add ( ",\"rejected\":" ).number ( view.rejected ).add ( ",\"stale\":" ).number ( view.stale ).put ( '}' );
		out->add ( ",\"difficulty\":" ).decimal ( view.difficulty, 4 ).add ( ",\"best\":" ).decimal ( view.best, 1 );
		out->add ( ",\"pool\":{\"address\":" );
		quoted ( view.on_backup ? settings.pool ( ).backup : settings.pool ( ).address );
		out->add ( ",\"backup\":" ).add ( view.on_backup ? "true" : "false" ).add ( ",\"connected\":" ).add ( view.link == ui::PHASE_READY ? "true" : "false" );
		out->add ( ",\"latency\":" ).decimal ( view.latency, 1 ).add ( ",\"height\":" ).number ( view.height ).add ( "},\"gpus\":[" );

		for ( int32_t i = 0; i < view.gpu_count; i++ ) {

			out->add ( i ? ",{\"name\":" : "{\"name\":" );
			quoted ( view.gpu_names [ i ] );
			out->add ( ",\"hashrate\":" ).decimal ( view.gpu_rates [ i ], 1 ).add ( ",\"temperature\":" ).decimal ( view.gpu_heat [ i ], 1 );
			out->add ( ",\"state\":\"" ).add ( gpu_states [ view.gpu_states [ i ] % 6 ] ).add ( "\"}" );

		}

		out->add ( "]}\n" );
		api.publish ( out->get ( ), out->size ( ) );
		delete out;

	}

}
