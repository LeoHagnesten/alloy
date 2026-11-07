#include "hash/sha256.hh"
#include <immintrin.h>

namespace hash {

	__attribute__ ( ( target ( "sha,sse4.1,ssse3" ) ) )
	void sha256_block_extensions ( uint32_t* state, const uint8_t* block ) {

		const __m128i order = _mm_set_epi64x ( 0x0C0D0E0F08090A0Bll, 0x0405060700010203ll );
		__m128i swapped = _mm_shuffle_epi32 ( _mm_loadu_si128 ( reinterpret_cast < const __m128i* > ( state ) ), 0xB1 );
		__m128i reversed = _mm_shuffle_epi32 ( _mm_loadu_si128 ( reinterpret_cast < const __m128i* > ( state + 4 ) ), 0x1B );
		__m128i abef = _mm_alignr_epi8 ( swapped, reversed, 8 );
		__m128i cdgh = _mm_blend_epi16 ( reversed, swapped, 0xF0 );
		__m128i abef_start = abef;
		__m128i cdgh_start = cdgh;
		__m128i words [ 4 ];

		for ( int32_t group = 0; group < 16; group++ ) {

			__m128i& current = words [ group & 3 ];

			if ( group < 4 )
				current = _mm_shuffle_epi8 ( _mm_loadu_si128 ( reinterpret_cast < const __m128i* > ( block + group * 16 ) ), order );

			__m128i mixed = _mm_add_epi32 ( current, _mm_loadu_si128 ( reinterpret_cast < const __m128i* > ( sha256_rounds + group * 4 ) ) );

			cdgh = _mm_sha256rnds2_epu32 ( cdgh, abef, mixed );

			if ( group >= 3 && group <= 14 ) {

				__m128i& next = words [ ( group + 1 ) & 3 ];

				next = _mm_sha256msg2_epu32 ( _mm_add_epi32 ( next, _mm_alignr_epi8 ( current, words [ ( group + 3 ) & 3 ], 4 ) ), current );

			}

			abef = _mm_sha256rnds2_epu32 ( abef, cdgh, _mm_shuffle_epi32 ( mixed, 0x0E ) );

			if ( group >= 1 && group <= 12 ) {

				__m128i& earlier = words [ ( group + 3 ) & 3 ];

				earlier = _mm_sha256msg1_epu32 ( earlier, current );

			}

		}

		abef = _mm_add_epi32 ( abef, abef_start );
		cdgh = _mm_add_epi32 ( cdgh, cdgh_start );

		__m128i feba = _mm_shuffle_epi32 ( abef, 0x1B );
		__m128i dchg = _mm_shuffle_epi32 ( cdgh, 0xB1 );

		_mm_storeu_si128 ( reinterpret_cast < __m128i* > ( state ), _mm_blend_epi16 ( feba, dchg, 0xF0 ) );
		_mm_storeu_si128 ( reinterpret_cast < __m128i* > ( state + 4 ), _mm_alignr_epi8 ( dchg, feba, 8 ) );

	}

}
