#pragma once

#include "chain/header.hh"
#include "chain/algorithm.hh"
#include "base/system/threads.hh"

namespace randomx {

	class cache;
	class dataset;
	class machine;

}

namespace mine {

	static constexpr uint32_t max_lanes = 256;
	static constexpr uint32_t max_gpus = 8;
	static constexpr uint64_t nonce_space = 0x100000000ull;
	static constexpr uint32_t blob_room = 256;

	enum kernel : uint8_t {

		KERNEL_AUTO,
		KERNEL_EXTENSIONS,
		KERNEL_PORTABLE,

	};

	struct unit {

		uint8_t algorithm = chain::ALGORITHM_SHA256D;
		chain::header header;
		uint8_t blob [ blob_room ] = { };
		uint32_t blob_size = 0;
		uint32_t nonce_at = 0;
		uint8_t seed [ 32 ] = { };
		uint8_t variant = 0;
		uint64_t timestamp = 0;
		uint32_t nonce_high = 0;
		uint8_t target [ 32 ] = { };
		uint64_t index = 0;
		uint32_t job = 0;
		uint32_t first = 0;
		uint64_t end = nonce_space;

	};

	class feeder {

	public:

		virtual uint32_t job ( ) {

			return 0;

		}

		virtual bool fill ( uint64_t index, unit& out ) {

			return false;

		}

		virtual bool found ( const unit& from, uint32_t nonce, const uint8_t* digest ) {

			return true;

		}

	};

	struct card_setting {

		bool custom = false;
		bool on = true;
		uint32_t percent = 100;
		uint32_t intensity = 22;
		uint32_t heat = 0;

	};

	struct limits {

		uint8_t algorithm = chain::ALGORITHM_SHA256D;
		uint32_t threads = 1;
		uint32_t cpu_percent = 100;
		base::thread_priority priority = base::PRIORITY_LOW;
		kernel choice = KERNEL_AUTO;
		bool gpu = false;
		uint32_t gpu_percent = 100;
		uint32_t gpu_intensity = 24;
		uint32_t gpu_heat = 0;
		uint32_t adapters [ max_gpus ] = { };
		card_setting cards [ max_gpus ];
		uint32_t adapter_count = 1;
		uint32_t dataset_megabytes = 2080;
		bool pin_threads = true;

	};

	enum gpu_state : uint8_t {

		GPU_OFF,
		GPU_STARTING,
		GPU_RUNNING,
		GPU_FAILED,
		GPU_UNSUPPORTED,
		GPU_COOLING,

	};

	enum dataset_state : uint8_t {

		DATASET_NONE,
		DATASET_BUILDING,
		DATASET_FULL,
		DATASET_LIGHT,
		DATASET_SHORT,
		DATASET_PART,

	};

	class engine {

	public:

		~engine ( );

		bool start ( feeder* source, const limits& settings );

		void stop ( );

		void apply ( const limits& settings );

		void set_paused ( bool value );

		void release_dataset ( );

		void break_gpu ( ) {

			base::atomic_store ( sabotage, 1u );

		}

		bool running ( ) const {

			return active;

		}

		bool halted ( ) const {

			return base::atomic_load ( halt ) != 0;

		}

		bool starved ( ) const {

			return base::atomic_load ( hungry ) != 0;

		}

		bool uses_extensions ( ) const {

			return compress == hash::sha256_block_extensions;

		}

		uint64_t hashes ( ) const;

		uint64_t lane_hashes ( uint32_t lane ) const;

		uint64_t gpu_hashes ( ) const;

		uint64_t best ( ) const {

			return base::atomic_load ( best_top );

		}

		uint32_t lanes ( ) const {

			return lane_count;

		}

		uint32_t threads ( ) const {

			return base::atomic_load ( wanted_threads );

		}

		uint32_t working_threads ( ) const {

			uint32_t wanted = base::atomic_load ( wanted_threads );

			return algorithm ( ) == chain::ALGORITHM_RANDOMX ? base::min ( wanted, base::atomic_load ( rx_lanes ) ) : wanted;

		}

		uint8_t algorithm ( ) const {

			return static_cast < uint8_t > ( base::atomic_load ( seen_algorithm ) );

		}

		uint32_t gpu_count ( ) const {

			return gpus_started;

		}

		gpu_state gpu_status ( ) const;

		gpu_state gpu_status ( uint32_t index ) const {

			return static_cast < gpu_state > ( base::atomic_load ( gpus [ index ].phase ) );

		}

		uint64_t gpu_hashes ( uint32_t index ) const {

			return base::atomic_load ( gpus [ index ].done );

		}

		float gpu_temperature ( uint32_t index ) const {

			return static_cast < float > ( base::atomic_load ( gpus [ index ].heat ) ) / 10.0f;

		}

