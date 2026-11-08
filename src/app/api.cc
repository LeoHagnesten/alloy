#include "app/api.hh"
#include "net/socket.hh"
#include "base/system/clock.hh"
#include "base/memory.hh"
#include "base/strings.hh"
#include "base/text.hh"

namespace app {

	bool status_api::start ( uint16_t port ) {

		stop ( );

		net::listener* open = new net::listener;

		if ( !open->open ( port ) ) {

			delete open;

			return false;

		}

		listener = open;
		bound = open->port ( );
		quit = 0;
		thread = base::start_thread ( &main, this );

		return thread != nullptr;

	}

	void status_api::stop ( ) {

		if ( !thread )
			return;

		base::atomic_store ( quit, 1u );
		base::join_thread ( thread );
		thread = nullptr;
		delete static_cast < net::listener* > ( listener );
		listener = nullptr;
		bound = 0;

	}

	void status_api::publish ( const char* json, int32_t size ) {

		base::guard hold ( latest_lock );

		latest_size = base::min ( size, api_room );
		base::copy ( latest, json, static_cast < size_t > ( latest_size ) );

	}

	unsigned long status_api::main ( void* context ) {

		static_cast < status_api* > ( context )->run ( );

		return 0;

	}

	static bool local_host ( const char* line, int32_t size, uint16_t port ) {

		char wanted [ 2 ] [ 48 ];
		base::writer < char, 48 > first;
		base::writer < char, 48 > second;

		first.add ( "host: 127.0.0.1:" ).number ( port );
		second.add ( "host: localhost:" ).number ( port );
		base::copy_text ( wanted [ 0 ], 48, first.get ( ) );
		base::copy_text ( wanted [ 1 ], 48, second.get ( ) );

		for ( const char* candidate : wanted ) {

			int32_t length = base::length ( candidate );
			bool same = size == length;

			for ( int32_t i = 0; i < length && same; i++ ) {

				char value = line [ i ] >= 'A' && line [ i ] <= 'Z' ? static_cast < char > ( line [ i ] + 32 ) : line [ i ];

				same = value == candidate [ i ];

			}

			if ( same )
				return true;

		}

		return false;

	}

	void status_api::run ( ) {

		net::listener& server = *static_cast < net::listener* > ( listener );
		net::connection* client = new net::connection;
		char* line = new char [ 1024 ];
		char* body = new char [ api_room ];

		while ( !base::atomic_load ( quit ) ) {

			if ( !server.accept ( *client, 100 ) )
				continue;

			bool wanted_path = false;
			bool trusted = false;
			bool first = true;
			double began = base::seconds ( );

			while ( client->is_open ( ) && base::seconds ( ) - began < 2.0 ) {

				line [ 0 ] = 1;

				int32_t size = client->read_line ( line, 1024, 100 );

				if ( size < 0 )
					break;

				if ( size == 0 ) {

					if ( line [ 0 ] == 0 && !first )
						break;

					continue;

				}

				if ( first ) {

					wanted_path = base::same_span ( line, base::min ( size, 6 ), "GET / " ) || base::same_span ( line, base::min ( size, 14 ), "GET /api.json " ) || base::same_span ( line, base::min ( size, 16 ), "GET /1/summary " );
					first = false;

				} else if ( local_host ( line, size, bound ) )
					trusted = true;

			}

			int32_t length = 0;

			{

				base::guard hold ( latest_lock );

				length = latest_size;
				base::copy ( body, latest, static_cast < size_t > ( length ) );

			}

			base::line reply;

			if ( !trusted )
				reply.add ( "HTTP/1.1 403 Forbidden\r\nContent-Length: 0\r\nConnection: close\r\n\r\n" );
			else if ( !wanted_path )
				reply.add ( "HTTP/1.1 404 Not Found\r\nContent-Length: 0\r\nConnection: close\r\n\r\n" );
			else
				reply.add ( "HTTP/1.1 200 OK\r\nContent-Type: application/json\r\nCache-Control: no-store\r\nContent-Length: " ).number ( static_cast < uint64_t > ( length ) ).add ( "\r\nConnection: close\r\n\r\n" );

			client->send ( reply.get ( ), reply.size ( ) );

			if ( trusted && wanted_path )
				client->send ( body, length );

			client->close ( );

		}

		delete [ ] body;
		delete [ ] line;
		delete client;

	}

}
