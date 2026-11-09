#pragma once

#include "chain/coins.hh"
#include "base/system/clock.hh"

namespace ui {

	static constexpr int32_t history_room = 3600;
	static constexpr int32_t share_room = 200;
	static constexpr int32_t log_room = 300;
	static constexpr int32_t lane_room = 256;
	static constexpr int32_t adapter_room = 8;
	static constexpr uint32_t dataset_full = 2080;
	static constexpr uint32_t dataset_cache = 256;

	enum work_mode : uint8_t {

		MODE_POOL,
		MODE_BENCHMARK,
		MODE_GENESIS,
		MODE_COUNT,

	};

	enum theme_choice : uint8_t {

		THEME_SYSTEM,
		THEME_DARK,
		THEME_LIGHT,

	};

	enum log_level : uint8_t {

		LEVEL_INFO,
		LEVEL_GOOD,
		LEVEL_WARNING,
		LEVEL_ERROR,

	};

	enum link_phase : uint8_t {

		PHASE_OFF,
		PHASE_CONNECTING,
		PHASE_SIGNING_IN,
		PHASE_READY,
		PHASE_RETRYING,
		PHASE_REFUSED,

	};

	enum gpu_phase : uint8_t {

		GPU_IDLE,
		GPU_STARTING,
		GPU_WORKING,
		GPU_BROKEN,
		GPU_UNSUPPORTED,
		GPU_COOLING,

	};

	enum command : uint8_t {

		COMMAND_NONE,
		COMMAND_TOGGLE,
		COMMAND_PREFERENCES,
		COMMAND_RESTART,
		COMMAND_CLEAR_LOG,
		COMMAND_COPY_TOKEN,
		COMMAND_OPEN_DASHBOARD,
		COMMAND_OPEN_LOGS,
		COMMAND_LARGE_PAGES,
		COMMAND_EXPORT_SHARES,
		COMMAND_EXPORT_LOG,
		COMMAND_SAVE_PROFILE,
		COMMAND_APPLY_PROFILE,
		COMMAND_DELETE_PROFILE,
		COMMAND_ADD_RIG,
		COMMAND_REMOVE_RIG,
		COMMAND_TUNE,
		COMMAND_FINISH_SETUP,
		COMMAND_REFRESH_BALANCE,

	};

	static constexpr uint32_t all_adapters = 0xFF;
	static constexpr int32_t gpu_room = 8;

	struct pool_profile {

		wchar_t address [ 256 ] = { };
		wchar_t worker [ 128 ] = { };
		wchar_t password [ 128 ] = { };
		wchar_t backup [ 256 ] = { };
		char pin [ 65 ] = { };
		char backup_pin [ 65 ] = { };
		wchar_t difficulty [ 24 ] = { };

	};

	struct card_tuning {

		bool custom = false;
		bool on = true;
		uint32_t limit = 100;
		uint32_t intensity = 22;
		uint32_t heat = 0;

	};

	struct algorithm_tuning {

		uint32_t threads = 0;
		uint32_t cpu_limit = 100;
		bool gpu = true;
		uint32_t gpu_limit = 100;
		uint32_t intensity = 22;

	};

	static constexpr int32_t rig_room = 8;

	enum rig_state : uint8_t {

		RIG_WAITING,
		RIG_ONLINE,
		RIG_UNREACHABLE,
		RIG_LOCKED,

	};

	struct rig_entry {

		wchar_t address [ 96 ] = { };
		char token [ 33 ] = { };

	};

	struct preferences {

