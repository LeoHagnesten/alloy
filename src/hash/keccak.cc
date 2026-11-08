#include "hash/keccak.hh"
#include "base/bits.hh"
#include "base/memory.hh"

namespace hash {

	static const uint64_t round_constants [ 24 ] = {

		0x0000000000000001ull, 0x0000000000008082ull, 0x800000000000808Aull, 0x8000000080008000ull,
		0x000000000000808Bull, 0x0000000080000001ull, 0x8000000080008081ull, 0x8000000000008009ull,
		0x000000000000008Aull, 0x0000000000000088ull, 0x0000000080008009ull, 0x000000008000000Aull,
		0x000000008000808Bull, 0x800000000000008Bull, 0x8000000000008089ull, 0x8000000000008003ull,
		0x8000000000008002ull, 0x8000000000000080ull, 0x000000000000800Aull, 0x800000008000000Aull,
		0x8000000080008081ull, 0x8000000000008080ull, 0x0000000080000001ull, 0x8000000080008008ull,

	};

	static const uint32_t rotations [ 24 ] = { 1, 3, 6, 10, 15, 21, 28, 36, 45, 55, 2, 14, 27, 41, 56, 8, 25, 43, 62, 18, 39, 61, 20, 44 };
	static const uint32_t lanes [ 24 ] = { 10, 7, 11, 17, 18, 3, 5, 16, 8, 21, 24, 4, 15, 23, 19, 13, 12, 2, 20, 14, 22, 9, 6, 1 };

	static inline uint64_t turn ( uint64_t value, uint32_t count ) {

		return value << count | value >> ( 64 - count );

	}

	void keccak_permute ( uint64_t* state ) {

		uint64_t columns [ 5 ];

		for ( int32_t round = 0; round < 24; round++ ) {

			for ( int32_t x = 0; x < 5; x++ )
				columns [ x ] = state [ x ] ^ state [ x + 5 ] ^ state [ x + 10 ] ^ state [ x + 15 ] ^ state [ x + 20 ];

			for ( int32_t x = 0; x < 5; x++ ) {

				uint64_t mixed = columns [ ( x + 4 ) % 5 ] ^ turn ( columns [ ( x + 1 ) % 5 ], 1 );

				for ( int32_t y = 0; y < 25; y += 5 )
					state [ y + x ] ^= mixed;

			}

			uint64_t carried = state [ 1 ];

			for ( int32_t i = 0; i < 24; i++ ) {

				uint32_t place = lanes [ i ];
				uint64_t kept = state [ place ];

				state [ place ] = turn ( carried, rotations [ i ] );
				carried = kept;

			}

			for ( int32_t y = 0; y < 25; y += 5 ) {

				for ( int32_t x = 0; x < 5; x++ )
					columns [ x ] = state [ y + x ];

				for ( int32_t x = 0; x < 5; x++ )
					state [ y + x ] = columns [ x ] ^ ( ~columns [ ( x + 1 ) % 5 ] & columns [ ( x + 2 ) % 5 ] );

			}

			state [ 0 ] ^= round_constants [ round ];

		}

	}

	void keccak_256 ( const void* data, size_t size, uint8_t* out ) {

		static constexpr size_t rate = 136;
		const uint8_t* bytes = static_cast < const uint8_t* > ( data );
		uint64_t state [ 25 ] = { };
		uint8_t last [ rate ] = { };

		for ( ; size >= rate; size -= rate, bytes += rate ) {

			for ( size_t i = 0; i < rate / 8; i++ )
				state [ i ] ^= base::load_little64 ( bytes + i * 8 );

			keccak_permute ( state );

		}

		base::copy ( last, bytes, size );
		last [ size ] ^= 0x01;
		last [ rate - 1 ] ^= 0x80;

		for ( size_t i = 0; i < rate / 8; i++ )
			state [ i ] ^= base::load_little64 ( last + i * 8 );

		keccak_permute ( state );

		for ( int32_t i = 0; i < 4; i++ )
			base::store_little64 ( out + i * 8, state [ i ] );

	}

}