		uint32_t gpu_restarts ( uint32_t index ) const {

			return base::atomic_load ( gpus [ index ].restarts );

		}

		uint32_t gpu_trimmed ( uint32_t index ) const {

			return base::atomic_load ( gpus [ index ].trimmed );

		}

		uint32_t gpu_errors ( ) const;

		const wchar_t* gpu_name ( ) const {

			return gpus [ 0 ].label;

		}

		const wchar_t* gpu_name ( uint32_t index ) const {

			return gpus [ index ].label;

		}

		uint8_t dataset_variant ( ) const {

			return static_cast < uint8_t > ( base::atomic_load ( rx_kind ) );

		}

		dataset_state dataset_status ( ) const {

			return static_cast < dataset_state > ( base::atomic_load ( rx_state ) );

		}

		uint32_t dataset_progress ( ) const {

			return base::atomic_load ( rx_progress );

		}

		double dataset_seconds ( ) const {

			return rx_seconds;

		}

		uint32_t dataset_megabytes ( ) const {

			return rx_built_for;

		}

		bool dataset_large ( ) const {

			return rx_large;

		}

	private:

		struct alignas ( 64 ) lane {

			engine* owner = nullptr;
			uint32_t index = 0;
			void* thread = nullptr;
			volatile uint64_t done = 0;

		};

		struct alignas ( 64 ) gpu_slot {

			engine* owner = nullptr;
			uint32_t adapter = 0;
			void* thread = nullptr;
			volatile uint64_t done = 0;
			volatile uint32_t phase = GPU_OFF;
			volatile uint32_t faults = 0;
			volatile uint32_t heat = 0;
			volatile uint32_t restarts = 0;
			volatile uint32_t trimmed = 0;
			volatile uint32_t on = 1;
			volatile uint32_t share = 100;
			volatile uint32_t power = 22;
			volatile uint32_t heat_limit = 0;
			wchar_t label [ 128 ] = { };

		};

		struct tools;

		static unsigned long lane_main ( void* context );

		static unsigned long gpu_main ( void* context );

		void run_lane ( lane& self );

		void run_gpu ( gpu_slot& self );

		bool claim ( bool gpu, const uint32_t* gpu_rooms, unit& work, uint64_t& from, uint64_t& to );

		bool stale ( const unit& work ) const;

		void report ( const unit& work, uint32_t nonce, const uint8_t* digest );

		void rest ( double& debt, double busy, uint32_t percent );

		template < typename hasher >
		void sweep ( lane& self, const unit& work, uint64_t from, uint64_t to, uint64_t step, double& debt, hasher& hash_one );

		bool enter_dataset ( const unit& work, tools& kit );

		void leave_dataset ( );

		void build_dataset ( const uint8_t* seed, uint8_t kind );

		feeder* source = nullptr;
		hash::compressor compress = hash::sha256_block;
		lane* lane_list = nullptr;
		uint32_t lane_count = 0;
		gpu_slot gpus [ max_gpus ];
		uint32_t gpus_started = 0;
		bool active = false;
		base::lock claim_lock;
		unit current;
		bool have_unit = false;
		uint64_t next_index = 0;
		uint64_t cursor = 0;
		volatile uint32_t quit = 0;
		volatile uint32_t halt = 0;
		volatile uint32_t hungry = 0;
		volatile uint32_t paused = 0;
		volatile uint32_t wanted_threads = 0;
		volatile uint32_t cpu_share = 100;
		volatile uint32_t priority = base::PRIORITY_LOW;
		volatile uint32_t pinning = 1;
		volatile uint32_t seen_algorithm = chain::ALGORITHM_SHA256D;
		volatile uint32_t gpu_wanted = 0;
		volatile uint32_t gpu_share = 100;
		volatile uint32_t gpu_power = 24;
		volatile uint32_t gpu_heat_limit = 0;
		volatile uint32_t sabotage = 0;
		volatile uint64_t best_top = ~0ull;
		randomx::cache* rx_cache = nullptr;
		randomx::dataset* rx_dataset = nullptr;
		uint8_t rx_seed [ 32 ] = { };
		volatile uint32_t rx_kind = 0;
		uint32_t rx_built_for = 0;
		double rx_seconds = 0.0;
		bool rx_large = false;
		uint64_t level3 = 0;
		bool wide_scrypt = false;
		volatile uint32_t rx_lanes = max_lanes;
		volatile uint32_t rx_megabytes = 2080;
		volatile uint32_t rx_ready = 0;
		volatile uint32_t rx_building = 0;
		volatile uint32_t rx_users = 0;
		volatile uint32_t rx_generation = 0;
		volatile uint32_t rx_progress = 0;
		volatile uint32_t rx_state = DATASET_NONE;

	};

}
