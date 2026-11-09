#pragma once

#include "app/api.hh"
#include "app/self_test.hh"
#include "app/ledger.hh"
#include "app/lookup.hh"
#include "app/rigs.hh"
#include "ui/model.hh"
#include "mine/engine.hh"
#include "mine/offline.hh"
#include "pool/client.hh"
#include "base/strings.hh"
#include "base/system/hardware.hh"

namespace app {

	bool within_schedule ( const base::calendar& now, uint32_t from, uint32_t to, uint32_t days );

	struct notice {

		wchar_t title [ 64 ] = { };
		wchar_t text [ 200 ] = { };

	};

	class session {

	public:

		session ( );

		~session ( );

		bool start ( const ui::preferences& settings );

		void stop ( );

		void apply ( const ui::preferences& settings );

		bool tick ( double now, const ui::preferences& settings );

		void log ( uint8_t level, const wchar_t* text );

		void clear_log ( );

		void check_kernels ( );

		void set_sleeping ( bool value );

		void set_held ( bool value );

		void network_changed ( );

		void announce ( const wchar_t* title, const wchar_t* text );

		void start_lookups ( );

		void save_history ( );

		bool next_notice ( notice& out );

		bool holding ( ) const {

			return held;

		}

		void set_genesis_start ( uint32_t nonce ) {

			genesis_from = nonce;

		}

		bool running ( ) const {

			return active;

		}

		const ui::snapshot& view ( ) const {

			return *shown;

		}

		ui::snapshot& edit ( ) {

			return *shown;

		}

		const base::processor& processor ( ) const {

			return cpu;

		}

		bool learned_pin ( uint8_t& kind, bool& backup, char* out, int32_t room ) {

			if ( !pin_waiting )
				return false;

			pin_waiting = false;
			kind = algorithm;
			backup = pin_backup;
			base::copy_text ( out, room, pin_learned );

			return true;

		}

		uint32_t suggested_threads ( ) const {

			return shown->adapter_count && cpu.threads > 1 ? cpu.threads - 1 : cpu.threads;

		}

		uint16_t api_bound ( ) const {

			return api.port ( );

		}

	private:

		static unsigned long check_main ( void* context );

		void finish_check ( const ui::preferences& settings );

		bool inside_schedule ( const ui::preferences& settings ) const;

		const wchar_t* pause_reason ( double now, const ui::preferences& settings );

		void account ( double now, const ui::preferences& settings );

		mine::limits limits_for ( const ui::preferences& settings ) const;

		void sync_api ( const ui::preferences& settings );

		void publish ( const ui::preferences& settings );

		void sample ( double now );

		void drain_pool ( );

		void finish_genesis ( );

		void describe ( const ui::preferences& settings );

		mine::engine engine;
		pool::client client;
		mine::benchmark_work benchmark;
		mine::genesis_work genesis;
		ui::snapshot* shown = nullptr;
		base::processor cpu;
		bool active = false;
		uint8_t mode = ui::MODE_POOL;
		uint8_t algorithm = chain::ALGORITHM_SHA256D;
		uint32_t adapter = 0;
		uint8_t kernel = 0;
		uint8_t variant = 0;
		double started = 0.0;
		double last_sample = 0.0;
		double last_tick = 0.0;
		uint64_t last_total = 0;
		double live_times [ 64 ] = { };
		uint64_t live_totals [ 64 ] = { };
		int32_t live_count = 0;
		uint64_t last_gpu = 0;
		uint64_t lane_last [ ui::lane_room ] = { };
		uint64_t gpu_last [ ui::gpu_room ] = { };
		uint32_t gpu_restarts_seen [ ui::gpu_room ] = { };
		uint32_t gpu_trims_seen [ ui::gpu_room ] = { };
		status_api api;
		uint32_t api_port = 0;
		double published = 0.0;
		uint32_t pin_serial = 0;
		bool pin_backup = false;
		bool pin_waiting = false;
		char pin_learned [ 65 ] = { };
		bool paused = false;
		uint32_t genesis_from = 0;
		void* checker = nullptr;
		self_report* checked = nullptr;
		bool portable_only = false;
		bool sleeping = false;
		bool held = false;
		double apps_checked = -100.0;
		notice notices [ 8 ];
		uint32_t notices_pushed = 0;
		uint32_t notices_read = 0;
		uint32_t reject_streak = 0;
		ledger book;
		lookup online;
		rig_watch watched;
		bool lookups_on = false;
		double ledger_carry = 0.0;
		double ledger_saved = 0.0;
		double accounted = 0.0;
		uint64_t pool_reward = 0;
		uint8_t gpu_phase_seen [ ui::gpu_room ] = { };
		wchar_t blocking [ 96 ] = { };
		uint32_t broken_cards [ chain::ALGORITHM_COUNT ] = { };

	};

}
