#include "app/commands.hh"
#include "app/journal.hh"
#include "app/self_test.hh"
#include "app/tune.hh"
#include "version.hh"
#include "app/session.hh"
#include "app/settings.hh"
#include "pool/test_pool.hh"
#include "net/socket.hh"
#include "base/system/clock.hh"
#include "base/system/console.hh"
#include "base/strings.hh"
#include "base/text.hh"

#ifdef ALLOY_DEBUG

namespace tests {

	uint32_t run_all ( const wchar_t* only );

}

#endif

namespace app {

	struct command_line {

		const wchar_t* action = nullptr;
		const wchar_t* algorithm = L"sha256d";
		const wchar_t* target = L"";
		const wchar_t* user = nullptr;
		const wchar_t* pass = nullptr;
		uint32_t seconds = 10;
		bool timed = false;
		uint32_t from = 0;
		uint32_t port = 3333;
		double difficulty = 0.01;
		uint32_t job_seconds = 30;
		uint32_t size = 256;
		bool stills = false;
		bool http = false;
		ui::preferences settings;

	};

	static const wchar_t* const actions [ ] = { L"--bench", L"--genesis", L"--mine", L"--test-pool", L"--allow-large-pages", L"--help", L"--test", L"--capture", L"--icon", L"--logo", L"--smoke-window", L"--tls-check", L"--self-test", L"--crash-test", L"--tune" };

	static bool window_option ( const wchar_t* item, bool& takes ) {

		takes = base::same_text ( item, L"--profile" ) || base::same_text ( item, L"--set" );

		return takes || base::same_text ( item, L"--minimized" );

	}

	bool is_command ( wchar_t** arguments, int32_t count ) {

		for ( int32_t i = 1; arguments && i < count; i++ ) {

			bool takes = false;

			if ( arguments [ i ] [ 0 ] != L'-' || arguments [ i ] [ 1 ] != L'-' )
				continue;

			if ( !window_option ( arguments [ i ], takes ) )
				return true;

			i += takes ? 1 : 0;

		}

		return false;

	}

	void read_launch ( wchar_t** arguments, int32_t count, launch_options& out ) {

		for ( int32_t i = 1; arguments && i < count; i++ ) {

			const wchar_t* item = arguments [ i ];
			const wchar_t* value = i + 1 < count ? arguments [ i + 1 ] : nullptr;

			if ( base::same_text ( item, L"--minimized" ) )
				out.minimized = true;
			else if ( base::same_text ( item, L"--profile" ) && value ) {

				out.profile = value;
				i++;

			} else if ( base::same_text ( item, L"--set" ) && value ) {

				if ( out.set_count < base::count_of ( out.sets ) )
					out.sets [ out.set_count++ ] = value;

				i++;

			}

		}

	}

	void apply_launch ( const launch_options& options, ui::preferences& settings, uint32_t threads ) {

		if ( options.profile && !load_profile ( options.profile, settings, threads ) ) {

			base::text note;

			note.add ( L"alloy: there's no profile called " ).add ( options.profile );
			base::print_line ( note.get ( ) );

		}

		for ( int32_t i = 0; i < options.set_count; i++ ) {

			char line [ 640 ];

			base::narrow ( line, sizeof ( line ), options.sets [ i ] );

			if ( !apply_setting ( settings, line, threads ) ) {

				base::text note;

				note.add ( L"alloy: --set wants key=value, like --set randomx.threads=8" );
				base::print_line ( note.get ( ) );

			}

		}

	}

