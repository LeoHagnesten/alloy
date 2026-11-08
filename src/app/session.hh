#pragma once

#include "app/api.hh"
#include "ui/model.hh"
#include "mine/engine.hh"
#include "mine/offline.hh"
#include "pool/client.hh"
#include "base/strings.hh"
#include "base/system/hardware.hh"

namespace app {

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
		double started = 0.0;
		double last_sample = 0.0;
		double last_tick = 0.0;
		uint64_t last_total = 0;
		uint64_t last_gpu = 0;
		uint64_t lane_last [ ui::lane_room ] = { };
		uint64_t gpu_last [ ui::gpu_room ] = { };
		status_api api;
		uint32_t api_port = 0;
		double published = 0.0;
		uint32_t pin_serial = 0;
		bool pin_backup = false;
		bool pin_waiting = false;
		char pin_learned [ 65 ] = { };
		bool paused = false;
		uint32_t genesis_from = 0;

	};

}
