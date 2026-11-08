#pragma once

#include "pool/job.hh"
#include "base/system/threads.hh"

namespace pool {

	struct server_settings {

		uint8_t algorithm = chain::ALGORITHM_SHA256D;
		uint16_t port = 3333;
		double difficulty = 0.01;
		uint32_t job_seconds = 30;
		uint32_t reseed_jobs = 0;
		uint8_t variant = 0;
		uint32_t switch_jobs = 0;
		bool nicehash = false;
		bool node = false;
		bool sv2 = false;
		bool verbose = false;

	};

	class test_server {

	public:

		bool start ( const server_settings& config );

		void stop ( );

		uint16_t port ( ) const {

			return bound;

		}

		uint32_t accepted ( ) const {

			return base::atomic_load ( good );

		}

		uint32_t rejected ( ) const {

			return base::atomic_load ( bad );

		}

		uint32_t clients ( ) const {

			return base::atomic_load ( connected );

		}

		uint32_t jobs ( ) const {

			return base::atomic_load ( made );

		}

		const char* authority_key ( ) const {

			return authority;

		}

		bool ready ( ) const {

			return base::atomic_load ( listening ) != 0;

		}

	private:

		static unsigned long main ( void* context );

		void run ( );

		void run_node ( );

		void run_sv2 ( );

		server_settings config;
		void* thread = nullptr;
		void* socket = nullptr;
		uint16_t bound = 0;
		volatile uint32_t quit = 0;
		volatile uint32_t good = 0;
		volatile uint32_t bad = 0;
		volatile uint32_t connected = 0;
		volatile uint32_t made = 0;
		volatile uint32_t listening = 0;
		char authority [ 80 ] = { };

	};

}