	static void usage ( ) {

		base::print_line ( L"Alloy " ALLOY_VERSION_TEXT L", a SHA-256d, scrypt, RandomX and kHeavyHash miner written from scratch" );
		base::print_line ( L"" );
		base::print_line ( L"  Alloy.exe                                   open the window" );
		base::print_line ( L"  Alloy.exe --bench [algorithm] [--seconds n] measure the hashrate" );
		base::print_line ( L"  Alloy.exe --genesis [--from nonce]          rediscover the Bitcoin genesis block's nonce" );
		base::print_line ( L"  Alloy.exe --mine host:port [--user name] [--pass word] [--seconds n]" );
		base::print_line ( L"  Alloy.exe --mine http://127.0.0.1:8332 --user address   mine solo against your own node" );
		base::print_line ( L"  Alloy.exe --mine stratum2+tcp://host:port/authority_key   mine on a Stratum V2 pool" );
		base::print_line ( L"  Alloy.exe --test-pool [--port n] [--difficulty d] [--job-seconds n] [--seconds n]" );
		base::print_line ( L"  Alloy.exe --allow-large-pages                let RandomX use large pages (run as administrator once)" );
		base::print_line ( L"  Alloy.exe --self-test                        check every hash kernel and graphics card against known answers" );
		base::print_line ( L"" );
		base::print_line ( L"  Algorithm --algorithm sha256d|scrypt|randomx|kheavyhash  --dataset full|light" );
		base::print_line ( L"  Devices   --threads n  --no-cpu  --gpu  --no-gpu  --adapter n" );
		base::print_line ( L"  Limits    --cpu-limit percent  --gpu-limit percent  --intensity 16-26" );
		base::print_line ( L"  Kernel    --kernel auto|extensions|portable  --priority idle|low|normal" );
		base::print_line ( L"  Settings  --profile name  --set key=value (any key from settings.ini, repeatable)" );
		base::print_line ( L"" );
		base::print_line ( L"  Alloy.exe --profile name --set randomx.threads=8   open the window with a profile and changes" );
		base::print_line ( L"  Alloy.exe --tune [algorithm]                 find the fastest threads and intensity and save them" );
#ifdef ALLOY_DEBUG
		base::print_line ( L"" );
		base::print_line ( L"  Alloy.exe --test | --capture <folder> [--stills] | --smoke-window [shot.png]" );
		base::print_line ( L"  Alloy.exe --icon <file.ico> | --logo <file.png> [--size n]" );
#endif

	}

	static void print_logs ( const ui::snapshot& view, uint64_t& printed ) {

		uint64_t first = view.log_serial - static_cast < uint64_t > ( view.log_count );

		if ( printed < first )
			printed = first;

		for ( ; printed < view.log_serial; printed++ ) {

			const ui::log_row& row = view.logs [ static_cast < int32_t > ( printed - first ) ];
			base::text line;

			line.add ( L"  " ).clock ( row.time.hour, row.time.minute, row.time.second ).add ( L"  " ).add ( row.text );
			base::print_line ( line.get ( ) );

		}

	}

	static void print_trial ( const trial& step, void* context ) {

		base::text note;

		note.add ( L"  " );

		if ( step.threads )
			note.number ( step.threads ).add ( step.threads == 1 ? L" thread" : L" threads" );
		else
			note.add ( L"no CPU" );

		if ( step.gpu )
			note.add ( L" + GPU at intensity " ).number ( step.intensity );

		note.add ( L":  " ).scaled ( step.rate, L"H/s" );
		base::print_line ( note.get ( ) );

	}

	static uint32_t tune_command ( command_line& line ) {

		tune_result* result = new tune_result;
		base::text note;
		uint8_t algorithm = line.settings.algorithm;

		note.add ( L"Finding the fastest settings for " ).add ( chain::algorithm_name ( algorithm ) );
		base::print_line ( note.get ( ) );
		tune ( algorithm, line.settings, *result, nullptr, &print_trial, nullptr );
		note.clear ( );
		note.add ( L"Fastest: " ).number ( result->threads ).add ( L" threads" );

		if ( result->gpu )
			note.add ( L" with the GPU at intensity " ).number ( result->intensity );

		note.add ( L", " ).scaled ( result->rate, L"H/s" );
		base::print_line ( note.get ( ) );

		const wchar_t* wide_key = chain::algorithm_key ( algorithm );

		note.clear ( );
		note.add ( L"Keep it with: Alloy.exe --set " ).add ( wide_key ).add ( L".threads=" ).number ( result->threads ).add ( L" --set " ).add ( wide_key ).add ( L".gpu=" ).add ( result->gpu ? L"yes" : L"no" );

		if ( result->gpu )
			note.add ( L" --set " ).add ( wide_key ).add ( L".intensity=" ).number ( result->intensity );

		base::print_line ( note.get ( ) );
		base::print_line ( L"or press Find the fastest settings in the window's Settings page." );
		delete result;

		return 0;

	}

