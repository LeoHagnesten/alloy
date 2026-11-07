#include "base/strings.hh"

namespace base {

	int32_t length ( const wchar_t* text ) {

		int32_t count = 0;

		while ( text && text [ count ] )
			count++;

		return count;

	}

	int32_t length ( const char* text ) {

		int32_t count = 0;

		while ( text && text [ count ] )
			count++;

		return count;

	}

	bool same_text ( const wchar_t* a, const wchar_t* b ) {

		int32_t i = 0;

		for ( ; a [ i ] && b [ i ]; i++ ) {

			if ( a [ i ] != b [ i ] )
				return false;

		}

		return a [ i ] == b [ i ];

	}

	bool same_text ( const char* a, const char* b ) {

		int32_t i = 0;

		for ( ; a [ i ] && b [ i ]; i++ ) {

			if ( a [ i ] != b [ i ] )
				return false;

		}

		return a [ i ] == b [ i ];

	}

	bool same_span ( const char* span, int32_t size, const char* text ) {

		for ( int32_t i = 0; i < size; i++ ) {

			if ( !text [ i ] || text [ i ] != span [ i ] )
				return false;

		}

		return text [ size ] == 0;

	}

	bool starts_with ( const wchar_t* text, const wchar_t* prefix ) {

		for ( int32_t i = 0; prefix [ i ]; i++ ) {

			if ( text [ i ] != prefix [ i ] )
				return false;

		}

		return true;

	}

	void copy_text ( wchar_t* target, int32_t room, const wchar_t* source ) {

		int32_t i = 0;

		for ( ; source && source [ i ] && i < room - 1; i++ )
			target [ i ] = source [ i ];

		if ( room > 0 )
			target [ i ] = 0;

	}

	void copy_text ( char* target, int32_t room, const char* source ) {

		int32_t i = 0;

		for ( ; source && source [ i ] && i < room - 1; i++ )
			target [ i ] = source [ i ];

		if ( room > 0 )
			target [ i ] = 0;

	}

	void copy_span ( char* target, int32_t room, const char* source, int32_t size ) {

		int32_t count = min ( size, room - 1 );

		for ( int32_t i = 0; i < count; i++ )
			target [ i ] = source [ i ];

		if ( room > 0 )
			target [ max ( count, 0 ) ] = 0;

	}

	int32_t widen ( wchar_t* target, int32_t room, const char* source, int32_t size ) {

		const uint8_t* in = reinterpret_cast < const uint8_t* > ( source );
		int32_t end = size < 0 ? length ( source ) : size;
		int32_t used = 0;
		int32_t i = 0;

		while ( i < end && used < room - 1 ) {

			uint32_t point = in [ i ];
			int32_t extra = 0;

			if ( point >= 0xF0 ) {

				point &= 0x07;
				extra = 3;

			} else if ( point >= 0xE0 ) {

				point &= 0x0F;
				extra = 2;

			} else if ( point >= 0xC0 ) {

				point &= 0x1F;
				extra = 1;

			} else if ( point >= 0x80 )
				point = 0xFFFD;

			i++;

			for ( int32_t k = 0; k < extra && i < end; k++, i++ )
				point = point << 6 | ( in [ i ] & 0x3F );

			if ( point >= 0x10000 ) {

				if ( used + 2 >= room )
					break;

				point -= 0x10000;
				target [ used++ ] = static_cast < wchar_t > ( 0xD800 + ( point >> 10 ) );
				target [ used++ ] = static_cast < wchar_t > ( 0xDC00 + ( point & 0x3FF ) );

			} else
				target [ used++ ] = static_cast < wchar_t > ( point );

		}

		if ( room > 0 )
			target [ used ] = 0;

		return used;

	}

	int32_t narrow ( char* target, int32_t room, const wchar_t* source ) {

		int32_t used = 0;

		for ( int32_t i = 0; source && source [ i ]; i++ ) {

			uint32_t point = source [ i ];

			if ( point >= 0xD800 && point < 0xDC00 && source [ i + 1 ] >= 0xDC00 && source [ i + 1 ] < 0xE000 ) {

				point = 0x10000 + ( ( point - 0xD800 ) << 10 ) + ( source [ i + 1 ] - 0xDC00 );
				i++;

			}

			uint8_t bytes [ 4 ];
			int32_t count = 0;

			if ( point < 0x80 )
				bytes [ count++ ] = static_cast < uint8_t > ( point );
			else if ( point < 0x800 ) {

				bytes [ count++ ] = static_cast < uint8_t > ( 0xC0 | point >> 6 );
				bytes [ count++ ] = static_cast < uint8_t > ( 0x80 | ( point & 0x3F ) );

			} else if ( point < 0x10000 ) {

				bytes [ count++ ] = static_cast < uint8_t > ( 0xE0 | point >> 12 );
				bytes [ count++ ] = static_cast < uint8_t > ( 0x80 | ( point >> 6 & 0x3F ) );
				bytes [ count++ ] = static_cast < uint8_t > ( 0x80 | ( point & 0x3F ) );

			} else {

				bytes [ count++ ] = static_cast < uint8_t > ( 0xF0 | point >> 18 );
				bytes [ count++ ] = static_cast < uint8_t > ( 0x80 | ( point >> 12 & 0x3F ) );
				bytes [ count++ ] = static_cast < uint8_t > ( 0x80 | ( point >> 6 & 0x3F ) );
				bytes [ count++ ] = static_cast < uint8_t > ( 0x80 | ( point & 0x3F ) );

			}

			if ( used + count >= room )
				break;

			for ( int32_t k = 0; k < count; k++ )
				target [ used++ ] = static_cast < char > ( bytes [ k ] );

		}

		if ( room > 0 )
			target [ used ] = 0;

		return used;

	}