		uint8_t mode = MODE_POOL;
		uint8_t algorithm = chain::ALGORITHM_SHA256D;
		pool_profile pools [ chain::ALGORITHM_COUNT ] = {

			{ L"127.0.0.1:3333", L"alloy.worker", L"x" },
			{ L"127.0.0.1:3334", L"alloy.worker", L"x" },
			{ L"127.0.0.1:3335", L"alloy.worker", L"x" },
			{ L"127.0.0.1:3336", L"alloy.worker", L"x" },
			{ L"127.0.0.1:3337", L"alloy.worker", L"x" },

		};
		uint32_t dataset_megabytes = dataset_full;
		uint8_t variant = 0;
		uint8_t coins [ chain::ALGORITHM_COUNT ] = { chain::COIN_BITCOIN, chain::COIN_LITECOIN, chain::COIN_MONERO, chain::COIN_KASPA, chain::COIN_ETHEREUM_CLASSIC };
		bool cpu = true;
		uint32_t threads = 0;
		uint32_t cpu_limit = 100;
		uint8_t priority = 1;
		uint8_t kernel = 0;
		bool gpu = true;
		uint32_t adapter = 0;
		uint32_t gpu_limit = 100;
		uint32_t intensity = 22;
		uint32_t gpu_heat = 0;
		card_tuning cards [ adapter_room ];
		bool pin_threads = true;
		uint32_t cores = 0;
		uint8_t scrypt_kernel = 0;
		bool interpret = false;
		bool advanced = false;
		uint32_t failover_tries = 3;
		uint32_t failback_minutes = 10;
		uint32_t connect_seconds = 5;
		uint32_t silence_minutes = 5;
		uint32_t retry_seconds = 60;
		wchar_t coinbase_tag [ 41 ] = L"/alloy/";
		uint8_t rate_window = 0;
		uint32_t pool_fee = 0;
		wchar_t power_watts [ 8 ] = { };
		wchar_t power_price [ 16 ] = { };
		bool pause_active = false;
		uint32_t idle_minutes = 2;
		bool pause_battery = true;
		bool pause_fullscreen = false;
		wchar_t pause_apps [ 256 ] = { };
		bool schedule = false;
		uint32_t schedule_from = 22;
		uint32_t schedule_to = 7;
		uint32_t schedule_days = 0x7F;
		bool notify = true;
		bool hotkey = true;
		bool autostart = false;
		bool launch_at_login = false;
		bool close_to_tray = false;
		uint32_t api_port = 0;
		rig_entry rigs [ rig_room ];
		bool lookups = true;
		bool show_fiat = false;
		uint8_t currency = 0;
		bool api_lan = false;
		bool api_control = false;
		char api_token [ 33 ] = { };
		uint8_t theme = THEME_SYSTEM;
		uint8_t chart_style = 0;
		uint8_t chart_span = 1;
		uint8_t chart_update = 0;
		uint8_t chart_smoothing = 0;
		uint8_t chart_clock = 0;
		algorithm_tuning tunings [ chain::ALGORITHM_COUNT ];

		uint8_t coin ( ) const {

			return coins [ algorithm % chain::ALGORITHM_COUNT ];

		}

		void choose_coin ( uint8_t which ) {

			if ( !chain::coin_mines ( which, algorithm ) )
				return;

			coins [ algorithm % chain::ALGORITHM_COUNT ] = which;

			if ( algorithm == chain::ALGORITHM_RANDOMX )
				variant = chain::coin_of ( which ).variant;

		}

		pool_profile& pool ( ) {

			return pools [ algorithm % chain::ALGORITHM_COUNT ];

		}

		const pool_profile& pool ( ) const {

			return pools [ algorithm % chain::ALGORITHM_COUNT ];

		}

		void stash ( ) {

			algorithm_tuning& slot = tunings [ algorithm % chain::ALGORITHM_COUNT ];

			slot.threads = threads;
			slot.cpu_limit = cpu_limit;
			slot.gpu = gpu;
			slot.gpu_limit = gpu_limit;
			slot.intensity = intensity;

		}

		void unstash ( ) {

			const algorithm_tuning& slot = tunings [ algorithm % chain::ALGORITHM_COUNT ];

			threads = slot.threads ? slot.threads : threads;
			cpu_limit = slot.cpu_limit;
			gpu = slot.gpu;
			gpu_limit = slot.gpu_limit;
			intensity = slot.intensity;

		}

		void choose_algorithm ( uint8_t next ) {

			stash ( );
			algorithm = static_cast < uint8_t > ( next % chain::ALGORITHM_COUNT );
			unstash ( );

		}

	};

	enum dataset_phase : uint8_t {

		DATASET_NONE,
		DATASET_BUILDING,
		DATASET_FULL,
		DATASET_LIGHT,
		DATASET_SHORT,
		DATASET_PART,

	};

	static constexpr int32_t day_view_room = 30;
	struct rig_status {

		wchar_t name [ 64 ] = { };
		wchar_t address [ 96 ] = { };
		uint8_t state = RIG_WAITING;
		bool running = false;
		bool paused = false;
		wchar_t algorithm [ 24 ] = { };
		double rate = 0.0;
		uint32_t accepted = 0;
		uint32_t rejected = 0;
		double uptime = 0.0;
		int32_t gpus = 0;
		double seen = 0.0;

	};

	struct day_row {

		uint32_t day = 0;
		uint64_t hashes = 0;
		uint32_t accepted = 0;
		uint32_t rejected = 0;
		uint32_t seconds = 0;
		double earned [ chain::COIN_COUNT ] = { };

	};

	struct share_row {