	static uint32_t bench ( command_line& line ) {

		session* run = new session;
		const ui::snapshot& view = run->view ( );
		base::text note;
		uint8_t algorithm = line.settings.algorithm;
		bool gpu = line.settings.gpu && algorithm != chain::ALGORITHM_RANDOMX;

		line.settings.mode = ui::MODE_BENCHMARK;
		run->start ( line.settings );
		note.add ( chain::algorithm_name ( algorithm ) );

		if ( line.settings.cpu ) {

			note.add ( L" on " ).number ( view.active_threads ).add ( view.active_threads == 1 ? L" thread" : L" threads" );

			if ( algorithm == chain::ALGORITHM_SHA256D )
				note.add ( view.extensions ? L" with SHA extensions" : L" with the portable kernel" );
			else if ( algorithm == chain::ALGORITHM_RANDOMX )
				note.add ( line.settings.full_dataset ? L" with the full dataset" : L" in light mode" );

		}

		if ( gpu )
			note.add ( line.settings.cpu ? L" plus the GPU" : L" on the GPU" );

		note.add ( L", " ).number ( line.seconds ).add ( L" seconds" );
		base::print_line ( note.get ( ) );

		if ( algorithm == chain::ALGORITHM_RANDOMX ) {

			uint8_t shown = ui::DATASET_NONE;
			double waited = base::seconds ( );

			while ( base::seconds ( ) - waited < 120.0 ) {

				run->tick ( base::seconds ( ), line.settings );

				if ( view.dataset != shown && view.dataset != ui::DATASET_BUILDING && view.dataset != ui::DATASET_NONE )
					break;

				base::sleep ( 20 );

			}

			double built = view.dataset_seconds;

			run->start ( line.settings );
			note.clear ( );
			note.add ( L"  dataset ready in " ).decimal ( built, 1 ).add ( L" s, " ).number ( view.active_threads ).add ( L" threads fit the processor's cache" );
			base::print_line ( note.get ( ) );

		}

		double began = base::seconds ( );
		uint64_t samples = view.history_serial;

		while ( base::seconds ( ) - began < line.seconds ) {

			run->tick ( base::seconds ( ), line.settings );

			if ( view.history_serial != samples && view.history_count ) {

				samples = view.history_serial;
				note.clear ( );
				note.add ( L"  " ).scaled ( view.history [ view.history_count - 1 ], L"H/s" );

				if ( gpu && line.settings.cpu )
					note.add ( L"   cpu " ).scaled ( view.cpu_rate, L"H/s" ).add ( L"   gpu " ).scaled ( view.gpu_rate, L"H/s" );

				base::print_line ( note.get ( ) );

			}

			base::sleep ( 20 );

		}

		run->tick ( base::seconds ( ), line.settings );

		double average = view.average;
		double per_thread = view.uptime > 0.0 && view.active_threads ? static_cast < double > ( view.cpu_hashes ) / view.uptime / view.active_threads : 0.0;

		note.clear ( );
		note.add ( L"average " ).scaled ( average, L"H/s" );

		if ( line.settings.cpu )
			note.add ( L", " ).scaled ( per_thread, L"H/s" ).add ( L" per thread" );

		base::print_line ( note.get ( ) );

		if ( gpu ) {

			note.clear ( );

			if ( view.gpu_state == ui::GPU_BROKEN )
				note.add ( L"gpu unavailable: " ).add ( view.gpu_name );
			else
				note.add ( L"gpu " ).add ( view.gpu_name ).add ( L", " ).scaled ( view.uptime > 0.0 ? view.gpu_hashes / view.uptime : 0.0, L"H/s" ).add ( L", " ).number ( view.faults ).add ( L" hardware errors" );

			base::print_line ( note.get ( ) );

		}

		run->stop ( );
		delete run;

		return 0;

	}

