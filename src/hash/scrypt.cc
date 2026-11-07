#include "hash/scrypt.hh"
#include "base/bits.hh"
#include "base/memory.hh"
#include <emmintrin.h>

namespace hash {

	hmac_sha256::hmac_sha256 ( const void* key, size_t size ) {

		uint8_t pad [ 64 ] = { };

		if ( size > 64 )
			sha256_once ( key, size, pad );
		else
			base::copy ( pad, key, size );

		for ( uint8_t& value : pad )
			value ^= 0x36;

		inner.add ( pad, 64 );

		for ( uint8_t& value : pad )
			value ^= 0x36 ^ 0x5C;

		outer.add ( pad, 64 );

	}

	void hmac_sha256::add ( const void* data, size_t size ) {

		inner.add ( data, size );

	}

	void hmac_sha256::end ( uint8_t* out ) {

		uint8_t digest [ sha256_size ];

		inner.end ( digest );
		outer.add ( digest, sha256_size );
		outer.end ( out );

	}

	void pbkdf2_sha256 ( const hmac_sha256& keyed, const void* salt, size_t salt_size, uint32_t rounds, uint8_t* out, size_t size ) {

		for ( uint32_t block = 1; size; block++ ) {

			hmac_sha256 first = keyed;
			uint8_t counter [ 4 ];
			uint8_t step [ sha256_size ];
			uint8_t sum [ sha256_size ];

			base::store_big ( counter, block );
			first.add ( salt, salt_size );
			first.add ( counter, 4 );
			first.end ( step );
			base::copy ( sum, step, sha256_size );

			for ( uint32_t i = 1; i < rounds; i++ ) {

				hmac_sha256 again = keyed;

				again.add ( step, sha256_size );
				again.end ( step );

				for ( uint32_t k = 0; k < sha256_size; k++ )
					sum [ k ] ^= step [ k ];

			}

			size_t take = base::min ( size, static_cast < size_t > ( sha256_size ) );

			base::copy ( out, sum, take );
			out += take;
			size -= take;

		}

	}

	void pbkdf2_sha256 ( const void* password, size_t password_size, const void* salt, size_t salt_size, uint32_t rounds, uint8_t* out, size_t size ) {

		hmac_sha256 keyed ( password, password_size );

		pbkdf2_sha256 ( keyed, salt, salt_size, rounds, out, size );

	}

	void salsa20_8 ( uint32_t* block ) {

		using base::rotate_left;

		uint32_t x [ 16 ];

		base::copy ( x, block, sizeof ( x ) );

		auto quarter = [ & ] ( int32_t a, int32_t b, int32_t c, int32_t d ) {

			x [ b ] ^= rotate_left ( x [ a ] + x [ d ], 7 );
			x [ c ] ^= rotate_left ( x [ b ] + x [ a ], 9 );
			x [ d ] ^= rotate_left ( x [ c ] + x [ b ], 13 );
			x [ a ] ^= rotate_left ( x [ d ] + x [ c ], 18 );

		};

		for ( int32_t i = 0; i < 8; i += 2 ) {

			quarter ( 0, 4, 8, 12 );
			quarter ( 5, 9, 13, 1 );
			quarter ( 10, 14, 2, 6 );
			quarter ( 15, 3, 7, 11 );
			quarter ( 0, 1, 2, 3 );
			quarter ( 5, 6, 7, 4 );
			quarter ( 10, 11, 8, 9 );
			quarter ( 15, 12, 13, 14 );

		}

		for ( int32_t i = 0; i < 16; i++ )
			block [ i ] += x [ i ];

	}

	static inline __m128i spin ( __m128i value, __m128i sum, int32_t count ) {

		return _mm_xor_si128 ( _mm_xor_si128 ( value, _mm_slli_epi32 ( sum, count ) ), _mm_srli_epi32 ( sum, 32 - count ) );

	}

	static inline void salsa_lanes ( __m128i* block ) {

		__m128i x0 = block [ 0 ];
		__m128i x1 = block [ 1 ];
		__m128i x2 = block [ 2 ];
		__m128i x3 = block [ 3 ];

		for ( int32_t i = 0; i < 8; i += 2 ) {

			x1 = spin ( x1, _mm_add_epi32 ( x0, x3 ), 7 );
			x2 = spin ( x2, _mm_add_epi32 ( x1, x0 ), 9 );
			x3 = spin ( x3, _mm_add_epi32 ( x2, x1 ), 13 );
			x0 = spin ( x0, _mm_add_epi32 ( x3, x2 ), 18 );
			x1 = _mm_shuffle_epi32 ( x1, 0x93 );
			x2 = _mm_shuffle_epi32 ( x2, 0x4E );
			x3 = _mm_shuffle_epi32 ( x3, 0x39 );
			x3 = spin ( x3, _mm_add_epi32 ( x0, x1 ), 7 );
			x2 = spin ( x2, _mm_add_epi32 ( x3, x0 ), 9 );
			x1 = spin ( x1, _mm_add_epi32 ( x2, x3 ), 13 );
			x0 = spin ( x0, _mm_add_epi32 ( x1, x2 ), 18 );
			x1 = _mm_shuffle_epi32 ( x1, 0x39 );
			x2 = _mm_shuffle_epi32 ( x2, 0x4E );
			x3 = _mm_shuffle_epi32 ( x3, 0x93 );

		}

		block [ 0 ] = _mm_add_epi32 ( block [ 0 ], x0 );
		block [ 1 ] = _mm_add_epi32 ( block [ 1 ], x1 );
		block [ 2 ] = _mm_add_epi32 ( block [ 2 ], x2 );
		block [ 3 ] = _mm_add_epi32 ( block [ 3 ], x3 );

	}

