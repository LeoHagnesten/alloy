#pragma once

#include "chain/header.hh"
#include "base/threads.hh"

namespace mine {

	static constexpr uint32_t max_lanes = 256;
	static constexpr uint64_t nonce_space = 0x100000000ull;

	enum kernel : uint8_t {

		KERNEL_AUTO,
		KERNEL_EXTENSIONS,
		KERNEL_PORTABLE,

	};

	struct unit {

		chain::header header;
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

	struct limits {

		uint32_t threads = 1;
		uint32_t cpu_percent = 100;
		base::thread_priority priority = base::PRIORITY_LOW;
		kernel choice = KERNEL_AUTO;
		bool gpu = false;
		uint32_t gpu_percent = 100;
		uint32_t gpu_intensity = 24;
		uint32_t adapter = 0;

	};

	enum gpu_state : uint8_t {

		GPU_OFF,
		GPU_STARTING,
		GPU_RUNNING,
		GPU_FAILED,

	};

	class engine {

	public:

		bool start ( feeder* source, const limits& settings );

		void stop ( );

		void apply ( const limits& settings );

		void set_paused ( bool value );

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

		uint64_t gpu_hashes ( ) const {

			return base::atomic_load ( gpu_done );

		}

		uint64_t best ( ) const {

			return base::atomic_load ( best_top );

		}

		uint32_t lanes ( ) const {

			return lane_count;

		}

		uint32_t threads ( ) const {

			return base::atomic_load ( wanted_threads );

		}

		gpu_state gpu_status ( ) const {

			return static_cast < gpu_state > ( base::atomic_load ( gpu_phase ) );

		}

		uint32_t gpu_errors ( ) const {

			return base::atomic_load ( gpu_faults );

		}

		const wchar_t* gpu_name ( ) const {

			return gpu_label;

		}

	private:

		struct alignas ( 64 ) lane {

			engine* owner = nullptr;
			uint32_t index = 0;
			void* thread = nullptr;
			volatile uint64_t done = 0;

		};

		static unsigned long lane_main ( void* context );

		static unsigned long gpu_main ( void* context );

		void run_lane ( lane& self );

		void run_gpu ( );

		bool claim ( uint64_t chunk, unit& work, uint64_t& from, uint64_t& to );

		bool stale ( const unit& work ) const;

		void report ( const unit& work, uint32_t nonce, const uint8_t* digest );

		void rest ( double& debt, double busy, uint32_t percent );

		feeder* source = nullptr;
		hash::compressor compress = hash::sha256_block;
		lane* lane_list = nullptr;
		uint32_t lane_count = 0;
		void* gpu_thread = nullptr;
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
		volatile uint32_t gpu_wanted = 0;
		volatile uint32_t gpu_share = 100;
		volatile uint32_t gpu_power = 24;
		volatile uint32_t gpu_adapter = 0;
		volatile uint32_t gpu_phase = GPU_OFF;
		volatile uint32_t gpu_faults = 0;
		volatile uint64_t gpu_done = 0;
		volatile uint64_t best_top = ~0ull;
		wchar_t gpu_label [ 128 ] = { };

	};

}