	static uint32_t genesis ( command_line& line ) {

		session* run = new session;
		const ui::snapshot& view = run->view ( );
		base::text note;

		bool litecoin = line.settings.algorithm == chain::ALGORITHM_SCRYPT;

		line.settings.mode = ui::MODE_GENESIS;
		run->set_genesis_start ( line.from );
		run->start ( line.settings );
		note.add ( litecoin ? L"Searching for the Litecoin genesis block's nonce with scrypt from " : L"Searching for the Bitcoin genesis block's nonce from " ).number ( line.from );
		base::print_line ( note.get ( ) );

		uint64_t samples = view.history_serial;

		while ( run->running ( ) ) {

			run->tick ( base::seconds ( ), line.settings );

			if ( view.history_serial != samples && run->running ( ) ) {

				samples = view.history_serial;
				note.clear ( );
				note.add ( L"  " ).decimal ( ( static_cast < double > ( line.from ) + view.hashes ) / 1e9, 2 ).add ( L" G nonces checked at " ).scaled ( view.rate, L"H/s" );
				base::print_line ( note.get ( ) );

			}

			base::sleep ( 20 );

		}

		if ( !view.solved ) {

			note.clear ( );
			note.add ( L"No nonce below 2^32 met the target from " ).number ( line.from );
			base::print_line ( note.get ( ) );
			delete run;

			return 1;

		}

		chain::header real;

		if ( litecoin )
			chain::litecoin_genesis ( real );
		else
			chain::genesis ( real );

		note.clear ( );
		note.add ( L"Found nonce " ).number ( view.nonce ).add ( L" in " ).decimal ( view.uptime, 1 ).add ( L" s" );
		base::print_line ( note.get ( ) );
		note.clear ( );
		note.add ( litecoin ? L"Proof of work " : L"Block hash " ).hex ( view.block_hash, 32, true );
		base::print_line ( note.get ( ) );

		if ( view.nonce != real.nonce ( ) )
			base::print_line ( L"That nonce differs from the real block's, so the hash code has a bug." );
		else
			base::print_line ( litecoin ? L"That's the nonce in Litecoin's first block, October 7, 2011." : L"That's the nonce Satoshi found on January 3, 2009." );

		uint32_t code = view.nonce == real.nonce ( ) ? 0 : 1;

		delete run;

		return code;

	}

	static uint32_t mine ( command_line& line ) {

		session* run = new session;
		const ui::snapshot& view = run->view ( );
		uint64_t printed = 0;

		line.settings.mode = ui::MODE_POOL;
		base::copy_text ( line.settings.pool ( ).address, base::count_of ( line.settings.pool ( ).address ), line.target );
		run->start ( line.settings );

		double began = base::seconds ( );
		double last_report = began;

		while ( !line.timed || base::seconds ( ) - began < line.seconds ) {

			run->tick ( base::seconds ( ), line.settings );
			print_logs ( view, printed );

			if ( base::seconds ( ) - last_report >= 10.0 ) {

				base::text note;

				last_report = base::seconds ( );
				note.add ( L"  " ).scaled ( view.rate, L"H/s" ).add ( L", " ).number ( view.accepted ).add ( L" accepted, " ).number ( view.rejected ).add ( L" rejected" );
				base::print_line ( note.get ( ) );

			}

			base::sleep ( 50 );

		}

		run->stop ( );
		print_logs ( view, printed );

		base::text note;

		note.add ( L"accepted " ).number ( view.accepted ).add ( L", rejected " ).number ( view.rejected ).add ( L", stale " ).number ( view.stale ).add ( L", average " ).scaled ( view.average, L"H/s" );
		base::print_line ( note.get ( ) );

		uint32_t code = view.accepted > 0 && view.rejected == 0 ? 0 : 1;

		delete run;

		return code;

	}

#ifdef ALLOY_DEBUG

