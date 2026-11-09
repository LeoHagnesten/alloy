#include "app/tune.hh"
#include "app/session.hh"
#include "base/system/clock.hh"

namespace app {

	static double measure ( session& run, ui::preferences& settings, double warmup, double span, volatile uint32_t* cancel ) {

		const ui::snapshot& view = run.view ( );

		run.start ( settings );

		double began = base::seconds ( );

		if ( settings.algorithm == chain::ALGORITHM_RANDOMX ) {

			while ( base::seconds ( ) - began < 120.0 && !( cancel && base::atomic_load ( *cancel ) ) ) {

				run.tick ( base::seconds ( ), settings );

				if ( view.dataset == ui::DATASET_FULL || view.dataset == ui::DATASET_LIGHT || view.dataset == ui::DATASET_SHORT || view.dataset == ui::DATASET_PART )
					break;

				base::sleep ( 20 );

			}

			began = base::seconds ( );

		}

		while ( base::seconds ( ) - began < warmup && !( cancel && base::atomic_load ( *cancel ) ) ) {

			run.tick ( base::seconds ( ), settings );
			base::sleep ( 20 );

		}

		uint64_t first = view.hashes;
		double from = base::seconds ( );

		while ( base::seconds ( ) - from < span && !( cancel && base::atomic_load ( *cancel ) ) ) {

			run.tick ( base::seconds ( ), settings );
			base::sleep ( 20 );

		}

		run.tick ( base::seconds ( ), settings );

		double rate = static_cast < double > ( view.hashes - first ) / base::max ( base::seconds ( ) - from, 0.001 );

		run.stop ( );

		return rate;

	}

	bool tune ( uint8_t algorithm, const ui::preferences& start, tune_result& out, volatile uint32_t* cancel, void ( *report ) ( const trial& step, void* context ), void* context ) {

		session* run = new session;
		ui::preferences* settings = new ui::preferences ( start );
		const ui::snapshot& view = run->view ( );
		uint32_t cores = view.cores;
		uint32_t threads = view.threads;
		bool card = view.adapter_count > 0 && algorithm != chain::ALGORITHM_RANDOMX;
		double span = algorithm == chain::ALGORITHM_RANDOMX ? 8.0 : 4.0;

		out = tune_result { };
		out.algorithm = algorithm;
		settings->choose_algorithm ( algorithm );
		settings->mode = ui::MODE_BENCHMARK;
		settings->cpu_limit = 100;
		settings->gpu_limit = 100;
		settings->pause_active = false;
		settings->pause_battery = false;
		settings->schedule = false;
		settings->pause_fullscreen = false;
		settings->pause_apps [ 0 ] = 0;

		auto record = [ & ] ( uint32_t count, bool gpu, uint32_t intensity ) {

			if ( out.count >= trial_room || ( cancel && base::atomic_load ( *cancel ) ) )
				return 0.0;

			settings->cpu = count > 0;
			settings->threads = base::max ( count, 1u );
			settings->gpu = gpu;
			settings->intensity = intensity;

			trial& step = out.trials [ out.count++ ];

			step.threads = count;
			step.gpu = gpu;
			step.intensity = intensity;
			step.rate = measure ( *run, *settings, gpu ? 1.5 : 1.0, span, cancel );

			if ( report )
				report ( step, context );

			return step.rate;

		};

		uint32_t candidates [ 4 ] = { cores, threads > 1 ? threads - 1 : threads, threads, 0 };
		uint32_t best_threads = threads;
		double best_cpu = 0.0;

		for ( int32_t i = 0; i < 3; i++ ) {

			bool repeated = false;

			for ( int32_t k = 0; k < i; k++ )
				repeated = repeated || candidates [ k ] == candidates [ i ];

			if ( repeated || !candidates [ i ] )
				continue;

			double rate = record ( candidates [ i ], false, settings->intensity );

			if ( rate > best_cpu ) {

				best_cpu = rate;
				best_threads = candidates [ i ];

			}

		}

		out.threads = best_threads;
		out.gpu = false;
		out.rate = best_cpu;

		if ( card ) {

			uint32_t best_intensity = 22;
			double best_gpu = 0.0;

			if ( algorithm == chain::ALGORITHM_SHA256D || algorithm == chain::ALGORITHM_KHEAVYHASH ) {

				static const uint32_t steps [ ] = { 18, 20, 22, 24, 26 };

				for ( uint32_t intensity : steps ) {

					double rate = record ( 0, true, intensity );

					if ( rate > best_gpu * 1.02 ) {

						best_gpu = rate;
						best_intensity = intensity;

					}

				}

			}

			out.intensity = best_intensity;

			uint32_t mixes [ 2 ] = { best_threads, best_threads > 1 ? best_threads - 1 : best_threads };

			for ( int32_t i = 0; i < 2; i++ ) {

				if ( i && mixes [ 1 ] == mixes [ 0 ] )
					break;

				double rate = record ( mixes [ i ], true, best_intensity );

				if ( rate > out.rate ) {

					out.rate = rate;
					out.threads = mixes [ i ];
					out.gpu = true;

				}

			}

		}

		out.finished = !( cancel && base::atomic_load ( *cancel ) );
		delete settings;
		delete run;

		return out.finished;

	}

	bool tuner::begin ( uint8_t algorithm, const ui::preferences& start ) {

		if ( thread )
			return false;

		kind = algorithm;
		settings = new ui::preferences ( start );
		cancel = 0;
		done = 0;
		measured = 0;
		thread = base::start_thread ( &main, this );

		return thread != nullptr;

	}

	void tuner::stop ( ) {

		if ( !thread )
			return;

		base::atomic_store ( cancel, 1u );
		base::join_thread ( thread );
		thread = nullptr;
		delete settings;
		settings = nullptr;

	}

	bool tuner::finished ( tune_result& out ) {

		if ( !thread || !base::atomic_load ( done ) )
			return false;

		base::join_thread ( thread );
		thread = nullptr;
		delete settings;
		settings = nullptr;
		out = result;

		return true;

	}

	void tuner::progress ( const trial& step, void* context ) {

		base::atomic_add ( static_cast < tuner* > ( context )->measured, 1u );

	}

	unsigned long tuner::main ( void* context ) {

		tuner* self = static_cast < tuner* > ( context );

		tune ( self->kind, *self->settings, self->result, &self->cancel, &progress, self );
		base::atomic_store ( self->done, 1u );

		return 0;

	}

}
