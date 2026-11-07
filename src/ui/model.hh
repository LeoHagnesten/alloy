#pragma once

#include "chain/algorithm.hh"
#include "base/clock.hh"

namespace ui {

	static constexpr int32_t history_room = 600;
	static constexpr int32_t share_room = 200;
	static constexpr int32_t log_room = 300;
	static constexpr int32_t lane_room = 256;
	static constexpr int32_t adapter_room = 8;

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

	};

	enum command : uint8_t {

		COMMAND_NONE,
		COMMAND_TOGGLE,
		COMMAND_PREFERENCES,
		COMMAND_RESTART,
		COMMAND_CLEAR_LOG,

	};

	struct pool_profile {

		wchar_t address [ 256 ] = { };
		wchar_t worker [ 128 ] = { };
		wchar_t password [ 128 ] = { };

	};

	struct preferences {

		uint8_t mode = MODE_POOL;
		uint8_t algorithm = chain::ALGORITHM_SHA256D;
		pool_profile pools [ chain::ALGORITHM_COUNT ] = {

			{ L"127.0.0.1:3333", L"alloy.worker", L"x" },
			{ L"127.0.0.1:3334", L"alloy.worker", L"x" },
			{ L"127.0.0.1:3335", L"alloy.worker", L"x" },

		};
		bool full_dataset = true;
		bool cpu = true;
		uint32_t threads = 0;
		uint32_t cpu_limit = 100;
		uint8_t priority = 1;
		uint8_t kernel = 0;
		bool gpu = true;
		uint32_t adapter = 0;
		uint32_t gpu_limit = 100;
		uint32_t intensity = 22;
		bool pause_active = false;
		uint32_t idle_minutes = 2;
		bool pause_battery = true;
		bool autostart = false;
		uint8_t theme = THEME_SYSTEM;

		pool_profile& pool ( ) {

			return pools [ algorithm % chain::ALGORITHM_COUNT ];

		}

		const pool_profile& pool ( ) const {

			return pools [ algorithm % chain::ALGORITHM_COUNT ];

		}

	};

	enum dataset_phase : uint8_t {

		DATASET_NONE,
		DATASET_BUILDING,
		DATASET_FULL,
		DATASET_LIGHT,
		DATASET_SHORT,

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
		double dataset_seconds = 0.0;
		uint64_t height = 0;
		int32_t blob_size = 0;
		uint8_t seed [ 32 ] = { };
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
		uint64_t history_serial = 0;
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

	};

}