	static uint32_t tls_check ( const wchar_t* target, bool http ) {

		char host [ 256 ];
		uint16_t port = 0;
		net::connection* link = new net::connection;
		base::text note;
		uint32_t code = 1;

		if ( !net::split_address ( target, host, sizeof ( host ), port ) )
			base::print_line ( L"tls-check: give it host:port" );
		else if ( !link->open ( host, port, 5000 ) )
			base::print_line ( L"tls-check: couldn't reach it" );
		else if ( !link->start_tls ( host, 8000 ) ) {

			note.add ( L"tls-check: " ).add ( link->tls_problem ( ) );
			base::print_line ( note.get ( ) );

		} else {

			base::line request;
			char reply [ 512 ];

			note.add ( L"handshake done with " ).add ( link->tls_protocol ( ) ).add ( L", " );

			if ( link->tls_trusted ( ) )
				note.add ( L"Windows trusts the certificate" );
			else
				note.add ( link->tls_trust_problem ( ) );

			note.add ( L", fingerprint " ).hex ( link->tls_fingerprint ( ), 32, false );
			base::print_line ( note.get ( ) );

			if ( http )
				request.add ( "HEAD / HTTP/1.1\r\nHost: " ).add ( host ).add ( "\r\nConnection: close\r\n\r\n" );
			else
				request.add ( "{\"id\":1,\"method\":\"mining.subscribe\",\"params\":[\"" ).add ( pool::agent ).add ( "\"]}\n" );

			int32_t size = link->send ( request.get ( ), request.size ( ) ) ? 0 : -1;
			double began = base::seconds ( );

			while ( size == 0 && base::seconds ( ) - began < 8.0 )
				size = link->read_line ( reply, sizeof ( reply ), 200 );

			if ( size > 0 ) {

				wchar_t wide [ 512 ];

				base::widen ( wide, 512, reply );
				note.clear ( );
				note.add ( L"first line back: " ).add ( wide );
				base::print_line ( note.get ( ) );
				code = 0;

			} else {

				note.clear ( );
				note.add ( L"tls-check: no reply over the encrypted link, read " ).number ( static_cast < uint64_t > ( size < 0 ? 999 : size ) ).add ( link->is_open ( ) ? L" open " : L" closed " ).hex32 ( static_cast < uint32_t > ( link->tls_status ( ) ) );
				base::print_line ( note.get ( ) );

			}

		}

		delete link;

		return code;

	}

#endif

	static uint32_t test_pool ( command_line& line ) {

		pool::server_settings config;
		pool::test_server* server = new pool::test_server;
		base::text note;

		config.algorithm = line.settings.algorithm;
		config.port = static_cast < uint16_t > ( line.port );
		config.difficulty = line.difficulty;
		config.job_seconds = base::max ( line.job_seconds, 1u );
		config.verbose = true;

		if ( !server->start ( config ) ) {

			note.add ( L"Couldn't listen on 127.0.0.1:" ).number ( line.port );
			base::print_line ( note.get ( ) );
			delete server;

			return 1;

		}

		note.add ( chain::algorithm_name ( config.algorithm ) ).add ( L" test pool listening on 127.0.0.1:" ).number ( server->port ( ) ).add ( L", share difficulty " ).decimal ( config.difficulty, 4 ).add ( L", new job every " ).number ( config.job_seconds ).add ( L" s" );
		base::print_line ( note.get ( ) );

		double began = base::seconds ( );

		while ( !line.timed || base::seconds ( ) - began < line.seconds )
			base::sleep ( 100 );

		note.clear ( );
		note.add ( L"accepted " ).number ( server->accepted ( ) ).add ( L", rejected " ).number ( server->rejected ( ) );
		base::print_line ( note.get ( ) );
		server->stop ( );
		delete server;

		return 0;

	}

