#include "hash/argon2.hh"
#include "hash/blake2b.hh"
#include "base/bits.hh"
#include "base/memory.hh"

namespace hash {

	static constexpr uint32_t slices = 4;

	static inline uint64_t turn ( uint64_t value, uint32_t count ) {

		return value >> count | value << ( 64 - count );

	}

	static inline uint64_t blend ( uint64_t x, uint64_t y ) {

		return x + y + 2 * ( x & 0xFFFFFFFFull ) * ( y & 0xFFFFFFFFull );

	}

	static inline void quarter ( uint64_t& a, uint64_t& b, uint64_t& c, uint64_t& d ) {

		a = blend ( a, b );
		d = turn ( d ^ a, 32 );
		c = blend ( c, d );
		b = turn ( b ^ c, 24 );
		a = blend ( a, b );
		d = turn ( d ^ a, 16 );
		c = blend ( c, d );
		b = turn ( b ^ c, 63 );

	}

	static void round ( uint64_t* v, const int32_t* at ) {

		quarter ( v [ at [ 0 ] ], v [ at [ 4 ] ], v [ at [ 8 ] ], v [ at [ 12 ] ] );
		quarter ( v [ at [ 1 ] ], v [ at [ 5 ] ], v [ at [ 9 ] ], v [ at [ 13 ] ] );
		quarter ( v [ at [ 2 ] ], v [ at [ 6 ] ], v [ at [ 10 ] ], v [ at [ 14 ] ] );
		quarter ( v [ at [ 3 ] ], v [ at [ 7 ] ], v [ at [ 11 ] ], v [ at [ 15 ] ] );
		quarter ( v [ at [ 0 ] ], v [ at [ 5 ] ], v [ at [ 10 ] ], v [ at [ 15 ] ] );
		quarter ( v [ at [ 1 ] ], v [ at [ 6 ] ], v [ at [ 11 ] ], v [ at [ 12 ] ] );
		quarter ( v [ at [ 2 ] ], v [ at [ 7 ] ], v [ at [ 8 ] ], v [ at [ 13 ] ] );
		quarter ( v [ at [ 3 ] ], v [ at [ 4 ] ], v [ at [ 9 ] ], v [ at [ 14 ] ] );

	}

	static void compress ( const argon2_block& previous, const argon2_block& reference, argon2_block& next, bool keep ) {

		argon2_block mixed;
		argon2_block saved;

		for ( int32_t i = 0; i < 128; i++ ) {

			mixed.v [ i ] = reference.v [ i ] ^ previous.v [ i ];
			saved.v [ i ] = keep ? mixed.v [ i ] ^ next.v [ i ] : mixed.v [ i ];

		}

		int32_t at [ 16 ];

		for ( int32_t row = 0; row < 8; row++ ) {

			for ( int32_t k = 0; k < 16; k++ )
				at [ k ] = row * 16 + k;

			round ( mixed.v, at );

		}

		for ( int32_t column = 0; column < 8; column++ ) {

			for ( int32_t k = 0; k < 8; k++ ) {

				at [ k * 2 ] = column * 2 + k * 16;
				at [ k * 2 + 1 ] = column * 2 + k * 16 + 1;

			}

			round ( mixed.v, at );

		}

		for ( int32_t i = 0; i < 128; i++ )
			next.v [ i ] = saved.v [ i ] ^ mixed.v [ i ];

	}

	static void put32 ( blake2b& hasher, uint32_t value ) {

		uint8_t bytes [ 4 ];

		base::store_little ( bytes, value );
		hasher.add ( bytes, 4 );

	}

	void argon2d_fill ( argon2_block* memory, uint32_t blocks, uint32_t passes, const void* password, size_t password_size, const void* salt, size_t salt_size ) {

		uint8_t seed [ 72 ];
		blake2b initial ( 64 );

		put32 ( initial, 1 );
		put32 ( initial, 0 );
		put32 ( initial, blocks );
		put32 ( initial, passes );
		put32 ( initial, 0x13 );
		put32 ( initial, 0 );
		put32 ( initial, static_cast < uint32_t > ( password_size ) );
		initial.add ( password, password_size );
		put32 ( initial, static_cast < uint32_t > ( salt_size ) );
		initial.add ( salt, salt_size );
		put32 ( initial, 0 );
		put32 ( initial, 0 );
		initial.end ( seed );

		for ( uint32_t i = 0; i < 2; i++ ) {

			base::store_little ( seed + 64, i );
			base::store_little ( seed + 68, 0 );
			blake2b_long ( memory [ i ].v, 1024, seed, 72 );

		}

		uint32_t segment = blocks / slices;

		for ( uint32_t pass = 0; pass < passes; pass++ ) {

			for ( uint32_t slice = 0; slice < slices; slice++ ) {

				uint32_t first = pass == 0 && slice == 0 ? 2 : 0;

				for ( uint32_t index = first; index < segment; index++ ) {

					uint32_t current = slice * segment + index;
					uint32_t previous = current == 0 ? blocks - 1 : current - 1;
					uint64_t random = memory [ previous ].v [ 0 ] & 0xFFFFFFFFull;
					uint64_t area = 0;

					if ( pass == 0 )
						area = slice == 0 ? index - 1 : slice * segment + index - 1;
					else
						area = blocks - segment + index - 1;

					uint64_t relative = random * random >> 32;

					relative = area - 1 - ( area * relative >> 32 );

					uint64_t start = pass == 0 || slice == slices - 1 ? 0 : ( slice + 1 ) * static_cast < uint64_t > ( segment );
					uint32_t reference = static_cast < uint32_t > ( ( start + relative ) % blocks );

					compress ( memory [ previous ], memory [ reference ], memory [ current ], pass > 0 );

				}

			}

		}

	}

}
