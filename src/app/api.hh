#pragma once

#include "base/system/threads.hh"

namespace app {

	static constexpr int32_t api_room = 8192;

	class status_api {

	public:

		status_api ( ) = default;

		status_api ( const status_api& ) = delete;

		status_api& operator = ( const status_api& ) = delete;

		~status_api ( ) {

			stop ( );

		}

		bool start ( uint16_t port );

		void stop ( );

		void publish ( const char* json, int32_t size );

		uint16_t port ( ) const {

			return bound;

		}

		bool running ( ) const {

			return thread != nullptr;

		}

	private:

		static unsigned long main ( void* context );

		void run ( );

		void* listener = nullptr;
		void* thread = nullptr;
		uint16_t bound = 0;
		volatile uint32_t quit = 0;
		base::lock latest_lock;
		char latest [ api_room ] = { };
		int32_t latest_size = 0;

	};

}
