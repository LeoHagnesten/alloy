#include "hash/aes.hh"
#include "base/bits.hh"
#include "base/memory.hh"
#include <immintrin.h>

namespace hash {

	static const uint8_t fill_1r_keys [ 4 ] [ 16 ] = {

		{ 0x53, 0xa5, 0xac, 0x6d, 0x09, 0x66, 0x71, 0x62, 0x2b, 0x55, 0xb5, 0xdb, 0x17, 0x49, 0xf4, 0xb4 },
		{ 0x07, 0xaf, 0x7c, 0x6d, 0x0d, 0x71, 0x6a, 0x84, 0x78, 0xd3, 0x25, 0x17, 0x4e, 0xdc, 0xa1, 0x0d },
		{ 0xf1, 0x62, 0x12, 0x3f, 0xc6, 0x7e, 0x94, 0x9f, 0x4f, 0x79, 0xc0, 0xf4, 0x45, 0xe3, 0x20, 0x3e },
		{ 0x35, 0x81, 0xef, 0x6a, 0x7c, 0x31, 0xba, 0xb1, 0x88, 0x4c, 0x31, 0x16, 0x54, 0x91, 0x16, 0x49 },

	};

	static const uint8_t fill_4r_keys [ 8 ] [ 16 ] = {

		{ 0xdd, 0xaa, 0x21, 0x64, 0xdb, 0x3d, 0x83, 0xd1, 0x2b, 0x6d, 0x54, 0x2f, 0x3f, 0xd2, 0xe5, 0x99 },
		{ 0x50, 0x34, 0x0e, 0xb2, 0x55, 0x3f, 0x91, 0xb6, 0x53, 0x9d, 0xf7, 0x06, 0xe5, 0xcd, 0xdf, 0xa5 },
		{ 0x04, 0xd9, 0x3e, 0x5c, 0xaf, 0x7b, 0x5e, 0x51, 0x9f, 0x67, 0xa4, 0x0a, 0xbf, 0x02, 0x1c, 0x17 },
		{ 0x63, 0x37, 0x62, 0x85, 0x08, 0x5d, 0x8f, 0xe7, 0x85, 0x37, 0x67, 0xcd, 0x91, 0xd2, 0xde, 0xd8 },
		{ 0x73, 0x6f, 0x82, 0xb5, 0xa6, 0xa7, 0xd6, 0xe3, 0x6d, 0x8b, 0x51, 0x3d, 0xb4, 0xff, 0x9e, 0x22 },
		{ 0xf3, 0x6b, 0x56, 0xc7, 0xd9, 0xb3, 0x10, 0x9c, 0x4e, 0x4d, 0x02, 0xe9, 0xd2, 0xb7, 0x72, 0xb2 },
		{ 0xe7, 0xc9, 0x73, 0xf2, 0x8b, 0xa3, 0x65, 0xf7, 0x0a, 0x66, 0xa9, 0x2b, 0xa7, 0xef, 0x3b, 0xf6 },
		{ 0x09, 0xd6, 0x7c, 0x7a, 0xde, 0x39, 0x58, 0x91, 0xfd, 0xd1, 0x06, 0x0c, 0x2d, 0x76, 0xb0, 0xc0 },

	};

	static const uint8_t hash_start [ 4 ] [ 16 ] = {

		{ 0x0d, 0x2c, 0xb5, 0x92, 0xde, 0x56, 0xa8, 0x9f, 0x47, 0xdb, 0x82, 0xcc, 0xad, 0x3a, 0x98, 0xd7 },
		{ 0x6e, 0x99, 0x8d, 0x33, 0x98, 0xb7, 0xc7, 0x15, 0x5a, 0x12, 0x9e, 0xf5, 0x57, 0x80, 0xe7, 0xac },
		{ 0x17, 0x00, 0x77, 0x6a, 0xd0, 0xc7, 0x62, 0xae, 0x6b, 0x50, 0x79, 0x50, 0xe4, 0x7c, 0xa0, 0xe8 },
		{ 0x0c, 0x24, 0x0a, 0x63, 0x8d, 0x82, 0xad, 0x07, 0x05, 0x00, 0xa1, 0x79, 0x48, 0x49, 0x99, 0x7e },

	};

	static const uint8_t hash_extra [ 2 ] [ 16 ] = {

		{ 0x89, 0x83, 0xfa, 0xf6, 0x9f, 0x94, 0x24, 0x8b, 0xbf, 0x56, 0xdc, 0x90, 0x01, 0x02, 0x89, 0x06 },
		{ 0xd1, 0x63, 0xb2, 0x61, 0x3c, 0xe0, 0xf4, 0x51, 0xc6, 0x43, 0x10, 0xee, 0x9b, 0xf9, 0x18, 0xed },

	};

