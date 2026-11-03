#include "base/text.hh"

namespace base {

	void text::put ( wchar_t value ) {

		if ( used < room - 1 ) {

			buffer [ used++ ] = value;
			buffer [ used ] = 0;

		}

	}

	text& text::add ( const wchar_t* value ) {

		for ( int32_t i = 0; value && value [ i ]; i++ )
			put ( value [ i ] );

		return *this;

	}

	text& text::number ( uint64_t value ) {

		wchar_t digits [ 24 ];
		int32_t count = 0;

		do {

			digits [ count++ ] = static_cast < wchar_t > ( L'0' + value % 10 );
			value /= 10;

		} while ( value );

		while ( count )
			put ( digits [ --count ] );

		return *this;

	}

	text& text::decimal ( double value, int32_t places ) {

		if ( value < 0.0 ) {

			put ( L'-' );
			value = -value;

		}

		uint64_t scale = 1;

		for ( int32_t i = 0; i < places; i++ )
			scale *= 10;

		uint64_t scaled = static_cast < uint64_t > ( value * static_cast < double > ( scale ) + 0.5 );

		number ( scaled / scale );

		if ( places > 0 ) {

			wchar_t fraction [ 24 ];
			uint64_t rest = scaled % scale;

			put ( L'.' );

			for ( int32_t i = places - 1; i >= 0; i-- ) {

				fraction [ i ] = static_cast < wchar_t > ( L'0' + rest % 10 );
				rest /= 10;

			}

			for ( int32_t i = 0; i < places; i++ )
				put ( fraction [ i ] );

		}

		return *this;

	}

	text& text::hex ( const uint8_t* bytes, size_t count, bool reversed ) {

		static const wchar_t digits [ ] = L"0123456789abcdef";

		for ( size_t i = 0; i < count; i++ ) {

			uint8_t value = bytes [ reversed ? count - 1 - i : i ];

			put ( digits [ value >> 4 ] );
			put ( digits [ value & 15 ] );

		}

		return *this;

	}

	text& text::rate ( double per_second, const wchar_t* unit ) {

		static const wchar_t* const prefixes [ ] = { L" ", L" k", L" M", L" G", L" T" };
		int32_t step = 0;

		while ( per_second >= 1000.0 && step < 4 ) {

			per_second /= 1000.0;
			step++;

		}

		return decimal ( per_second, per_second < 10.0 ? 2 : 1 ).add ( prefixes [ step ] ).add ( unit );

	}

}