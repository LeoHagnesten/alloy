#pragma once

#include "base/base.hh"

namespace net {

	static constexpr int32_t line_room = 16384;

	bool startup ( );

	bool split_address ( const wchar_t* text, char* host, int32_t room, uint16_t& port );

	bool wait_any ( const uintptr_t* handles, int32_t count, uint32_t wait );

	class connection {

	public:

		connection ( ) = default;

		connection ( const connection& ) = delete;

		connection& operator = ( const connection& ) = delete;

		~connection ( ) {

			close ( );

		}

		bool open ( const char* host, uint16_t port, uint32_t timeout );

		void adopt ( uintptr_t socket );

		void close ( );

		bool is_open ( ) const {

			return handle != ~static_cast < uintptr_t > ( 0 );

		}

		uintptr_t native ( ) const {

			return handle;

		}

		bool send ( const char* data, int32_t size );

		bool send_line ( const char* text );

		int32_t read_line ( char* out, int32_t room, uint32_t wait );

	private:

		uintptr_t handle = ~static_cast < uintptr_t > ( 0 );
		char pending [ line_room ] = { };
		int32_t pending_size = 0;

	};

	class listener {

	public:

		~listener ( ) {

			close ( );

		}

		bool open ( uint16_t port );

		void close ( );

		uint16_t port ( ) const {

			return bound;

		}

		uintptr_t native ( ) const {

			return handle;

		}

		bool accept ( connection& out, uint32_t wait );

	private:

		uintptr_t handle = ~static_cast < uintptr_t > ( 0 );
		uint16_t bound = 0;

	};

}
