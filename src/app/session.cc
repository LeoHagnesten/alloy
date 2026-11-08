#include "app/session.hh"
#include "app/journal.hh"
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

	static void real_genesis ( uint8_t algorithm, chain::header& out ) {

		if ( algorithm == chain::ALGORITHM_SCRYPT )
			chain::litecoin_genesis ( out );
		else
			chain::genesis ( out );

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
		out.full_dataset = settings.full_dataset;
		out.algorithm = algorithm_of ( settings );

		if ( settings.adapter == ui::all_adapters && adapters > 1 ) {

			out.adapter_count = static_cast < uint32_t > ( base::min ( adapters, static_cast < int32_t > ( mine::max_gpus ) ) );

			for ( uint32_t i = 0; i < out.adapter_count; i++ )
				out.adapters [ i ] = i;

		} else {

			out.adapter_count = 1;
			out.adapters [ 0 ] = settings.adapter == ui::all_adapters ? 0 : settings.adapter;

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

			}

		}

		bool restrict = !report.sha_extensions || report.broken_sha_cards || report.broken_scrypt_cards;

		portable_only = !report.sha_extensions;
		broken_cards [ chain::ALGORITHM_SHA256D ] = report.broken_sha_cards;
		broken_cards [ chain::ALGORITHM_SCRYPT ] = report.broken_scrypt_cards;
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
		view.history_serial++;
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
		started = base::seconds ( );
		last_sample = started;
		last_tick = started;
		last_total = 0;
		last_gpu = 0;
		base::zero ( gpu_last, sizeof ( gpu_last ) );
		base::zero ( gpu_restarts_seen, sizeof ( gpu_restarts_seen ) );
		base::zero ( gpu_trims_seen, sizeof ( gpu_trims_seen ) );
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

				if ( row.accepted )
					continue;

			}

			log ( level, item.text );
			view.logs [ view.log_count - 1 ].time = item.time;

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

		if ( now - last_sample >= 1.0 ) {

			double span = now - last_sample;
			float rate = static_cast < float > ( static_cast < double > ( total - last_total ) / span );

			if ( view.history_count == ui::history_room ) {

				base::move ( view.history, view.history + 1, sizeof ( float ) * ( ui::history_room - 1 ) );
				view.history_count--;

			}

			view.history [ view.history_count++ ] = rate;
			view.history_serial++;

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
			else if ( dataset == ui::DATASET_SHORT )
				line.add ( L"Not enough free memory for the 2 GB dataset, so RandomX runs in light mode" );

			if ( line.size ( ) )
				log ( dataset == ui::DATASET_SHORT ? ui::LEVEL_WARNING : ui::LEVEL_INFO, line.get ( ) );

			view.dataset = dataset;

		}

		view.dataset_progress = engine.dataset_progress ( );
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

			const wchar_t* reason = nullptr;

			if ( settings.pause_active && base::idle_seconds ( ) < static_cast < double > ( settings.idle_minutes ) * 60.0 )
				reason = L"Paused while the computer is in use";
			else if ( settings.pause_battery && base::on_battery ( ) )
				reason = L"Paused on battery power";

			if ( ( reason != nullptr ) != paused ) {

				paused = reason != nullptr;
				engine.set_paused ( paused );
				shown->paused = paused;
				base::copy_text ( shown->pause_reason, base::count_of ( shown->pause_reason ), reason ? reason : L"" );
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

		if ( api.running ( ) && now - published >= 1.0 ) {

			published = now;
			publish ( settings );

		}

		return changed || logs != shown->log_serial || shares != shown->share_serial;

	}

	void session::sync_api ( const ui::preferences& settings ) {

		if ( settings.api_port == api_port )
			return;

		api.stop ( );
		api_port = settings.api_port;

		if ( api_port && !api.start ( static_cast < uint16_t > ( api_port ) ) )
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

		out->add ( "{\"version\":\"" ALLOY_VERSION_TEXT "\",\"algorithm\":" );
		quoted ( chain::algorithm_key ( view.running ? view.algorithm : settings.algorithm ) );
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