	struct round_tables {

		uint32_t forward [ 4 ] [ 256 ];
		uint32_t backward [ 4 ] [ 256 ];
		bool ready;
		bool hardware;

	};

	static round_tables tables;

	static uint8_t times ( uint8_t a, uint8_t b ) {

		uint8_t product = 0;

		while ( b ) {

			if ( b & 1 )
				product ^= a;

			a = static_cast < uint8_t > ( a << 1 ^ ( a & 0x80 ? 0x1B : 0 ) );
			b >>= 1;

		}

		return product;

	}

	static uint32_t column ( uint8_t a, uint8_t b, uint8_t c, uint8_t d ) {

		return static_cast < uint32_t > ( a ) | static_cast < uint32_t > ( b ) << 8 | static_cast < uint32_t > ( c ) << 16 | static_cast < uint32_t > ( d ) << 24;

	}

	void aes_prepare ( bool hardware ) {

		uint8_t box [ 256 ];
		uint8_t inverse_box [ 256 ];

		for ( int32_t x = 0; x < 256; x++ ) {

			uint8_t inverse = 0;

			for ( int32_t y = 1; y < 256 && x; y++ ) {

				if ( times ( static_cast < uint8_t > ( x ), static_cast < uint8_t > ( y ) ) == 1 )
					inverse = static_cast < uint8_t > ( y );

			}

			uint8_t value = inverse;

			for ( int32_t k = 1; k < 5; k++ )
				value ^= static_cast < uint8_t > ( inverse << k | inverse >> ( 8 - k ) );

			box [ x ] = value ^ 0x63;
			inverse_box [ box [ x ] ] = static_cast < uint8_t > ( x );

		}

		for ( int32_t x = 0; x < 256; x++ ) {

			uint8_t s = box [ x ];
			uint8_t i = inverse_box [ x ];
			uint8_t s2 = times ( s, 2 );
			uint8_t s3 = times ( s, 3 );
			uint8_t ie = times ( i, 14 );
			uint8_t i9 = times ( i, 9 );
			uint8_t id = times ( i, 13 );
			uint8_t ib = times ( i, 11 );

			tables.forward [ 0 ] [ x ] = column ( s2, s, s, s3 );
			tables.forward [ 1 ] [ x ] = column ( s3, s2, s, s );
			tables.forward [ 2 ] [ x ] = column ( s, s3, s2, s );
			tables.forward [ 3 ] [ x ] = column ( s, s, s3, s2 );
			tables.backward [ 0 ] [ x ] = column ( ie, i9, id, ib );
			tables.backward [ 1 ] [ x ] = column ( ib, ie, i9, id );
			tables.backward [ 2 ] [ x ] = column ( id, ib, ie, i9 );
			tables.backward [ 3 ] [ x ] = column ( i9, id, ib, ie );

		}

		tables.hardware = hardware;
		tables.ready = true;

	}

	bool aes_hardware ( ) {

		return tables.hardware;

	}

	bool aes_ready ( ) {

		return tables.ready;

	}

	void aes_encrypt_round ( uint8_t* block, const uint8_t* key ) {

		uint8_t in [ 16 ];

		base::copy ( in, block, 16 );

		for ( int32_t c = 0; c < 4; c++ ) {

			uint32_t value = tables.forward [ 0 ] [ in [ c * 4 ] ] ^ tables.forward [ 1 ] [ in [ ( c + 1 ) % 4 * 4 + 1 ] ] ^ tables.forward [ 2 ] [ in [ ( c + 2 ) % 4 * 4 + 2 ] ] ^ tables.forward [ 3 ] [ in [ ( c + 3 ) % 4 * 4 + 3 ] ];

			base::store_little ( block + c * 4, value ^ base::load_little ( key + c * 4 ) );

		}

	}

	void aes_decrypt_round ( uint8_t* block, const uint8_t* key ) {

		uint8_t in [ 16 ];

		base::copy ( in, block, 16 );

		for ( int32_t c = 0; c < 4; c++ ) {

			uint32_t value = tables.backward [ 0 ] [ in [ c * 4 ] ] ^ tables.backward [ 1 ] [ in [ ( c + 3 ) % 4 * 4 + 1 ] ] ^ tables.backward [ 2 ] [ in [ ( c + 2 ) % 4 * 4 + 2 ] ] ^ tables.backward [ 3 ] [ in [ ( c + 1 ) % 4 * 4 + 3 ] ];

			base::store_little ( block + c * 4, value ^ base::load_little ( key + c * 4 ) );

		}

	}

