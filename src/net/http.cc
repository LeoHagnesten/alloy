#include "net/http.hh"
#include "net/socket.hh"
#include "version.hh"
#include "base/system/clock.hh"
#include "base/strings.hh"
#include "base/text.hh"

namespace net {

	static constexpr int32_t body_limit = 32 << 20;

	static char lowered ( char value ) {

		return value >= 'A' && value <= 'Z' ? static_cast < char > ( value + 32 ) : value;

	}

	static bool header_is ( const char* line, int32_t size, const char* name ) {

		int32_t length = base::length ( name );

		if ( size <= length || line [ length ] != ':' )
			return false;

		for ( int32_t i = 0; i < length; i++ ) {

			if ( lowered ( line [ i ] ) != name [ i ] )
				return false;

		}

		return true;

	}

	static const char* header_value ( const char* line, int32_t size, const char* name ) {

		int32_t at = base::length ( name ) + 1;

		while ( at < size && line [ at ] == ' ' )
			at++;

		return line + at;

	}

	bool parse_url ( const char* url, http_target& out ) {

		out = http_target { };

		const char* at = url;

		if ( base::starts_with ( at, "https://" ) ) {

			out.secure = true;
			out.port = 443;
			at += 8;

		} else if ( base::starts_with ( at, "http://" ) )
			at += 7;

		int32_t host = 0;

		while ( *at && *at != '/' && *at != ':' && *at != '?' && host < static_cast < int32_t > ( sizeof ( out.host ) ) - 1 )
			out.host [ host++ ] = *at++;

		out.host [ host ] = 0;

		if ( *at == ':' ) {

			uint32_t port = 0;

			at++;

			while ( *at >= '0' && *at <= '9' )
				port = port * 10 + static_cast < uint32_t > ( *at++ - '0' );

			if ( !port || port > 65535 )
				return false;

			out.port = static_cast < uint16_t > ( port );

		}

		if ( *at != '/' )
			out.path [ 0 ] = '/';

		base::copy_text ( out.path + ( *at != '/' ? 1 : 0 ), static_cast < int32_t > ( sizeof ( out.path ) ) - 1, at );

		return host > 0;

	}

	static int32_t find_break ( const base::list < char >& data ) {

		for ( int32_t i = 3; i < data.size ( ); i++ ) {

			if ( data [ i - 3 ] == '\r' && data [ i - 2 ] == '\n' && data [ i - 1 ] == '\r' && data [ i ] == '\n' )
				return i + 1;

		}

		return -1;

	}

	static bool unchunk ( const char* data, int32_t size, base::list < char >& out ) {

		int32_t at = 0;

		while ( at < size ) {

			uint32_t length = 0;
			bool digits = false;

			while ( at < size && base::hex_digit ( data [ at ] ) >= 0 ) {

				length = length * 16 + static_cast < uint32_t > ( base::hex_digit ( data [ at++ ] ) );
				digits = true;

			}

			while ( at < size && data [ at ] != '\n' )
				at++;

			at++;

			if ( !digits )
				return false;

			if ( !length )
				return true;

			if ( at + static_cast < int32_t > ( length ) > size || !out.append ( data + at, static_cast < int32_t > ( length ) ) )
				return false;

			at += static_cast < int32_t > ( length ) + 2;

		}

		return false;

	}

	bool http_request ( const char* method, const char* url, const char* headers, const char* body, int32_t body_size, http_reply& out, uint32_t timeout ) {

		http_target target;

		out.status = 0;
		out.body.clear ( );

		if ( !parse_url ( url, target ) )
			return false;

		connection* link = new connection;

		if ( !link->open ( target.host, target.port, timeout ) || ( target.secure && !link->start_tls ( target.host, timeout ) ) ) {

			delete link;

			return false;

		}

		base::writer < char, 4096 > request;

		request.add ( method ).put ( ' ' ).add ( target.path ).add ( " HTTP/1.1\r\nHost: " ).add ( target.host );

		if ( ( target.secure && target.port != 443 ) || ( !target.secure && target.port != 80 ) )
			request.put ( ':' ).number ( target.port );

		request.add ( "\r\nUser-Agent: Alloy/" ALLOY_VERSION_TEXT "\r\nAccept: application/json, text/plain, */*\r\nConnection: close\r\n" );

		if ( headers && headers [ 0 ] )
			request.add ( headers );

		if ( body )
			request.add ( "Content-Length: " ).number ( static_cast < uint64_t > ( body_size ) ).add ( "\r\n" );

		request.add ( "\r\n" );

		if ( !link->send ( request.get ( ), request.size ( ) ) || ( body && body_size && !link->send ( body, body_size ) ) ) {

			delete link;

			return false;

		}

		base::list < char >* raw = new base::list < char >;
		char* chunk = new char [ 16384 ];
		double began = base::seconds ( );
		int32_t head = -1;
		int64_t expected = -1;
		bool chunked = false;

		while ( base::seconds ( ) - began < timeout / 1000.0 && raw->size ( ) < body_limit ) {

			int32_t got = link->read ( chunk, 16384, 100 );

			if ( got < 0 )
				break;

			if ( got > 0 && !raw->append ( chunk, got ) )
				break;

			if ( head < 0 && ( head = find_break ( *raw ) ) >= 0 ) {

				const char* text = raw->data ( );
				int32_t line_start = 0;

				for ( int32_t i = 0; i + 1 < head; i++ ) {

					if ( text [ i ] != '\r' || text [ i + 1 ] != '\n' )
						continue;

					int32_t size = i - line_start;
					const char* line = text + line_start;

					if ( line_start == 0 ) {

						for ( int32_t k = 0; k < size; k++ ) {

							if ( line [ k ] == ' ' ) {

								out.status = static_cast < int32_t > ( base::read_number ( line + k + 1 ) );

								break;

							}

						}

					} else if ( header_is ( line, size, "content-length" ) )
						expected = static_cast < int64_t > ( base::read_number ( header_value ( line, size, "content-length" ) ) );
					else if ( header_is ( line, size, "transfer-encoding" ) )
						chunked = lowered ( *header_value ( line, size, "transfer-encoding" ) ) == 'c';

					line_start = i + 2;

				}

			}

			if ( head >= 0 && !chunked && expected >= 0 && raw->size ( ) - head >= expected )
				break;

			if ( head >= 0 && chunked && raw->size ( ) >= head + 5 && base::same_span ( raw->data ( ) + raw->size ( ) - 5, 5, "0\r\n\r\n" ) )
				break;

		}

		bool good = head >= 0 && out.status > 0;

		if ( good ) {

			const char* content = raw->data ( ) + head;
			int32_t size = raw->size ( ) - head;

			if ( chunked )
				good = unchunk ( content, size, out.body );
			else
				good = out.body.append ( content, expected >= 0 ? static_cast < int32_t > ( base::min ( static_cast < int64_t > ( size ), expected ) ) : size );

		}

		out.body.push ( 0 );
		delete [ ] chunk;
		delete raw;
		delete link;

		return good;

	}

	bool http_get ( const char* url, http_reply& out, uint32_t timeout ) {

		return http_request ( "GET", url, nullptr, nullptr, 0, out, timeout );

	}

}