		base::wall_time time;
		bool accepted = false;
		double difficulty = 0.0;
		double latency = 0.0;
		uint32_t nonce = 0;
		wchar_t reason [ 80 ] = { };

	};

	struct log_row {

		base::wall_time time;
		uint8_t level = LEVEL_INFO;
		wchar_t text [ 160 ] = { };

	};

	struct snapshot {

		bool running = false;
		bool paused = false;
		bool light = false;
		uint8_t mode = MODE_POOL;
		uint8_t algorithm = chain::ALGORITHM_SHA256D;
		uint8_t dataset = DATASET_NONE;
		uint32_t dataset_progress = 0;
		uint32_t dataset_megabytes = 0;
		uint32_t memory_free = 0;
		double dataset_seconds = 0.0;
		uint64_t height = 0;
		int32_t blob_size = 0;
		uint8_t seed [ 32 ] = { };
		uint8_t coin = 0;
		uint8_t variant = 0;
		bool aes = false;
		wchar_t status [ 160 ] = { };
		wchar_t pause_reason [ 80 ] = { };
		double rate = 0.0;
		double average = 0.0;
		double peak = 0.0;
		double uptime = 0.0;
		double best = 0.0;
		uint64_t hashes = 0;
		uint32_t accepted = 0;
		uint32_t rejected = 0;
		uint32_t stale = 0;
		uint32_t faults = 0;
		float history [ history_room ] = { };
		int32_t history_count = 0;
		uint64_t history_first = 0;
		uint64_t history_serial = 0;
		double history_time = 0.0;
		double live_rate = 0.0;
		double cpu_rate = 0.0;
		uint64_t cpu_hashes = 0;
		float lane_rates [ lane_room ] = { };
		int32_t lane_count = 0;
		uint32_t active_threads = 0;
		bool extensions = false;
		double gpu_rate = 0.0;
		uint64_t gpu_hashes = 0;
		uint8_t gpu_state = GPU_IDLE;
		wchar_t gpu_name [ 128 ] = { };
		int32_t gpu_count = 0;
		wchar_t gpu_names [ gpu_room ] [ 128 ] = { };
		float gpu_rates [ gpu_room ] = { };
		float gpu_heat [ gpu_room ] = { };
		uint8_t gpu_states [ gpu_room ] = { };
		bool large_pages = false;
		bool pages_granted = false;
		bool tuning = false;
		uint32_t tuning_steps = 0;
		bool on_backup = false;
		bool nicehash = false;
		uint32_t api_port = 0;
		share_row shares [ share_room ];
		int32_t share_count = 0;
		uint64_t share_serial = 0;
		log_row logs [ log_room ];
		int32_t log_count = 0;
		uint64_t log_serial = 0;
		uint8_t link = PHASE_OFF;
		double difficulty = 0.0;
		double latency = 0.0;
		bool has_job = false;
		wchar_t job [ 64 ] = { };
		uint32_t version = 0;
		uint32_t bits = 0;
		uint32_t time = 0;
		int32_t branches = 0;
		int32_t coinbase_size = 0;
		uint8_t previous [ 32 ] = { };
		double progress = 0.0;
		double remaining = 0.0;
		bool solved = false;
		uint32_t nonce = 0;
		uint32_t from = 0;
		uint8_t block_hash [ 32 ] = { };
		wchar_t cpu_name [ 64 ] = { };
		uint32_t cores = 1;
		uint32_t threads = 1;
		bool sha = false;
		wchar_t adapters [ adapter_room ] [ 128 ] = { };
		int32_t adapter_count = 0;
		bool network_known = false;
		double network_difficulty = 0.0;
		uint64_t network_height = 0;
		double block_reward = 0.0;
		double network_rate = 0.0;
		double share_value = 0.0;
		double per_day = 0.0;
		double block_seconds = 0.0;
		double shares_per_hour = 0.0;
		double run_earned = 0.0;
		day_row days [ day_view_room ];
		int32_t day_count = 0;
		day_row lifetime;
		uint32_t today = 0;
		bool balance_known = false;
		double balance_due = 0.0;
		double balance_paid = 0.0;
		double pool_rate = 0.0;
		double pool_best = 0.0;
		uint64_t pool_shares = 0;
		wchar_t balance_source [ 48 ] = { };
		wchar_t lookup_note [ 120 ] = { };
		bool price_known = false;
		double price = 0.0;
		rig_status rigs [ rig_room ];
		int32_t rig_count = 0;
		wchar_t profiles [ 12 ] [ 48 ] = { };
		int32_t profile_count = 0;

	};

}
