#include "hash/blake2b.hh"
#include "base/bits.hh"
#include "base/memory.hh"

namespace hash {

	static constexpr uint64_t start [ 8 ] = {

		0x6A09E667F3BCC908ull, 0xBB67AE8584CAA73Bull, 0x3C6EF372FE94F82Bull, 0xA54FF53A5F1D36F1ull,
		0x510E527FADE682D1ull, 0x9B05688C2B3E6C1Full, 0x1F83D9ABFB41BD6Bull, 0x5BE0CD19137E2179ull,

	};

	static constexpr uint8_t order [ 12 ] [ 16 ] = {

		{ 0, 1, 2, 3, 4, 5, 6, 7, 8, 9, 10, 11, 12, 13, 14, 15 },
		{ 14, 10, 4, 8, 9, 15, 13, 6, 1, 12, 0, 2, 11, 7, 5, 3 },
		{ 11, 8, 12, 0, 5, 2, 15, 13, 10, 14, 3, 6, 7, 1, 9, 4 },
		{ 7, 9, 3, 1, 13, 12, 11, 14, 2, 6, 5, 10, 4, 0, 15, 8 },
		{ 9, 0, 5, 7, 2, 4, 10, 15, 14, 1, 11, 12, 6, 8, 3, 13 },
		{ 2, 12, 6, 10, 0, 11, 8, 3, 4, 13, 7, 5, 15, 14, 1, 9 },
		{ 12, 5, 1, 15, 14, 13, 4, 10, 0, 7, 6, 3, 9, 2, 8, 11 },
		{ 13, 11, 7, 14, 12, 1, 3, 9, 5, 0, 15, 4, 8, 6, 2, 10 },
		{ 6, 15, 14, 9, 11, 3, 0, 8, 12, 2, 13, 7, 1, 4, 10, 5 },
		{ 10, 2, 8, 4, 7, 6, 1, 5, 15, 11, 9, 14, 3, 12, 13, 0 },
		{ 0, 1, 2, 3, 4, 5, 6, 7, 8, 9, 10, 11, 12, 13, 14, 15 },
		{ 14, 10, 4, 8, 9, 15, 13, 6, 1, 12, 0, 2, 11, 7, 5, 3 },

	};

	static inline uint64_t turn ( uint64_t value, uint32_t count ) {

		return value >> count | value << ( 64 - count );

	}

	blake2b::blake2b ( uint32_t output_size ) {

		size = base::clamp ( output_size, 1u, 64u );

		for ( int32_t i = 0; i < 8; i++ )
			state [ i ] = start [ i ];

		state [ 0 ] ^= 0x01010000ull ^ size;

	}

	blake2b::blake2b ( uint32_t output_size, const void* key, size_t key_size ) : blake2b ( output_size ) {

		uint8_t block [ 128 ] = { };

		key_size = base::min ( key_size, static_cast < size_t > ( 64 ) );

		if ( !key_size )
			return;

		state [ 0 ] ^= static_cast < uint64_t > ( key_size ) << 8;
		base::copy ( block, key, key_size );
		add ( block, sizeof ( block ) );
		base::zero ( block, sizeof ( block ) );

	}

