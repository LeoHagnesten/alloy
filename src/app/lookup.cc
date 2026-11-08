#include "app/lookup.hh"
#include "net/http.hh"
#include "chain/algorithm.hh"
#include "base/formats/json.hh"
#include "base/system/clock.hh"
#include "base/memory.hh"
#include "base/strings.hh"
#include "base/text.hh"

namespace app {

	static constexpr double poll_seconds = 120.0;
	static constexpr double price_seconds = 600.0;

	static const char* const currency_keys [ CURRENCY_COUNT ] = { "usd", "eur", "gbp", "sek" };
	static const wchar_t* const currency_codes [ CURRENCY_COUNT ] = { L"USD", L"EUR", L"GBP", L"SEK" };
	static const char* const coin_ids [ chain::ALGORITHM_COUNT ] = { "bitcoin", "litecoin", "monero", "kaspa" };

	const wchar_t* currency_code ( uint8_t which ) {

		return currency_codes [ which % CURRENCY_COUNT ];

	}

	void lookup::start ( ) {

		if ( thread )
			return;

		quit = 0;
		thread = base::start_thread ( &main, this );

	}

	void lookup::stop ( ) {

		if ( !thread )
			return;

		base::atomic_store ( quit, 1u );
		base::join_thread ( thread );
		thread = nullptr;

	}

	void lookup::ask ( const lookup_request& wanted ) {

		base::guard hold ( guard_lock );

		bool same = wanted.algorithm == request.algorithm && wanted.variant == request.variant && wanted.balances == request.balances && wanted.prices == request.prices && base::same_text ( wanted.pool, request.pool ) && base::same_text ( wanted.worker, request.worker );

		if ( same )
			return;

		request = wanted;
		changes++;
		base::atomic_store ( hurry, 1u );

	}

	void lookup::read ( lookup_result& out ) {

		base::guard hold ( guard_lock );

		out = result;

	}

	unsigned long lookup::main ( void* context ) {

		static_cast < lookup* > ( context )->run ( );

		return 0;

	}

	static bool fetch ( const char* url, base::json& parsed, net::http_reply& reply ) {

		return net::http_get ( url, reply, 10000 ) && reply.status == 200 && reply.body.size ( ) > 1 && parsed.parse ( reply.body.data ( ), reply.body.size ( ) - 1 );

	}

	static double scaled_number ( const char* text ) {

		static const char suffixes [ ] = "kMGTPE";
		int32_t size = base::length ( text );
		double value = base::read_decimal ( text, size );

		for ( int32_t i = 0; i < 6 && size; i++ ) {

			if ( text [ size - 1 ] == suffixes [ i ] ) {

				for ( int32_t k = 0; k <= i; k++ )
					value *= 1000.0;

			}

		}

		return value;

	}