	int32_t hex_digit ( char value ) {

		if ( value >= '0' && value <= '9' )
			return value - '0';

		if ( value >= 'a' && value <= 'f' )
			return value - 'a' + 10;

		if ( value >= 'A' && value <= 'F' )
			return value - 'A' + 10;

		return -1;

	}

	bool read_hex ( const char* text, uint8_t* out, size_t count ) {

		return length ( text ) == static_cast < int32_t > ( count * 2 ) && read_hex ( text, static_cast < int32_t > ( count * 2 ), out, count );

	}

	bool read_hex ( const char* text, int32_t size, uint8_t* out, size_t count ) {

		if ( size != static_cast < int32_t > ( count * 2 ) )
			return false;

		for ( size_t i = 0; i < count; i++ ) {

			int32_t high = hex_digit ( text [ i * 2 ] );
			int32_t low = high < 0 ? -1 : hex_digit ( text [ i * 2 + 1 ] );

			if ( low < 0 )
				return false;

			out [ i ] = static_cast < uint8_t > ( high << 4 | low );

		}

		return true;

	}

	void write_hex ( char* out, const uint8_t* bytes, size_t count ) {

		static const char digits [ ] = "0123456789abcdef";

		for ( size_t i = 0; i < count; i++ ) {

			out [ i * 2 ] = digits [ bytes [ i ] >> 4 ];
			out [ i * 2 + 1 ] = digits [ bytes [ i ] & 15 ];

		}

		out [ count * 2 ] = 0;

	}

	bool read_hex32 ( const char* text, int32_t size, uint32_t& out ) {

		uint8_t bytes [ 4 ];

		if ( !read_hex ( text, size, bytes, 4 ) )
			return false;

		out = static_cast < uint32_t > ( bytes [ 0 ] ) << 24 | static_cast < uint32_t > ( bytes [ 1 ] ) << 16 | static_cast < uint32_t > ( bytes [ 2 ] ) << 8 | bytes [ 3 ];

		return true;

	}

	uint64_t read_number ( const wchar_t* text ) {

		uint64_t value = 0;

		for ( int32_t i = 0; text && text [ i ] >= L'0' && text [ i ] <= L'9'; i++ )
			value = value * 10 + static_cast < uint64_t > ( text [ i ] - L'0' );

		return value;

	}

	uint64_t read_number ( const char* text ) {

		uint64_t value = 0;

		for ( int32_t i = 0; text && text [ i ] >= '0' && text [ i ] <= '9'; i++ )
			value = value * 10 + static_cast < uint64_t > ( text [ i ] - '0' );

		return value;

	}

	template < typename unit >
	static double parse_decimal ( const unit* text, int32_t size ) {

		double value = 0.0;
		double sign = 1.0;
		int32_t i = 0;

		if ( i < size && ( text [ i ] == '-' || text [ i ] == '+' ) ) {

			sign = text [ i ] == '-' ? -1.0 : 1.0;
			i++;

		}

		for ( ; i < size && text [ i ] >= '0' && text [ i ] <= '9'; i++ )
			value = value * 10.0 + static_cast < double > ( text [ i ] - '0' );

		if ( i < size && text [ i ] == '.' ) {

			double place = 0.1;

			for ( i++; i < size && text [ i ] >= '0' && text [ i ] <= '9'; i++ ) {

				value += static_cast < double > ( text [ i ] - '0' ) * place;
				place *= 0.1;

			}

		}

		if ( i < size && ( text [ i ] == 'e' || text [ i ] == 'E' ) ) {

			int32_t power = 0;
			bool negative = false;

			i++;

			if ( i < size && ( text [ i ] == '-' || text [ i ] == '+' ) ) {

				negative = text [ i ] == '-';
				i++;

			}

			for ( ; i < size && text [ i ] >= '0' && text [ i ] <= '9'; i++ )
				power = power * 10 + ( text [ i ] - '0' );

			for ( int32_t k = 0; k < power && k < 400; k++ )
				value = negative ? value / 10.0 : value * 10.0;

		}

		return value * sign;

	}

	double read_decimal ( const wchar_t* text ) {

		return parse_decimal ( text, length ( text ) );

	}

	double read_decimal ( const char* text, int32_t size ) {

		return parse_decimal ( text, size );

	}

}
