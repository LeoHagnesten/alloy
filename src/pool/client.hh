#pragma once

#include "pool/job.hh"
#include "version.hh"
#include "mine/engine.hh"
#include "base/system/clock.hh"
#include "base/containers/ring.hh"

namespace net {

	class connection;

}

namespace pool {

	static constexpr const char* agent = "alloy/" ALLOY_VERSION_TEXT;

	enum link_state : uint8_t {

		LINK_OFF,
		LINK_CONNECTING,
		LINK_SUBSCRIBING,
		LINK_AUTHORIZING,
		LINK_READY,
		LINK_WAITING,
		LINK_REFUSED,

	};

	enum event_kind : uint8_t {

		EVENT_INFO,
		EVENT_WARNING,
		EVENT_ERROR,
		EVENT_JOB,
		EVENT_ACCEPTED,
		EVENT_REJECTED,

	};

	struct event {

		event_kind kind = EVENT_INFO;
		base::wall_time time;
		double difficulty = 0.0;
		double latency = 0.0;
		uint32_t nonce = 0;
		wchar_t text [ 160 ] = { };

	};

	struct settings {

		uint8_t algorithm = chain::ALGORITHM_SHA256D;
		uint8_t variant = 0;
		wchar_t address [ 256 ] = { };
		wchar_t backup [ 256 ] = { };
		wchar_t worker [ 128 ] = { };
		wchar_t password [ 128 ] = { };
		char pin [ 65 ] = { };
		char backup_pin [ 65 ] = { };

	};

	struct status {

		link_state state = LINK_OFF;
		double difficulty = 0.0;
		double latency = 0.0;
		uint32_t accepted = 0;
		uint32_t rejected = 0;
		uint32_t stale = 0;
		uint32_t jobs = 0;
		uint32_t version = 0;
		uint32_t bits = 0;
		uint32_t time = 0;
		int32_t branches = 0;
		int32_t coinbase_size = 0;
		bool has_job = false;
		uint8_t previous [ 32 ] = { };
		wchar_t job [ 64 ] = { };
		uint64_t height = 0;
		int32_t blob_size = 0;
		uint8_t seed [ 32 ] = { };
		uint8_t variant = 0;
		bool on_backup = false;
		bool nicehash = false;
		char learned_pin [ 65 ] = { };
		bool learned_backup = false;
		uint32_t pin_serial = 0;

	};

	class client : public mine::feeder {

	public:

		bool start ( const settings& config );

		void stop ( );

		void read ( status& out );

		bool next_event ( event& out );

		uint32_t job ( ) override;

		bool fill ( uint64_t index, mine::unit& out ) override;

		bool found ( const mine::unit& from, uint32_t nonce, const uint8_t* digest ) override;

	private:

		struct known_job {

			uint32_t serial = 0;
			char id [ 64 ] = { };
			uint32_t time = 0;
			double difficulty = 0.0;

		};

		struct share {

			uint32_t serial = 0;
			uint64_t second = 0;
			uint32_t time = 0;
			uint32_t nonce = 0;
			double difficulty = 0.0;
			uint8_t result [ 32 ] = { };

		};

		struct pending_submit {

			uint32_t id = 0;
			double sent = 0.0;
			double difficulty = 0.0;
			uint32_t nonce = 0;

		};

		static unsigned long main ( void* context );

		void run ( );

		bool session ( );

		bool handle ( const char* text, int32_t size );

		void subscribe ( );

		void login ( );

		bool handle_blob ( const base::json& message );

		void take_blob_job ( const base::json& message, int32_t node );

		void keep_alive ( );

		void settle ( uint32_t number, bool accepted, const wchar_t* reason );

		void submit_shares ( );

		bool blob_mode ( ) const {

			return config.algorithm == chain::ALGORITHM_RANDOMX;

		}

		void note ( event_kind kind, const wchar_t* text, double difficulty = 0.0, double latency = 0.0, uint32_t nonce = 0 );

		void advance ( );

		void set_state ( link_state value );

		void pause ( uint32_t seconds );

		settings config;
		void* thread = nullptr;
		volatile uint32_t quit = 0;
		volatile uint32_t on_backup = 0;
		volatile uint32_t serial = 0;
		base::lock state_lock;
		status shown;
		pool::job current;
		blob_job blob_current;
		char login_id [ 128 ] = { };
		double last_keepalive = 0.0;
		bool nicehash = false;
		char refused_algo [ 16 ] = { };
		session_nonce nonce;
		double difficulty = 1.0;
		double next_difficulty = 1.0;
		bool have_job = false;
		bool authorized = false;
		uint32_t clean_serial = 0;
		known_job recent [ 8 ];
		uint32_t recent_next = 0;
		base::lock share_lock;
		share queued [ 64 ];
		int32_t queued_count = 0;
		pending_submit submits [ 32 ];
		uint32_t next_id = 100;
		base::lock event_lock;
		base::ring < event, 256 > events;
		uint64_t events_read = 0;
		net::connection* link = nullptr;
		base::json* parser = nullptr;
		bool refused = false;

	};

}