	__attribute__ ( ( target ( "aes,sse2" ) ) )
	static void fill_1r_hardware ( uint8_t* state, uint8_t* out, size_t size ) {

		__m128i k0 = _mm_loadu_si128 ( reinterpret_cast < const __m128i* > ( fill_1r_keys [ 0 ] ) );
		__m128i k1 = _mm_loadu_si128 ( reinterpret_cast < const __m128i* > ( fill_1r_keys [ 1 ] ) );
		__m128i k2 = _mm_loadu_si128 ( reinterpret_cast < const __m128i* > ( fill_1r_keys [ 2 ] ) );
		__m128i k3 = _mm_loadu_si128 ( reinterpret_cast < const __m128i* > ( fill_1r_keys [ 3 ] ) );
		__m128i s0 = _mm_loadu_si128 ( reinterpret_cast < const __m128i* > ( state ) );
		__m128i s1 = _mm_loadu_si128 ( reinterpret_cast < const __m128i* > ( state + 16 ) );
		__m128i s2 = _mm_loadu_si128 ( reinterpret_cast < const __m128i* > ( state + 32 ) );
		__m128i s3 = _mm_loadu_si128 ( reinterpret_cast < const __m128i* > ( state + 48 ) );

		for ( size_t at = 0; at < size; at += 64 ) {

			s0 = _mm_aesdec_si128 ( s0, k0 );
			s1 = _mm_aesenc_si128 ( s1, k1 );
			s2 = _mm_aesdec_si128 ( s2, k2 );
			s3 = _mm_aesenc_si128 ( s3, k3 );
			_mm_storeu_si128 ( reinterpret_cast < __m128i* > ( out + at ), s0 );
			_mm_storeu_si128 ( reinterpret_cast < __m128i* > ( out + at + 16 ), s1 );
			_mm_storeu_si128 ( reinterpret_cast < __m128i* > ( out + at + 32 ), s2 );
			_mm_storeu_si128 ( reinterpret_cast < __m128i* > ( out + at + 48 ), s3 );

		}

		_mm_storeu_si128 ( reinterpret_cast < __m128i* > ( state ), s0 );
		_mm_storeu_si128 ( reinterpret_cast < __m128i* > ( state + 16 ), s1 );
		_mm_storeu_si128 ( reinterpret_cast < __m128i* > ( state + 32 ), s2 );
		_mm_storeu_si128 ( reinterpret_cast < __m128i* > ( state + 48 ), s3 );

	}

	__attribute__ ( ( target ( "aes,sse2" ) ) )
	static void fill_4r_hardware ( const uint8_t* state, uint8_t* out, size_t size ) {

		__m128i keys [ 8 ];

		for ( int32_t i = 0; i < 8; i++ )
			keys [ i ] = _mm_loadu_si128 ( reinterpret_cast < const __m128i* > ( fill_4r_keys [ i ] ) );

		__m128i s0 = _mm_loadu_si128 ( reinterpret_cast < const __m128i* > ( state ) );
		__m128i s1 = _mm_loadu_si128 ( reinterpret_cast < const __m128i* > ( state + 16 ) );
		__m128i s2 = _mm_loadu_si128 ( reinterpret_cast < const __m128i* > ( state + 32 ) );
		__m128i s3 = _mm_loadu_si128 ( reinterpret_cast < const __m128i* > ( state + 48 ) );

		for ( size_t at = 0; at < size; at += 64 ) {

			for ( int32_t k = 0; k < 4; k++ ) {

				s0 = _mm_aesdec_si128 ( s0, keys [ k ] );
				s1 = _mm_aesenc_si128 ( s1, keys [ k ] );
				s2 = _mm_aesdec_si128 ( s2, keys [ k + 4 ] );
				s3 = _mm_aesenc_si128 ( s3, keys [ k + 4 ] );

			}

			_mm_storeu_si128 ( reinterpret_cast < __m128i* > ( out + at ), s0 );
			_mm_storeu_si128 ( reinterpret_cast < __m128i* > ( out + at + 16 ), s1 );
			_mm_storeu_si128 ( reinterpret_cast < __m128i* > ( out + at + 32 ), s2 );
			_mm_storeu_si128 ( reinterpret_cast < __m128i* > ( out + at + 48 ), s3 );

		}

	}