	void blake2b::compress ( const uint8_t* block, bool last ) {

		uint64_t words [ 16 ];
		uint64_t v [ 16 ];

		for ( int32_t i = 0; i < 16; i++ )
			words [ i ] = base::load_little64 ( block + i * 8 );

		for ( int32_t i = 0; i < 8; i++ ) {

			v [ i ] = state [ i ];
			v [ i + 8 ] = start [ i ];

		}

		v [ 12 ] ^= counter;

		if ( last )
			v [ 14 ] = ~v [ 14 ];

		auto mix = [ & ] ( int32_t a, int32_t b, int32_t c, int32_t d, uint64_t x, uint64_t y ) {

			v [ a ] = v [ a ] + v [ b ] + x;
			v [ d ] = turn ( v [ d ] ^ v [ a ], 32 );
			v [ c ] = v [ c ] + v [ d ];
			v [ b ] = turn ( v [ b ] ^ v [ c ], 24 );
			v [ a ] = v [ a ] + v [ b ] + y;
			v [ d ] = turn ( v [ d ] ^ v [ a ], 16 );
			v [ c ] = v [ c ] + v [ d ];
			v [ b ] = turn ( v [ b ] ^ v [ c ], 63 );

		};

		for ( int32_t round = 0; round < 12; round++ ) {

			const uint8_t* s = order [ round ];

			mix ( 0, 4, 8, 12, words [ s [ 0 ] ], words [ s [ 1 ] ] );
			mix ( 1, 5, 9, 13, words [ s [ 2 ] ], words [ s [ 3 ] ] );
			mix ( 2, 6, 10, 14, words [ s [ 4 ] ], words [ s [ 5 ] ] );
			mix ( 3, 7, 11, 15, words [ s [ 6 ] ], words [ s [ 7 ] ] );
			mix ( 0, 5, 10, 15, words [ s [ 8 ] ], words [ s [ 9 ] ] );
			mix ( 1, 6, 11, 12, words [ s [ 10 ] ], words [ s [ 11 ] ] );
			mix ( 2, 7, 8, 13, words [ s [ 12 ] ], words [ s [ 13 ] ] );
			mix ( 3, 4, 9, 14, words [ s [ 14 ] ], words [ s [ 15 ] ] );

		}

		for ( int32_t i = 0; i < 8; i++ )
			state [ i ] ^= v [ i ] ^ v [ i + 8 ];

	}

	void blake2b::add ( const void* data, size_t length ) {

		const uint8_t* bytes = static_cast < const uint8_t* > ( data );

		while ( length ) {

			if ( waiting_size == 128 ) {

				counter += 128;
				compress ( waiting, false );
				waiting_size = 0;

			}

			uint32_t take = static_cast < uint32_t > ( base::min ( static_cast < size_t > ( 128 - waiting_size ), length ) );

			base::copy ( waiting + waiting_size, bytes, take );
			waiting_size += take;
			bytes += take;
			length -= take;

		}

	}

	void blake2b::end ( void* out ) {

		uint8_t full [ 64 ];

		counter += waiting_size;
		base::zero ( waiting + waiting_size, 128 - waiting_size );
		compress ( waiting, true );

		for ( int32_t i = 0; i < 8; i++ ) {

			for ( int32_t k = 0; k < 8; k++ )
				full [ i * 8 + k ] = static_cast < uint8_t > ( state [ i ] >> ( k * 8 ) );

		}

		base::copy ( out, full, size );

	}

	void blake2b_once ( void* out, uint32_t output_size, const void* data, size_t size ) {

		blake2b hasher ( output_size );

		hasher.add ( data, size );
		hasher.end ( out );

	}

	void blake2b_long ( void* out, uint32_t output_size, const void* data, size_t size ) {

		uint8_t length [ 4 ];
		uint8_t* target = static_cast < uint8_t* > ( out );

		base::store_little ( length, output_size );

		if ( output_size <= 64 ) {

			blake2b hasher ( output_size );

			hasher.add ( length, 4 );
			hasher.add ( data, size );
			hasher.end ( out );

			return;

		}

		uint8_t block [ 64 ];
		blake2b first ( 64 );
		uint32_t left = output_size - 32;

		first.add ( length, 4 );
		first.add ( data, size );
		first.end ( block );
		base::copy ( target, block, 32 );
		target += 32;

		while ( left > 64 ) {

			blake2b_once ( block, 64, block, 64 );
			base::copy ( target, block, 32 );
			target += 32;
			left -= 32;

		}

		blake2b_once ( block, left, block, 64 );
		base::copy ( target, block, left );

	}

}
