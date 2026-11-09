#include "chain/rlp.hh"
#include "base/strings.hh"

namespace chain {

	bool rlp_list::header ( uint8_t base, uint64_t size, base::list < uint8_t >& out ) const {

		if ( size < 56 )
			return out.push ( static_cast < uint8_t > ( base + size ) );

		uint8_t length [ 8 ];
		int32_t used = 0;

		for ( uint64_t rest = size; rest; rest >>= 8 )
			length [ used++ ] = static_cast < uint8_t > ( rest );

		if ( !out.push ( static_cast < uint8_t > ( base + 55 + used ) ) )
			return false;

		for ( int32_t i = used - 1; i >= 0; i-- ) {

			if ( !out.push ( length [ i ] ) )
				return false;

		}

		return true;

	}

	bool rlp_list::bytes ( const uint8_t* data, int32_t size ) {

		if ( size == 1 && data [ 0 ] < 0x80 )
			return body.push ( data [ 0 ] );

		return header ( 0x80, static_cast < uint64_t > ( size ), body ) && ( !size || body.append ( data, size ) );

	}

	bool rlp_list::hex ( const char* text, int32_t size, bool quantity ) {

		if ( size >= 2 && text [ 0 ] == '0' && ( text [ 1 ] == 'x' || text [ 1 ] == 'X' ) ) {

			text += 2;
			size -= 2;

		}

		base::list < uint8_t > raw;
		char padded [ 2 ] = { '0', 0 };
		int32_t at = 0;

		if ( !raw.set_size ( ( size + 1 ) / 2 ) )
			return false;

		if ( size % 2 ) {

			padded [ 1 ] = text [ 0 ];

			if ( !base::read_hex ( padded, 2, raw.data ( ), 1 ) )
				return false;

			at = 1;

		}

		if ( size - at && !base::read_hex ( text + at, size - at, raw.data ( ) + at, static_cast < size_t > ( ( size - at ) / 2 ) ) )
			return false;

		int32_t skip = 0;

		while ( quantity && skip < raw.size ( ) && !raw.data ( ) [ skip ] )
			skip++;

		return bytes ( raw.data ( ) + skip, raw.size ( ) - skip );

	}

	bool rlp_list::finish ( base::list < uint8_t >& out ) const {

		out.clear ( );

		return header ( 0xC0, static_cast < uint64_t > ( body.size ( ) ), out ) && ( !body.size ( ) || out.append ( body.data ( ), body.size ( ) ) );

	}

}
