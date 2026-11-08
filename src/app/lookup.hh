#pragma once

#include "chain/algorithm.hh"
#include "base/system/threads.hh"

namespace app {

	enum currency : uint8_t {

		CURRENCY_USD,
		CURRENCY_EUR,
		CURRENCY_GBP,
		CURRENCY_SEK,
		CURRENCY_COUNT,

	};

	struct lookup_request {

		uint8_t algorithm = 0;
		uint8_t variant = 0;
		wchar_t pool [ 256 ] = { };
		wchar_t worker [ 128 ] = { };
		bool balances = false;
		bool prices = false;

	};

	struct lookup_result {

		bool network = false;
		double difficulty = 0.0;
		uint64_t height = 0;
		double reward = 0.0;
		bool balance = false;
		double due = 0.0;
		double paid = 0.0;
		double pool_rate = 0.0;
		double best = 0.0;
		uint64_t pool_shares = 0;
		wchar_t source [ 48 ] = { };
		bool priced = false;
		double prices [ chain::ALGORITHM_COUNT ] [ CURRENCY_COUNT ] = { };
		wchar_t note [ 120 ] = { };
		uint32_t serial = 0;

	};

	class lookup {

	public:

		~lookup ( ) {

			stop ( );

		}

		void start ( );

		void stop ( );

		void ask ( const lookup_request& wanted );

		void read ( lookup_result& out );

		void refresh ( ) {

			base::atomic_store ( hurry, 1u );

		}

	private:

		static unsigned long main ( void* context );

		void run ( );

		void poll ( const lookup_request& wanted, lookup_result& out, bool prices_due );

		void* thread = nullptr;
		volatile uint32_t quit = 0;
		volatile uint32_t hurry = 0;
		base::lock guard_lock;
		lookup_request request;
		lookup_result result;
		uint32_t changes = 0;

	};

	const wchar_t* currency_code ( uint8_t which );

}