	__attribute__ ( ( target ( "aes,sse2" ) ) )
	static void hash_1r_hardware ( const uint8_t* input, size_t size, uint8_t* out ) {

		__m128i s0 = _mm_loadu_si128 ( reinterpret_cast < const __m128i* > ( hash_start [ 0 ] ) );
		__m128i s1 = _mm_loadu_si128 ( reinterpret_cast < const __m128i* > ( hash_start [ 1 ] ) );
		__m128i s2 = _mm_loadu_si128 ( reinterpret_cast < const __m128i* > ( hash_start [ 2 ] ) );
		__m128i s3 = _mm_loadu_si128 ( reinterpret_cast < const __m128i* > ( hash_start [ 3 ] ) );

		for ( size_t at = 0; at < size; at += 64 ) {

			s0 = _mm_aesenc_si128 ( s0, _mm_loadu_si128 ( reinterpret_cast < const __m128i* > ( input + at ) ) );
			s1 = _mm_aesdec_si128 ( s1, _mm_loadu_si128 ( reinterpret_cast < const __m128i* > ( input + at + 16 ) ) );
			s2 = _mm_aesenc_si128 ( s2, _mm_loadu_si128 ( reinterpret_cast < const __m128i* > ( input + at + 32 ) ) );
			s3 = _mm_aesdec_si128 ( s3, _mm_loadu_si128 ( reinterpret_cast < const __m128i* > ( input + at + 48 ) ) );

		}

		for ( int32_t k = 0; k < 2; k++ ) {

			__m128i key = _mm_loadu_si128 ( reinterpret_cast < const __m128i* > ( hash_extra [ k ] ) );

			s0 = _mm_aesenc_si128 ( s0, key );
			s1 = _mm_aesdec_si128 ( s1, key );
			s2 = _mm_aesenc_si128 ( s2, key );
			s3 = _mm_aesdec_si128 ( s3, key );

		}

		_mm_storeu_si128 ( reinterpret_cast < __m128i* > ( out ), s0 );
		_mm_storeu_si128 ( reinterpret_cast < __m128i* > ( out + 16 ), s1 );
		_mm_storeu_si128 ( reinterpret_cast < __m128i* > ( out + 32 ), s2 );
		_mm_storeu_si128 ( reinterpret_cast < __m128i* > ( out + 48 ), s3 );

	}

	void aes_fill_1r ( uint8_t* state, uint8_t* out, size_t size ) {

		if ( tables.hardware ) {

			fill_1r_hardware ( state, out, size );

			return;

		}

		for ( size_t at = 0; at < size; at += 64 ) {

			aes_decrypt_round ( state, fill_1r_keys [ 0 ] );
			aes_encrypt_round ( state + 16, fill_1r_keys [ 1 ] );
			aes_decrypt_round ( state + 32, fill_1r_keys [ 2 ] );
			aes_encrypt_round ( state + 48, fill_1r_keys [ 3 ] );
			base::copy ( out + at, state, 64 );

		}

	}

	void aes_fill_4r ( const uint8_t* state, uint8_t* out, size_t size ) {

		if ( tables.hardware ) {

			fill_4r_hardware ( state, out, size );

			return;

		}

		uint8_t work [ 64 ];

		base::copy ( work, state, 64 );

		for ( size_t at = 0; at < size; at += 64 ) {

			for ( int32_t k = 0; k < 4; k++ ) {

				aes_decrypt_round ( work, fill_4r_keys [ k ] );
				aes_encrypt_round ( work + 16, fill_4r_keys [ k ] );
				aes_decrypt_round ( work + 32, fill_4r_keys [ k + 4 ] );
				aes_encrypt_round ( work + 48, fill_4r_keys [ k + 4 ] );

			}

			base::copy ( out + at, work, 64 );

		}

	}

	void aes_hash_1r ( const uint8_t* input, size_t size, uint8_t* out ) {

		if ( tables.hardware ) {

			hash_1r_hardware ( input, size, out );

			return;

		}

		uint8_t work [ 64 ];

		base::copy ( work, hash_start, 64 );

		for ( size_t at = 0; at < size; at += 64 ) {

			aes_encrypt_round ( work, input + at );
			aes_decrypt_round ( work + 16, input + at + 16 );
			aes_encrypt_round ( work + 32, input + at + 32 );
			aes_decrypt_round ( work + 48, input + at + 48 );

		}

		for ( int32_t k = 0; k < 2; k++ ) {

			aes_encrypt_round ( work, hash_extra [ k ] );
			aes_decrypt_round ( work + 16, hash_extra [ k ] );
			aes_encrypt_round ( work + 32, hash_extra [ k ] );
			aes_decrypt_round ( work + 48, hash_extra [ k ] );

		}

		base::copy ( out, work, 64 );

	}

}