	void lookup::poll ( const lookup_request& wanted, lookup_result& out, bool prices_due ) {

		base::json* parsed = new base::json;
		net::http_reply* reply = new net::http_reply;
		char pool [ 256 ];
		char wallet [ 128 ];
		base::writer < char, 512 > url;
		int32_t length = 0;

		base::narrow ( pool, sizeof ( pool ), wanted.pool );
		base::narrow ( wallet, sizeof ( wallet ), wanted.worker );

		for ( int32_t i = 0; pool [ i ]; i++ )
			pool [ i ] = pool [ i ] >= 'A' && pool [ i ] <= 'Z' ? static_cast < char > ( pool [ i ] + 32 ) : pool [ i ];

		while ( wallet [ length ] && wallet [ length ] != '.' && wallet [ length ] != '+' && wallet [ length ] != ':' )
			length++;

		wallet [ length ] = 0;

		bool monero = wanted.algorithm == chain::ALGORITHM_RANDOMX && wanted.variant <= 1;
		auto mentions = [ & ] ( const char* name ) {

			for ( int32_t i = 0; pool [ i ]; i++ ) {

				if ( base::starts_with ( pool + i, name ) )
					return true;

			}

			return false;

		};

		out.network = false;
		out.balance = false;
		out.note [ 0 ] = 0;

		if ( monero && fetch ( "https://supportxmr.com/api/network/stats", *parsed, *reply ) ) {

			out.network = true;
			out.difficulty = parsed->number ( parsed->member ( 0, "difficulty" ) );
			out.height = static_cast < uint64_t > ( parsed->number ( parsed->member ( 0, "height" ) ) );
			out.reward = parsed->number ( parsed->member ( 0, "value" ) ) / 1e12;

		}

		if ( wanted.balances && length > 20 && mentions ( "supportxmr.com" ) ) {

			url.clear ( );
			url.add ( "https://supportxmr.com/api/miner/" ).add ( wallet ).add ( "/stats" );

			if ( fetch ( url.get ( ), *parsed, *reply ) ) {

				out.balance = true;
				out.due = parsed->number ( parsed->member ( 0, "amtDue" ) ) / 1e12;
				out.paid = parsed->number ( parsed->member ( 0, "amtPaid" ) ) / 1e12;
				out.pool_rate = parsed->number ( parsed->member ( 0, "hash" ) );
				out.pool_shares = static_cast < uint64_t > ( parsed->number ( parsed->member ( 0, "validShares" ) ) );
				out.best = 0.0;
				base::copy_text ( out.source, base::count_of ( out.source ), L"SupportXMR" );

			} else
				base::copy_text ( out.note, base::count_of ( out.note ), L"SupportXMR didn't answer, so the balance is from the last lookup" );

		} else if ( wanted.balances && length > 20 && mentions ( "ckpool.org" ) ) {

			url.clear ( );
			url.add ( "https://solo.ckpool.org/users/" ).add ( wallet );

			if ( fetch ( url.get ( ), *parsed, *reply ) ) {

				char text [ 32 ];

				out.balance = true;
				out.due = 0.0;
				out.paid = 0.0;
				parsed->text ( parsed->member ( 0, "hashrate1hr" ), text, sizeof ( text ) );
				out.pool_rate = scaled_number ( text );
				out.pool_shares = static_cast < uint64_t > ( parsed->number ( parsed->member ( 0, "shares" ) ) );
				out.best = parsed->number ( parsed->member ( 0, "bestever" ) );
				base::copy_text ( out.source, base::count_of ( out.source ), L"CKPool solo" );

			} else
				base::copy_text ( out.note, base::count_of ( out.note ), L"CKPool hasn't seen a share from this address yet" );

		} else if ( wanted.balances && wallet [ 0 ] )
			base::copy_text ( out.note, base::count_of ( out.note ), L"This pool doesn't publish balances Alloy can read, so the figures are Alloy's own estimate" );

		if ( prices_due && wanted.prices ) {

			url.clear ( );
			url.add ( "https://api.coingecko.com/api/v3/simple/price?ids=bitcoin,litecoin,monero,kaspa&vs_currencies=usd,eur,gbp,sek" );

			if ( fetch ( url.get ( ), *parsed, *reply ) ) {

				out.priced = true;

				for ( int32_t coin = 0; coin < chain::ALGORITHM_COUNT; coin++ ) {

					int32_t node = parsed->member ( 0, coin_ids [ coin ] );

					for ( int32_t c = 0; c < CURRENCY_COUNT; c++ )
						out.prices [ coin ] [ c ] = parsed->number ( parsed->member ( node, currency_keys [ c ] ) );

				}

			}

		}

		delete reply;
		delete parsed;

	}

	void lookup::run ( ) {

		double last_poll = -1e9;
		double last_price = -1e9;
		uint32_t seen = ~0u;

		while ( !base::atomic_load ( quit ) ) {

			base::sleep ( 250 );

			double now = base::seconds ( );
			lookup_request wanted;
			uint32_t version = 0;

			{

				base::guard hold ( guard_lock );

				wanted = request;
				version = changes;

			}

			bool rushed = base::atomic_exchange ( hurry, 0u ) != 0;
			bool prices_due = wanted.prices && ( now - last_price >= price_seconds || ( rushed && now - last_price > 30.0 ) || version != seen );

			if ( !rushed && version == seen && now - last_poll < poll_seconds && !prices_due )
				continue;

			lookup_result fresh;

			{

				base::guard hold ( guard_lock );

				fresh = result;

			}

			poll ( wanted, fresh, prices_due );
			last_poll = base::seconds ( );

			if ( prices_due )
				last_price = last_poll;

			seen = version;
			fresh.serial++;

			base::guard hold ( guard_lock );

			result = fresh;

		}

	}

}
