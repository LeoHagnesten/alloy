#include "net/socket.hh"
#include "base/memory.hh"
#include "base/strings.hh"
#include "base/text.hh"
#include <winsock2.h>
#include <ws2tcpip.h>

namespace net {

	static constexpr uintptr_t closed = ~static_cast < uintptr_t > ( 0 );

	static bool wait_for ( uintptr_t handle, bool writing, uint32_t wait ) {

		fd_set set;
		fd_set failed;
		timeval limit = { static_cast < long > ( wait / 1000 ), static_cast < long > ( wait % 1000 * 1000 ) };

		set.fd_count = 1;
		set.fd_array [ 0 ] = static_cast < SOCKET > ( handle );
		failed.fd_count = 1;
		failed.fd_array [ 0 ] = static_cast < SOCKET > ( handle );

		int result = select ( 0, writing ? nullptr : &set, writing ? &set : nullptr, &failed, &limit );

		return result > 0 && set.fd_count == 1;

	}

	bool wait_any ( const uintptr_t* handles, int32_t count, uint32_t wait ) {

		fd_set set;
		timeval limit = { static_cast < long > ( wait / 1000 ), static_cast < long > ( wait % 1000 * 1000 ) };

		set.fd_count = 0;

		for ( int32_t i = 0; i < count && set.fd_count < FD_SETSIZE; i++ ) {

			if ( handles [ i ] != closed )
				set.fd_array [ set.fd_count++ ] = static_cast < SOCKET > ( handles [ i ] );

		}

		if ( !set.fd_count ) {

			Sleep ( wait );

			return false;

		}

		return select ( 0, &set, nullptr, nullptr, &limit ) > 0;

	}

	bool startup ( ) {

		static volatile long started = 0;
		WSADATA data;

		if ( started )
			return true;

		if ( WSAStartup ( MAKEWORD ( 2, 2 ), &data ) != 0 )
			return false;

		started = 1;

		return true;

	}

	bool split_address ( const wchar_t* text, char* host, int32_t room, uint16_t& port ) {

		char narrow [ 512 ];
		int32_t start = 0;

		base::narrow ( narrow, sizeof ( narrow ), text );

		for ( int32_t i = 0; narrow [ i ]; i++ ) {

			if ( narrow [ i ] == ':' && narrow [ i + 1 ] == '/' && narrow [ i + 2 ] == '/' ) {

				start = i + 3;

				break;

			}

		}

		while ( narrow [ start ] == ' ' )
			start++;

		int32_t end = start;

		while ( narrow [ end ] && narrow [ end ] != ':' && narrow [ end ] != '/' && narrow [ end ] != ' ' )
			end++;

		if ( end == start || narrow [ end ] != ':' )
			return false;

		uint64_t value = base::read_number ( narrow + end + 1 );

		if ( !value || value > 65535 )
			return false;

		base::copy_span ( host, room, narrow + start, end - start );
		port = static_cast < uint16_t > ( value );

		return true;

	}

	bool connection::open ( const char* host, uint16_t port, uint32_t timeout ) {

		close ( );

		if ( !startup ( ) )
			return false;

		base::writer < char, 8 > service;
		addrinfo hints = { };
		addrinfo* found = nullptr;

		service.number ( port );
		hints.ai_family = AF_UNSPEC;
		hints.ai_socktype = SOCK_STREAM;
		hints.ai_protocol = IPPROTO_TCP;

		if ( getaddrinfo ( host, service.get ( ), &hints, &found ) != 0 || !found )
			return false;

		for ( addrinfo* item = found; item && handle == closed; item = item->ai_next ) {

			SOCKET attempt = socket ( item->ai_family, item->ai_socktype, item->ai_protocol );

			if ( attempt == INVALID_SOCKET )
				continue;

			u_long blocking = 1;

			ioctlsocket ( attempt, FIONBIO, &blocking );

			bool linked = connect ( attempt, item->ai_addr, static_cast < int > ( item->ai_addrlen ) ) == 0;

			if ( !linked && WSAGetLastError ( ) == WSAEWOULDBLOCK && wait_for ( attempt, true, timeout ) ) {

				int problem = 0;
				int size = sizeof ( problem );

				linked = getsockopt ( attempt, SOL_SOCKET, SO_ERROR, reinterpret_cast < char* > ( &problem ), &size ) == 0 && problem == 0;

			}

			if ( !linked ) {

				closesocket ( attempt );

				continue;

			}

			blocking = 0;
			ioctlsocket ( attempt, FIONBIO, &blocking );
			adopt ( attempt );

		}

		freeaddrinfo ( found );

		return handle != closed;

	}

