#include "app/api.hh"
#include "net/socket.hh"
#include "base/system/clock.hh"
#include "base/memory.hh"
#include "base/strings.hh"
#include "base/text.hh"

namespace app {

	struct request {

		char method [ 8 ] = { };
		char path [ 128 ] = { };
		char token [ 80 ] = { };
		bool local_host = false;

	};

	bool status_api::start ( const api_options& options ) {

		stop ( );

		net::listener* open = new net::listener;

		if ( !open->open ( options.port, options.everyone ) ) {

			delete open;

			return false;

		}

		config = options;
		listener = open;
		bound = open->port ( );
		quit = 0;
		command = API_NONE;
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

	uint32_t status_api::take_command ( ) {

		return base::atomic_exchange ( command, static_cast < uint32_t > ( API_NONE ) );

	}

	unsigned long status_api::main ( void* context ) {

		static_cast < status_api* > ( context )->run ( );

		return 0;

	}

	static char lowered ( char value ) {

		return value >= 'A' && value <= 'Z' ? static_cast < char > ( value + 32 ) : value;

	}

	static bool header ( const char* line, int32_t size, const char* name, const char*& value ) {

		int32_t length = base::length ( name );

		if ( size <= length || line [ length ] != ':' )
			return false;

		for ( int32_t i = 0; i < length; i++ ) {

			if ( lowered ( line [ i ] ) != name [ i ] )
				return false;

		}

		value = line + length + 1;

		while ( *value == ' ' )
			value++;

		return true;

	}

	static void read_request_line ( const char* line, int32_t size, request& out ) {

		int32_t at = 0;
		int32_t used = 0;

		while ( at < size && line [ at ] != ' ' && used < 7 )
			out.method [ used++ ] = line [ at++ ];

		out.method [ used ] = 0;
		at++;
		used = 0;

		while ( at < size && line [ at ] != ' ' && line [ at ] != '?' && used < 127 )
			out.path [ used++ ] = line [ at++ ];

		out.path [ used ] = 0;

		if ( at < size && line [ at ] == '?' ) {

			at++;

			while ( at < size && line [ at ] != ' ' ) {

				if ( base::same_span ( line + at, base::min ( 6, size - at ), "token=" ) ) {

					at += 6;
					used = 0;

					while ( at < size && line [ at ] != ' ' && line [ at ] != '&' && used < 79 )
						out.token [ used++ ] = line [ at++ ];

					out.token [ used ] = 0;

				}

				while ( at < size && line [ at ] != ' ' && line [ at ] != '&' )
					at++;

				if ( at < size && line [ at ] == '&' )
					at++;

			}

		}

	}

	static bool local_host ( const char* value, uint16_t port ) {

		base::writer < char, 48 > first;
		base::writer < char, 48 > second;

		first.add ( "127.0.0.1:" ).number ( port );
		second.add ( "localhost:" ).number ( port );

		char wanted [ 48 ];
		int32_t size = 0;

		while ( value [ size ] && value [ size ] != ' ' && size < 47 ) {

			wanted [ size ] = lowered ( value [ size ] );
			size++;

		}

		wanted [ size ] = 0;

		return base::same_text ( wanted, first.get ( ) ) || base::same_text ( wanted, second.get ( ) );

	}

	void status_api::run ( ) {

		net::listener& server = *static_cast < net::listener* > ( listener );
		net::connection* client = new net::connection;
		char* line = new char [ 1024 ];
		char* body = new char [ api_room ];
		const char* page = dashboard_page ( );

		while ( !base::atomic_load ( quit ) ) {

			bool from_here = false;

			if ( !server.accept ( *client, 100, &from_here ) )
				continue;

			request asked;
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

				const char* value = nullptr;

				if ( first ) {

					read_request_line ( line, size, asked );
					first = false;

				} else if ( header ( line, size, "host", value ) )
					asked.local_host = local_host ( value, bound );
				else if ( header ( line, size, "authorization", value ) && base::starts_with ( value, "Bearer " ) )
					base::copy_text ( asked.token, sizeof ( asked.token ), value + 7 );

			}

			bool token_ok = config.token [ 0 ] && base::same_secret ( asked.token, config.token );
			bool readable = ( from_here && asked.local_host ) || token_ok;
			bool get = base::same_text ( asked.method, "GET" );
			bool post = base::same_text ( asked.method, "POST" );
			uint32_t order = API_NONE;
			base::line reply;
			int32_t length = 0;
			const char* content = nullptr;
			const char* type = "application/json";

			if ( post && base::same_text ( asked.path, "/1/pause" ) )
				order = API_PAUSE;
			else if ( post && base::same_text ( asked.path, "/1/resume" ) )
				order = API_RESUME;
			else if ( post && base::same_text ( asked.path, "/1/start" ) )
				order = API_START;
			else if ( post && base::same_text ( asked.path, "/1/stop" ) )
				order = API_STOP;

			if ( get && ( base::same_text ( asked.path, "/" ) || base::same_text ( asked.path, "/index.html" ) ) ) {

				content = page;
				length = base::length ( page );
				type = "text/html; charset=utf-8";

			} else if ( get && ( base::same_text ( asked.path, "/api.json" ) || base::same_text ( asked.path, "/1/summary" ) ) ) {

				if ( readable ) {

					base::guard hold ( latest_lock );

					length = latest_size;
					base::copy ( body, latest, static_cast < size_t > ( length ) );
					content = body;

				}

			}

			if ( content )
				reply.add ( "HTTP/1.1 200 OK\r\nContent-Type: " ).add ( type ).add ( "\r\nCache-Control: no-store\r\nContent-Length: " ).number ( static_cast < uint64_t > ( length ) ).add ( "\r\nConnection: close\r\n\r\n" );
			else if ( order != API_NONE && !config.control )
				reply.add ( "HTTP/1.1 403 Forbidden\r\nContent-Length: 0\r\nConnection: close\r\n\r\n" );
			else if ( order != API_NONE && !token_ok )
				reply.add ( "HTTP/1.1 401 Unauthorized\r\nWWW-Authenticate: Bearer\r\nContent-Length: 0\r\nConnection: close\r\n\r\n" );
			else if ( order != API_NONE ) {

				base::atomic_store ( command, order );
				reply.add ( "HTTP/1.1 200 OK\r\nContent-Type: application/json\r\nContent-Length: 11\r\nConnection: close\r\n\r\n{\"ok\":true}" );

			} else if ( get && ( base::same_text ( asked.path, "/api.json" ) || base::same_text ( asked.path, "/1/summary" ) ) )
				reply.add ( from_here && !config.token [ 0 ] ? "HTTP/1.1 403 Forbidden\r\nContent-Length: 0\r\nConnection: close\r\n\r\n" : "HTTP/1.1 401 Unauthorized\r\nWWW-Authenticate: Bearer\r\nContent-Length: 0\r\nConnection: close\r\n\r\n" );
			else
				reply.add ( "HTTP/1.1 404 Not Found\r\nContent-Length: 0\r\nConnection: close\r\n\r\n" );

			client->send ( reply.get ( ), reply.size ( ) );

			if ( content )
				client->send ( content, length );

			client->close ( );

		}

		delete [ ] body;
		delete [ ] line;
		delete client;

	}

}
