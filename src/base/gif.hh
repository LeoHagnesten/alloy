#pragma once

#include "base/list.hh"

namespace base {

	class gif_writer {

	public:

		gif_writer ( ) = default;

		gif_writer ( const gif_writer& ) = delete;

		gif_writer& operator = ( const gif_writer& ) = delete;

		~gif_writer ( );

		bool open ( int32_t width, int32_t height );

		void learn ( const uint8_t* rgba );

		void seal ( );

		bool frame ( const uint8_t* rgba, uint32_t hundredths );

		bool finish ( list < uint8_t >& out );

		int32_t frames ( ) const {

			return written;

		}

	private:

		void write_codes ( const uint8_t* indices, int32_t count );

		int32_t wide = 0;
		int32_t tall = 0;
		uint32_t* histogram = nullptr;
		uint8_t* lookup = nullptr;
		uint8_t* previous = nullptr;
		uint8_t* current = nullptr;
		uint8_t* region = nullptr;
		uint8_t palette [ 768 ] = { };
		int32_t colors = 0;
		list < uint8_t > bytes;
		int32_t last_delay = -1;
		int32_t written = 0;

	};

}
