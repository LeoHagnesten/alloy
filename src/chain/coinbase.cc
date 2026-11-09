#include "chain/coinbase.hh"
#include "chain/algorithm.hh"
#include "base/bits.hh"

namespace chain {

	bool read_varint ( const uint8_t* data, int32_t size, int32_t& at, uint64_t& out ) {

		if ( at >= size )
			return false;

		uint8_t first = data [ at++ ];

		if ( first < 0xFD ) {

			out = first;

			return true;

		}

		int32_t width = first == 0xFD ? 2 : ( first == 0xFE ? 4 : 8 );

		if ( at + width > size )
			return false;

		out = 0;

		for ( int32_t i = 0; i < width; i++ )
			out |= static_cast < uint64_t > ( data [ at + i ] ) << ( i * 8 );

		at += width;

		return true;

	}

	void write_varint ( uint8_t* out, int32_t& at, uint64_t value ) {

		if ( value < 0xFD ) {

			out [ at++ ] = static_cast < uint8_t > ( value );

			return;

		}

		int32_t width = value <= 0xFFFF ? 2 : ( value <= 0xFFFFFFFFull ? 4 : 8 );

		out [ at++ ] = width == 2 ? 0xFD : ( width == 4 ? 0xFE : 0xFF );

		for ( int32_t i = 0; i < width; i++ )
			out [ at++ ] = static_cast < uint8_t > ( value >> ( i * 8 ) );

	}

	bool coinbase_value ( const uint8_t* tx, int32_t size, uint64_t& total ) {

		int32_t at = 4;
		uint64_t inputs = 0;
		uint64_t outputs = 0;

		total = 0;

		if ( size > 6 && tx [ 4 ] == 0 && tx [ 5 ] == 1 )
			at += 2;

		if ( !read_varint ( tx, size, at, inputs ) || inputs != 1 )
			return false;

		uint64_t script = 0;

		at += 36;

		if ( !read_varint ( tx, size, at, script ) || script > 10000 )
			return false;

		at += static_cast < int32_t > ( script ) + 4;

		if ( !read_varint ( tx, size, at, outputs ) || outputs > 1000 )
			return false;

		for ( uint64_t i = 0; i < outputs; i++ ) {

			uint64_t length = 0;

			if ( at + 8 > size )
				return false;

			total += base::load_little64 ( tx + at );
			at += 8;

			if ( !read_varint ( tx, size, at, length ) || length > 10000 )
				return false;

			at += static_cast < int32_t > ( length );

		}

		return at <= size;

	}

	double share_hashes ( uint8_t algorithm, double difficulty ) {

		if ( algorithm == ALGORITHM_SCRYPT )
			return difficulty * 65536.0;

		if ( algorithm == ALGORITHM_RANDOMX )
			return difficulty;

		return difficulty * 4294967296.0;

	}

	double block_hashes ( uint8_t algorithm, double difficulty ) {

		return algorithm == ALGORITHM_RANDOMX || algorithm == ALGORITHM_ETHASH ? difficulty : difficulty * 4294967296.0;

	}

}
