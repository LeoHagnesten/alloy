#pragma once

#include "base/base.hh"

namespace base {

	template < typename unit, int32_t room >
	class writer {

	public:

		writer& put ( unit value ) {

			if ( used < room - 1 ) {

				buffer [ used++ ] = value;
				buffer [ used ] = 0;

			}

			return *this;

		}

		writer& add ( const unit* value ) {

			for ( int32_t i = 0; value && value [ i ]; i++ )
				put ( value [ i ] );

			return *this;

		}

		writer& add ( const unit* value, int32_t count ) {

			for ( int32_t i = 0; value && i < count; i++ )
				put ( value [ i ] );

			return *this;

		}

		writer& number ( uint64_t value ) {

			unit digits [ 24 ];
			int32_t count = 0;

			do {

				digits [ count++ ] = static_cast < unit > ( '0' + value % 10 );
				value /= 10;

			} while ( value );

			while ( count )
				put ( digits [ --count ] );

			return *this;

		}

		writer& padded ( uint64_t value, int32_t width ) {

			uint64_t scale = 1;

			for ( int32_t i = 1; i < width; i++ )
				scale *= 10;

			while ( scale > 1 && value < scale ) {

				put ( '0' );
				scale /= 10;

			}

			return number ( value );

		}

		writer& grouped ( uint64_t value ) {

			unit digits [ 32 ];
			int32_t count = 0;

			do {

				if ( count % 4 == 3 )
					digits [ count++ ] = ',';

				digits [ count++ ] = static_cast < unit > ( '0' + value % 10 );
				value /= 10;

			} while ( value );

			while ( count )
				put ( digits [ --count ] );

			return *this;

		}

		writer& decimal ( double value, int32_t places ) {

			if ( value < 0.0 ) {

				put ( '-' );
				value = -value;

			}

			uint64_t scale = 1;

			for ( int32_t i = 0; i < places; i++ )
				scale *= 10;

			uint64_t scaled = static_cast < uint64_t > ( value * static_cast < double > ( scale ) + 0.5 );

			number ( scaled / scale );

			if ( places > 0 ) {

				put ( '.' );
				padded ( scaled % scale, places );

			}

			return *this;

		}

		writer& hex ( const uint8_t* bytes, size_t count, bool reversed ) {

			static constexpr char digits [ ] = "0123456789abcdef";

			for ( size_t i = 0; i < count; i++ ) {

				uint8_t value = bytes [ reversed ? count - 1 - i : i ];

				put ( static_cast < unit > ( digits [ value >> 4 ] ) );
				put ( static_cast < unit > ( digits [ value & 15 ] ) );

			}

			return *this;

		}

		writer& hex32 ( uint32_t value ) {

			uint8_t bytes [ 4 ] = { static_cast < uint8_t > ( value >> 24 ), static_cast < uint8_t > ( value >> 16 ), static_cast < uint8_t > ( value >> 8 ), static_cast < uint8_t > ( value ) };

			return hex ( bytes, 4, false );

		}

		writer& trimmed ( double value, int32_t places ) {

			int32_t mark = used;

			decimal ( value, places );

			bool dotted = false;

			for ( int32_t i = mark; i < used; i++ )
				dotted = dotted || buffer [ i ] == static_cast < unit > ( '.' );

			while ( dotted && used > mark && buffer [ used - 1 ] == static_cast < unit > ( '0' ) )
				used--;

			if ( used > mark && buffer [ used - 1 ] == static_cast < unit > ( '.' ) )
				used--;

			buffer [ used ] = 0;

			return *this;

		}

		writer& hex64 ( uint64_t value ) {

			return hex32 ( static_cast < uint32_t > ( value >> 32 ) ).hex32 ( static_cast < uint32_t > ( value ) );

		}

		writer& scaled ( double value, const unit* name ) {

			static constexpr char prefixes [ ] = { 0, 'k', 'M', 'G', 'T', 'P', 'E' };
			int32_t step = 0;

			while ( value >= 1000.0 && step < 6 ) {

				value /= 1000.0;
				step++;

			}

			decimal ( value, value < 10.0 ? 2 : ( value < 100.0 ? 1 : 0 ) ).put ( ' ' );

			if ( step )
				put ( static_cast < unit > ( prefixes [ step ] ) );

			return add ( name );

		}

		writer& compact ( double value ) {

			static constexpr char prefixes [ ] = { 0, 'k', 'M', 'G', 'T', 'P', 'E' };
			int32_t step = 0;

			if ( value < 1000.0 )
				return decimal ( value, value >= 100.0 ? 0 : ( value >= 10.0 ? 1 : ( value >= 1.0 ? 2 : ( value >= 0.01 ? 3 : 5 ) ) ) );

			while ( value >= 1000.0 && step < 6 ) {

				value /= 1000.0;
				step++;

			}

			return decimal ( value, value < 10.0 ? 2 : ( value < 100.0 ? 1 : 0 ) ).put ( static_cast < unit > ( prefixes [ step ] ) );

		}

		writer& clock ( uint32_t hour, uint32_t minute, uint32_t second ) {

			return padded ( hour, 2 ).put ( ':' ).padded ( minute, 2 ).put ( ':' ).padded ( second, 2 );

		}

		writer& duration ( double seconds ) {

			uint64_t whole = static_cast < uint64_t > ( seconds < 0.0 ? 0.0 : seconds );
			uint64_t hours = whole / 3600;
			uint64_t minutes = whole / 60 % 60;

			if ( hours )
				return number ( hours ).put ( ':' ).padded ( minutes, 2 ).put ( ':' ).padded ( whole % 60, 2 );

			return number ( minutes ).put ( ':' ).padded ( whole % 60, 2 );

		}

		const unit* get ( ) const {

			return buffer;

		}

		int32_t size ( ) const {

			return used;

		}

		bool full ( ) const {

			return used >= room - 1;

		}

		void clear ( ) {

			used = 0;
			buffer [ 0 ] = 0;

		}

	private:

		unit buffer [ room ] = { };
		int32_t used = 0;

	};

	using text = writer < wchar_t, 512 >;

	using line = writer < char, 4096 >;

}