	uint32_t run_command ( wchar_t** arguments, int32_t count ) {

		command_line* line = new command_line;
		base::processor cpu;
		bool threads_given = false;
		bool algorithm_given = false;
		launch_options launch;

		base::detect ( cpu );
		freeze_settings ( );
		line->settings.threads = cpu.threads;
		line->settings.gpu = false;

		for ( int32_t i = 1; i < count; i++ ) {

			const wchar_t* item = arguments [ i ];
			const wchar_t* value = i + 1 < count ? arguments [ i + 1 ] : L"";
			bool takes = value [ 0 ] && !( value [ 0 ] == L'-' && value [ 1 ] == L'-' );
			bool known = false;

			for ( const wchar_t* action : actions ) {

				if ( base::same_text ( item, action ) ) {

					line->action = action;
					known = true;

				}

			}

			if ( known ) {

				if ( takes && ( base::same_text ( item, L"--bench" ) || base::same_text ( item, L"--tune" ) ) ) {

					line->algorithm = value;
					algorithm_given = true;
					i++;

				} else if ( takes && ( base::same_text ( item, L"--mine" ) || base::same_text ( item, L"--test" ) || base::same_text ( item, L"--tls-check" ) || base::same_text ( item, L"--capture" ) || base::same_text ( item, L"--icon" ) || base::same_text ( item, L"--logo" ) || base::same_text ( item, L"--smoke-window" ) || base::same_text ( item, L"--crash-test" ) ) ) {

					line->target = value;
					i++;

				}

				continue;

			}

			if ( ( base::same_text ( item, L"--profile" ) || base::same_text ( item, L"--set" ) ) && takes ) {

				if ( base::same_text ( item, L"--profile" ) )
					launch.profile = value;
				else if ( launch.set_count < base::count_of ( launch.sets ) )
					launch.sets [ launch.set_count++ ] = value;

				i++;

				continue;

			}

			if ( base::same_text ( item, L"--gpu" ) )
				line->settings.gpu = true;
			else if ( base::same_text ( item, L"--stills" ) )
				line->stills = true;
			else if ( base::same_text ( item, L"--no-gpu" ) )
				line->settings.gpu = false;
			else if ( base::same_text ( item, L"--no-cpu" ) )
				line->settings.cpu = false;
			else if ( base::same_text ( item, L"--no-pin" ) )
				line->settings.pin_threads = false;
			else if ( base::same_text ( item, L"--http" ) )
				line->http = true;
			else if ( !takes ) {

				base::text note;

				note.add ( L"alloy: " ).add ( item ).add ( L" needs a value or isn't an option" );
				base::print_line ( note.get ( ) );
				delete line;

				return 2;

			} else {

				uint64_t number = base::read_number ( value );

				if ( base::same_text ( item, L"--seconds" ) ) {

					line->seconds = static_cast < uint32_t > ( base::max ( number, 1ull ) );
					line->timed = true;

				} else if ( base::same_text ( item, L"--threads" ) ) {

					line->settings.threads = static_cast < uint32_t > ( base::clamp ( number, 1ull, static_cast < uint64_t > ( cpu.threads ) ) );
					threads_given = true;

				}
				else if ( base::same_text ( item, L"--from" ) )
					line->from = static_cast < uint32_t > ( base::min ( number, 0xFFFFFFFFull ) );
				else if ( base::same_text ( item, L"--port" ) )
					line->port = static_cast < uint32_t > ( base::min ( number, 65535ull ) );
				else if ( base::same_text ( item, L"--job-seconds" ) )
					line->job_seconds = static_cast < uint32_t > ( number );
				else if ( base::same_text ( item, L"--size" ) )
					line->size = static_cast < uint32_t > ( base::clamp ( number, 16ull, 1024ull ) );
				else if ( base::same_text ( item, L"--difficulty" ) )
					line->difficulty = base::read_decimal ( value );
				else if ( base::same_text ( item, L"--cpu-limit" ) )
					line->settings.cpu_limit = static_cast < uint32_t > ( base::clamp ( number, 10ull, 100ull ) );
				else if ( base::same_text ( item, L"--gpu-limit" ) )
					line->settings.gpu_limit = static_cast < uint32_t > ( base::clamp ( number, 10ull, 100ull ) );
				else if ( base::same_text ( item, L"--intensity" ) )
					line->settings.intensity = static_cast < uint32_t > ( base::clamp ( number, 16ull, 26ull ) );
				else if ( base::same_text ( item, L"--adapter" ) )
					line->settings.adapter = static_cast < uint32_t > ( number );
				else if ( base::same_text ( item, L"--user" ) )
					line->user = value;
				else if ( base::same_text ( item, L"--pass" ) )
					line->pass = value;
				else if ( base::same_text ( item, L"--algorithm" ) ) {

					line->algorithm = value;
					algorithm_given = true;

				}
				else if ( base::same_text ( item, L"--dataset" ) )
					line->settings.full_dataset = !base::same_folded ( value, L"light" );
				else if ( base::same_text ( item, L"--kernel" ) )
					line->settings.kernel = base::same_text ( value, L"portable" ) ? 2 : ( base::same_text ( value, L"extensions" ) ? 1 : 0 );
				else if ( base::same_text ( item, L"--priority" ) )
					line->settings.priority = base::same_text ( value, L"idle" ) ? 0 : ( base::same_text ( value, L"normal" ) ? 2 : 1 );
				else {

					base::text note;

					note.add ( L"alloy: unknown option " ).add ( item );
					base::print_line ( note.get ( ) );
					delete line;

					return 2;

				}

				i++;

			}

		}

		uint8_t chosen = line->settings.algorithm;

		apply_launch ( launch, line->settings, cpu.threads );

		if ( !algorithm_given && ( launch.profile || launch.set_count ) )
			line->algorithm = chain::algorithm_key ( line->settings.algorithm );

		if ( !chain::read_algorithm ( line->algorithm, chosen ) ) {

			base::text note;

			note.add ( L"alloy: " ).add ( line->algorithm ).add ( L" isn't an algorithm Alloy knows. Try sha256d, scrypt, randomx or kheavyhash." );
			base::print_line ( note.get ( ) );
			delete line;

			return 2;

		}

		line->settings.algorithm = chosen;

		if ( line->user )
			base::copy_text ( line->settings.pool ( ).worker, base::count_of ( line->settings.pool ( ).worker ), line->user );

		if ( line->pass )
			base::copy_text ( line->settings.pool ( ).password, base::count_of ( line->settings.pool ( ).password ), line->pass );

		if ( line->settings.gpu && line->settings.cpu && !threads_given && cpu.threads > 1 && line->settings.algorithm != chain::ALGORITHM_RANDOMX )
			line->settings.threads = cpu.threads - 1;

		const wchar_t* action = line->action ? line->action : L"--help";
		uint32_t code = 0;

		if ( base::same_text ( action, L"--bench" ) )
			code = bench ( *line );
		else if ( base::same_text ( action, L"--genesis" ) )
			code = genesis ( *line );
		else if ( base::same_text ( action, L"--mine" ) && line->target [ 0 ] )
			code = mine ( *line );
		else if ( base::same_text ( action, L"--test-pool" ) )
			code = test_pool ( *line );
		else if ( base::same_text ( action, L"--self-test" ) )
			code = self_test_command ( );
		else if ( base::same_text ( action, L"--tune" ) )
			code = tune_command ( *line );
		else if ( base::same_text ( action, L"--allow-large-pages" ) ) {

			bool granted = base::allow_large_pages ( );

			base::print_line ( granted ? L"Large pages are allowed for this account. Sign out and back in, and RandomX will use them." : L"Windows refused. Run this once from an administrator terminal." );
			code = granted ? 0 : 1;

		}
#ifdef ALLOY_DEBUG
		else if ( base::same_text ( action, L"--test" ) )
			code = tests::run_all ( line->target );
		else if ( base::same_text ( action, L"--capture" ) && line->target [ 0 ] )
			code = capture ( line->target, line->timed ? line->seconds : 40, !line->stills );
		else if ( base::same_text ( action, L"--icon" ) && line->target [ 0 ] )
			code = export_icon ( line->target );
		else if ( base::same_text ( action, L"--logo" ) && line->target [ 0 ] )
			code = export_logo ( line->target, line->size );
		else if ( base::same_text ( action, L"--smoke-window" ) )
			code = smoke_window ( line->target );
		else if ( base::same_text ( action, L"--tls-check" ) && line->target [ 0 ] )
			code = tls_check ( line->target, line->http );
		else if ( base::same_text ( action, L"--crash-test" ) && line->target [ 0 ] ) {

			volatile uintptr_t nowhere = 0;

			open_journal ( line->target );
			journal ( 0, L"Crashing on purpose to test the crash report" );
			*reinterpret_cast < volatile uint32_t* > ( nowhere + 0 ) = 1;

		}
#endif
		else {

			usage ( );
			code = base::same_text ( action, L"--help" ) ? 0 : 2;

		}

		delete line;

		return code;

	}

}
