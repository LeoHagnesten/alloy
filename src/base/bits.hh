#pragma once

#include "base/base.hh"

namespace base {

	constexpr uint32_t rotate_right ( uint32_t value, uint32_t count ) {

		return value >> count | value << ( 32 - count );

	}

	constexpr uint32_t swap_bytes ( uint32_t value ) {

		return value >> 24 | ( value >> 8 & 0xFF00 ) | ( value << 8 & 0xFF0000 ) | value << 24;

	}

	inline uint32_t load_big ( const uint8_t* at ) {

		return static_cast < uint32_t > ( at [ 0 ] ) << 24 | static_cast < uint32_t > ( at [ 1 ] ) << 16 | static_cast < uint32_t > ( at [ 2 ] ) << 8 | at [ 3 ];

	}

	inline void store_big ( uint8_t* at, uint32_t value ) {

		at [ 0 ] = static_cast < uint8_t > ( value >> 24 );
		at [ 1 ] = static_cast < uint8_t > ( value >> 16 );
		at [ 2 ] = static_cast < uint8_t > ( value >> 8 );
		at [ 3 ] = static_cast < uint8_t > ( value );

	}

	inline uint32_t load_little ( const uint8_t* at ) {

		return static_cast < uint32_t > ( at [ 0 ] ) | static_cast < uint32_t > ( at [ 1 ] ) << 8 | static_cast < uint32_t > ( at [ 2 ] ) << 16 | static_cast < uint32_t > ( at [ 3 ] ) << 24;

	}

	inline uint64_t load_little64 ( const uint8_t* at ) {

		return static_cast < uint64_t > ( load_little ( at ) ) | static_cast < uint64_t > ( load_little ( at + 4 ) ) << 32;

	}

	inline void store_little ( uint8_t* at, uint32_t value ) {

		at [ 0 ] = static_cast < uint8_t > ( value );
		at [ 1 ] = static_cast < uint8_t > ( value >> 8 );
		at [ 2 ] = static_cast < uint8_t > ( value >> 16 );
		at [ 3 ] = static_cast < uint8_t > ( value >> 24 );

	}

	inline void reverse_bytes ( uint8_t* bytes, size_t count ) {

		for ( size_t i = 0; i < count / 2; i++ ) {

			uint8_t kept = bytes [ i ];

			bytes [ i ] = bytes [ count - 1 - i ];
			bytes [ count - 1 - i ] = kept;

		}

	}

	inline void reverse_words ( uint8_t* bytes, size_t count ) {

		for ( size_t i = 0; i + 4 <= count; i += 4 )
			reverse_bytes ( bytes + i, 4 );

	}

	inline uint32_t leading_zeros ( uint32_t value ) {

		return value ? static_cast < uint32_t > ( __builtin_clz ( value ) ) : 32;

	}

}
