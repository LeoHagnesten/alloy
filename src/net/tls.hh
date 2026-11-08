#pragma once

#include "base/base.hh"

namespace net {

	static constexpr int32_t tls_room = 32768;

	class tls_session {

	public:

		tls_session ( ) = default;

		tls_session ( const tls_session& ) = delete;

		tls_session& operator = ( const tls_session& ) = delete;

		~tls_session ( );

		bool handshake ( uintptr_t socket, const char* host, uint32_t timeout );

		bool trusted ( ) const {

			return chain_ok;

		}

		const uint8_t* fingerprint ( ) const {

			return print;

		}

		const wchar_t* trust_problem ( ) const;

		bool send ( uintptr_t socket, const char* data, int32_t size );

		int32_t receive ( uintptr_t socket, char* plain, int32_t room, bool socket_ready );

		bool buffered ( ) const;

		void close ( uintptr_t socket );

		long status ( ) const {

			return last_status;

		}

		const wchar_t* problem ( ) const;

		const wchar_t* protocol ( ) const;

	private:

		bool exchange ( uintptr_t socket, const char* host, uint32_t timeout );

		void inspect ( );

		int32_t drain ( char* plain, int32_t room );

		uint8_t credentials [ 16 ] = { };
		uint8_t context [ 16 ] = { };
		bool has_credentials = false;
		bool has_context = false;
		bool ready = false;
		bool chain_ok = false;
		unsigned long chain_error = 0;
		uint8_t print [ 32 ] = { };
		uint32_t header = 0;
		uint32_t trailer = 0;
		uint32_t largest = 0;
		uint32_t version = 0;
		long last_status = 0;
		char incoming [ tls_room ] = { };
		int32_t incoming_size = 0;
		char spare [ tls_room ] = { };
		int32_t spare_size = 0;
		wchar_t host_name [ 256 ] = { };

	};

}