	static void lanes_in ( const uint8_t* bytes, __m128i* out ) {

		uint32_t words [ 16 ];
		alignas ( 16 ) uint32_t placed [ 16 ];

		for ( int32_t i = 0; i < 16; i++ )
			words [ i ] = base::load_little ( bytes + i * 4 );

		for ( int32_t i = 0; i < 16; i++ )
			placed [ i ] = words [ i * 5 % 16 ];

		for ( int32_t i = 0; i < 4; i++ )
			out [ i ] = _mm_load_si128 ( reinterpret_cast < const __m128i* > ( placed + i * 4 ) );

	}

	static void lanes_out ( const __m128i* in, uint8_t* bytes ) {

		alignas ( 16 ) uint32_t placed [ 16 ];

		for ( int32_t i = 0; i < 4; i++ )
			_mm_store_si128 ( reinterpret_cast < __m128i* > ( placed + i * 4 ), in [ i ] );

		for ( int32_t i = 0; i < 16; i++ )
			base::store_little ( bytes + i * 5 % 16 * 4, placed [ i ] );

	}

	static void block_mix ( const __m128i* in, __m128i* out, uint32_t rows ) {

		__m128i x [ 4 ];
		uint32_t blocks = rows * 2;

		for ( int32_t k = 0; k < 4; k++ )
			x [ k ] = in [ ( blocks - 1 ) * 4 + k ];

		for ( uint32_t i = 0; i < blocks; i++ ) {

			for ( int32_t k = 0; k < 4; k++ )
				x [ k ] = _mm_xor_si128 ( x [ k ], in [ i * 4 + k ] );

			salsa_lanes ( x );

			uint32_t place = i & 1 ? rows + i / 2 : i / 2;

			for ( int32_t k = 0; k < 4; k++ )
				out [ place * 4 + k ] = x [ k ];

		}

	}

	static void romix ( uint8_t* bytes, uint32_t cost, uint32_t rows, __m128i* table, __m128i* x, __m128i* y ) {

		uint32_t vectors = rows * 8;
		uint32_t last = ( rows * 2 - 1 ) * 4;

		for ( uint32_t i = 0; i < rows * 2; i++ )
			lanes_in ( bytes + i * 64, x + i * 4 );

		for ( uint32_t i = 0; i < cost; i++ ) {

			__m128i* slot = table + static_cast < size_t > ( i ) * vectors;

			for ( uint32_t k = 0; k < vectors; k++ )
				slot [ k ] = x [ k ];

			block_mix ( x, y, rows );

			__m128i* kept = x;

			x = y;
			y = kept;

		}

		for ( uint32_t i = 0; i < cost; i++ ) {

			uint32_t pick = static_cast < uint32_t > ( _mm_cvtsi128_si32 ( x [ last ] ) ) & ( cost - 1 );
			const __m128i* slot = table + static_cast < size_t > ( pick ) * vectors;

			for ( uint32_t k = 0; k < vectors; k++ )
				x [ k ] = _mm_xor_si128 ( x [ k ], slot [ k ] );

			block_mix ( x, y, rows );

			__m128i* kept = x;

			x = y;
			y = kept;

		}

		for ( uint32_t i = 0; i < rows * 2; i++ )
			lanes_out ( x + i * 4, bytes + i * 64 );

	}

	bool scrypt ( const void* password, size_t password_size, const void* salt, size_t salt_size, uint32_t cost, uint32_t rows, uint32_t parallel, uint8_t* out, size_t size ) {

		if ( cost < 2 || ( cost & ( cost - 1 ) ) || !rows || !parallel || rows > 64 || parallel > 64 )
			return false;

		size_t stripe = static_cast < size_t > ( rows ) * 128;
		uint8_t* blocks = static_cast < uint8_t* > ( base::allocate_pages ( stripe * parallel ) );
		__m128i* table = static_cast < __m128i* > ( base::allocate_pages ( stripe * cost + stripe * 2 ) );

		if ( !blocks || !table ) {

			base::release_pages ( blocks );
			base::release_pages ( table );

			return false;

		}

		hmac_sha256 keyed ( password, password_size );
		__m128i* x = table + static_cast < size_t > ( cost ) * rows * 8;
		__m128i* y = x + rows * 8;

		pbkdf2_sha256 ( keyed, salt, salt_size, 1, blocks, stripe * parallel );

		for ( uint32_t i = 0; i < parallel; i++ )
			romix ( blocks + stripe * i, cost, rows, table, x, y );

		pbkdf2_sha256 ( keyed, blocks, stripe * parallel, 1, out, size );
		base::release_pages ( table );
		base::release_pages ( blocks );

		return true;

	}

	void scrypt_header ( const uint8_t* header, uint8_t* out, void* scratch ) {

		uintptr_t at = ( reinterpret_cast < uintptr_t > ( scratch ) + 63 ) & ~static_cast < uintptr_t > ( 63 );
		__m128i* table = reinterpret_cast < __m128i* > ( at );
		__m128i* x = table + scrypt_cost * 8;
		__m128i* y = x + 8;
		hmac_sha256 keyed ( header, 80 );
		uint8_t blocks [ 128 ];

		pbkdf2_sha256 ( keyed, header, 80, 1, blocks, 128 );
		romix ( blocks, scrypt_cost, 1, table, x, y );
		pbkdf2_sha256 ( keyed, blocks, 128, 1, out, 32 );

	}

}
