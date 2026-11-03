#include "hash/sha256.hh"

namespace hash {

	const uint32_t sha256_start [ 8 ] = {

		0x6A09E667, 0xBB67AE85, 0x3C6EF372, 0xA54FF53A, 0x510E527F, 0x9B05688C, 0x1F83D9AB, 0x5BE0CD19,

	};

	static const uint32_t rounds [ 64 ] = {

		0x428A2F98, 0x71374491, 0xB5C0FBCF, 0xE9B5DBA5, 0x3956C25B, 0x59F111F1, 0x923F82A4, 0xAB1C5ED5,
		0xD807AA98, 0x12835B01, 0x243185BE, 0x550C7DC3, 0x72BE5D74, 0x80DEB1FE, 0x9BDC06A7, 0xC19BF174,
		0xE49B69C1, 0xEFBE4786, 0x0FC19DC6, 0x240CA1CC, 0x2DE92C6F, 0x4A7484AA, 0x5CB0A9DC, 0x76F988DA,
		0x983E5152, 0xA831C66D, 0xB00327C8, 0xBF597FC7, 0xC6E00BF3, 0xD5A79147, 0x06CA6351, 0x14292967,
		0x27B70A85, 0x2E1B2138, 0x4D2C6DFC, 0x53380D13, 0x650A7354, 0x766A0ABB, 0x81C2C92E, 0x92722C85,
		0xA2BFE8A1, 0xA81A664B, 0xC24B8B70, 0xC76C51A3, 0xD192E819, 0xD6990624, 0xF40E3585, 0x106AA070,
		0x19A4C116, 0x1E376C08, 0x2748774C, 0x34B0BCB5, 0x391C0CB3, 0x4ED8AA4A, 0x5B9CCA4F, 0x682E6FF3,
		0x748F82EE, 0x78A5636F, 0x84C87814, 0x8CC70208, 0x90BEFFFA, 0xA4506CEB, 0xBEF9A3F7, 0xC67178F2,

	};

	static inline uint32_t turn ( uint32_t value, uint32_t count ) {

		return value >> count | value << ( 32 - count );

	}

	static inline uint32_t load_big ( const uint8_t* at ) {

		return static_cast < uint32_t > ( at [ 0 ] ) << 24 | static_cast < uint32_t > ( at [ 1 ] ) << 16 | static_cast < uint32_t > ( at [ 2 ] ) << 8 | at [ 3 ];

	}

	void sha256_block ( uint32_t* state, const uint8_t* block ) {

		uint32_t words [ 64 ];

		for ( int32_t i = 0; i < 16; i++ )
			words [ i ] = load_big ( block + i * 4 );

		for ( int32_t i = 16; i < 64; i++ ) {

			uint32_t low = turn ( words [ i - 15 ], 7 ) ^ turn ( words [ i - 15 ], 18 ) ^ words [ i - 15 ] >> 3;
			uint32_t high = turn ( words [ i - 2 ], 17 ) ^ turn ( words [ i - 2 ], 19 ) ^ words [ i - 2 ] >> 10;

			words [ i ] = words [ i - 16 ] + low + words [ i - 7 ] + high;

		}

		uint32_t a = state [ 0 ];
		uint32_t b = state [ 1 ];
		uint32_t c = state [ 2 ];
		uint32_t d = state [ 3 ];
		uint32_t e = state [ 4 ];
		uint32_t f = state [ 5 ];
		uint32_t g = state [ 6 ];
		uint32_t h = state [ 7 ];

		for ( int32_t i = 0; i < 64; i++ ) {

			uint32_t big_sum = turn ( e, 6 ) ^ turn ( e, 11 ) ^ turn ( e, 25 );
			uint32_t choose = ( e & f ) ^ ( ~e & g );
			uint32_t first = h + big_sum + choose + rounds [ i ] + words [ i ];
			uint32_t small_sum = turn ( a, 2 ) ^ turn ( a, 13 ) ^ turn ( a, 22 );
			uint32_t majority = ( a & b ) ^ ( a & c ) ^ ( b & c );
			uint32_t second = small_sum + majority;

			h = g;
			g = f;
			f = e;
			e = d + first;
			d = c;
			c = b;
			b = a;
			a = first + second;

		}

		state [ 0 ] += a;
		state [ 1 ] += b;
		state [ 2 ] += c;
		state [ 3 ] += d;
		state [ 4 ] += e;
		state [ 5 ] += f;
		state [ 6 ] += g;
		state [ 7 ] += h;

	}

	void sha256_digest ( const uint32_t* state, uint8_t* out ) {

		for ( int32_t i = 0; i < 8; i++ ) {

			out [ i * 4 ] = static_cast < uint8_t > ( state [ i ] >> 24 );
			out [ i * 4 + 1 ] = static_cast < uint8_t > ( state [ i ] >> 16 );
			out [ i * 4 + 2 ] = static_cast < uint8_t > ( state [ i ] >> 8 );
			out [ i * 4 + 3 ] = static_cast < uint8_t > ( state [ i ] );

		}

	}

	void sha256::begin ( ) {

		for ( int32_t i = 0; i < 8; i++ )
			state [ i ] = sha256_start [ i ];

		waiting_size = 0;
		total = 0;

	}

	void sha256::add ( const void* data, size_t size ) {

		const uint8_t* bytes = static_cast < const uint8_t* > ( data );

		total += size;

		while ( size ) {

			if ( !waiting_size && size >= 64 ) {

				sha256_block ( state, bytes );
				bytes += 64;
				size -= 64;

				continue;

			}

			uint32_t take = static_cast < uint32_t > ( base::min ( static_cast < size_t > ( 64 - waiting_size ), size ) );

			base::copy ( waiting + waiting_size, bytes, take );
			waiting_size += take;
			bytes += take;
			size -= take;

			if ( waiting_size == 64 ) {

				sha256_block ( state, waiting );
				waiting_size = 0;

			}

		}

	}

	void sha256::end ( uint8_t* out ) {

		uint64_t bits = total * 8;
		uint8_t tail [ 72 ] = { 0x80 };
		uint32_t pad = waiting_size < 56 ? 56 - waiting_size : 120 - waiting_size;

		for ( int32_t i = 0; i < 8; i++ )
			tail [ pad + i ] = static_cast < uint8_t > ( bits >> ( 56 - i * 8 ) );

		uint64_t kept = total;

		add ( tail, pad + 8 );
		total = kept;
		sha256_digest ( state, out );

	}

	void sha256_once ( const void* data, size_t size, uint8_t* out ) {

		sha256 hasher;

		hasher.add ( data, size );
		hasher.end ( out );

	}

	void sha256_twice ( const void* data, size_t size, uint8_t* out ) {

		uint8_t first [ sha256_size ];

		sha256_once ( data, size, first );
		sha256_once ( first, sha256_size, out );

	}

}