	void connection::adopt ( uintptr_t socket ) {

		BOOL fast = TRUE;

		close ( );
		handle = socket;
		setsockopt ( static_cast < SOCKET > ( handle ), IPPROTO_TCP, TCP_NODELAY, reinterpret_cast < const char* > ( &fast ), sizeof ( fast ) );

	}

	void connection::close ( ) {

		if ( handle != closed ) {

			shutdown ( static_cast < SOCKET > ( handle ), SD_BOTH );
			closesocket ( static_cast < SOCKET > ( handle ) );

		}

		handle = closed;
		pending_size = 0;

	}

	bool connection::send ( const char* data, int32_t size ) {

		while ( handle != closed && size > 0 ) {

			int sent = ::send ( static_cast < SOCKET > ( handle ), data, size, 0 );

			if ( sent <= 0 ) {

				close ( );

				return false;

			}

			data += sent;
			size -= sent;

		}

		return handle != closed;

	}

	bool connection::send_line ( const char* text ) {

		return send ( text, base::length ( text ) ) && send ( "\n", 1 );

	}

	int32_t connection::read_line ( char* out, int32_t room, uint32_t wait ) {

		while ( handle != closed ) {

			for ( int32_t i = 0; i < pending_size; i++ ) {

				if ( pending [ i ] != '\n' )
					continue;

				int32_t size = i;

				if ( size && pending [ size - 1 ] == '\r' )
					size--;

				base::copy_span ( out, room, pending, size );
				base::move ( pending, pending + i + 1, static_cast < size_t > ( pending_size - i - 1 ) );
				pending_size -= i + 1;

				return base::min ( size, room - 1 ) > 0 ? base::min ( size, room - 1 ) : 0;

			}

			if ( pending_size >= line_room ) {

				close ( );

				return -1;

			}

			if ( !wait_for ( handle, false, wait ) )
				return 0;

			int received = recv ( static_cast < SOCKET > ( handle ), pending + pending_size, line_room - pending_size, 0 );

			if ( received <= 0 ) {

				close ( );

				return -1;

			}

			pending_size += received;
			wait = 0;

		}

		return -1;

	}

	bool listener::open ( uint16_t port ) {

		close ( );

		if ( !startup ( ) )
			return false;

		SOCKET server = socket ( AF_INET, SOCK_STREAM, IPPROTO_TCP );

		if ( server == INVALID_SOCKET )
			return false;

		sockaddr_in address = { };
		int size = sizeof ( address );

		address.sin_family = AF_INET;
		address.sin_port = htons ( port );
		address.sin_addr.s_addr = htonl ( INADDR_LOOPBACK );

		if ( bind ( server, reinterpret_cast < sockaddr* > ( &address ), sizeof ( address ) ) != 0 || listen ( server, 8 ) != 0 || getsockname ( server, reinterpret_cast < sockaddr* > ( &address ), &size ) != 0 ) {

			closesocket ( server );

			return false;

		}

		handle = server;
		bound = ntohs ( address.sin_port );

		return true;

	}

	void listener::close ( ) {

		if ( handle != closed )
			closesocket ( static_cast < SOCKET > ( handle ) );

		handle = closed;
		bound = 0;

	}

	bool listener::accept ( connection& out, uint32_t wait ) {

		if ( handle == closed || !wait_for ( handle, false, wait ) )
			return false;

		SOCKET client = ::accept ( static_cast < SOCKET > ( handle ), nullptr, nullptr );

		if ( client == INVALID_SOCKET )
			return false;

		out.adopt ( client );

		return true;

	}

}
