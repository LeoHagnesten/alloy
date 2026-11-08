#pragma once

#include "base/system/threads.hh"

namespace app {

	static constexpr int32_t api_room = 8192;
	static constexpr int32_t token_room = 33;

	enum api_command : uint32_t {

		API_NONE,
		API_PAUSE,
		API_RESUME,
		API_START,
		API_STOP,

	};

	struct api_options {

		uint16_t port = 0;
		bool everyone = false;
		bool control = false;
		char token [ token_room ] = { };

	};

	class status_api {

	public:

		status_api ( ) = default;

		status_api ( const status_api& ) = delete;

		status_api& operator = ( const status_api& ) = delete;

		~status_api ( ) {

			stop ( );

		}

		bool start ( const api_options& options );

		void stop ( );

		void publish ( const char* json, int32_t size );

		uint32_t take_command ( );

		uint16_t port ( ) const {

			return bound;

		}

		bool running ( ) const {

			return thread != nullptr;

		}

		const api_options& options ( ) const {

			return config;

		}

	private:

		static unsigned long main ( void* context );

		void run ( );

		api_options config;
		void* listener = nullptr;
		void* thread = nullptr;
		uint16_t bound = 0;
		volatile uint32_t quit = 0;
		volatile uint32_t command = API_NONE;
		base::lock latest_lock;
		char latest [ api_room ] = { };
		int32_t latest_size = 0;

	};

	const char* dashboard_page ( );

}
