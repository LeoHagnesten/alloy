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

	void argon2d_fill ( argon2_block* memory, uint32_t blocks, uint32_t passes, uint32_t lanes, const argon2_input& input ) {

		uint8_t seed [ 72 ];
		blake2b initial ( 64 );

		lanes = lanes ? lanes : 1;
		put32 ( initial, lanes );
		put32 ( initial, input.tag_size );
		put32 ( initial, blocks );
		put32 ( initial, passes );
		put32 ( initial, 0x13 );
		put32 ( initial, 0 );
		put32 ( initial, static_cast < uint32_t > ( input.password_size ) );
		initial.add ( input.password, input.password_size );
		put32 ( initial, static_cast < uint32_t > ( input.salt_size ) );
		initial.add ( input.salt, input.salt_size );
		put32 ( initial, static_cast < uint32_t > ( input.secret_size ) );
		initial.add ( input.secret, input.secret_size );
		put32 ( initial, static_cast < uint32_t > ( input.data_size ) );
		initial.add ( input.data, input.data_size );
		initial.end ( seed );

		uint32_t used = blocks / ( slices * lanes ) * slices * lanes;
		uint32_t width = used / lanes;
		uint32_t segment = width / slices;

		for ( uint32_t lane = 0; lane < lanes; lane++ ) {

			for ( uint32_t i = 0; i < 2; i++ ) {

				base::store_little ( seed + 64, i );
				base::store_little ( seed + 68, lane );
				blake2b_long ( memory [ lane * width + i ].v, 1024, seed, 72 );

			}

		}

		for ( uint32_t pass = 0; pass < passes; pass++ ) {

			for ( uint32_t slice = 0; slice < slices; slice++ ) {

				for ( uint32_t lane = 0; lane < lanes; lane++ ) {

					uint32_t first = pass == 0 && slice == 0 ? 2 : 0;
					uint32_t row = lane * width;

					for ( uint32_t index = first; index < segment; index++ ) {

						uint32_t column = slice * segment + index;
						uint32_t current = row + column;
						uint32_t previous = column == 0 ? row + width - 1 : current - 1;
						uint64_t word = memory [ previous ].v [ 0 ];
						uint64_t random = word & 0xFFFFFFFFull;
						uint32_t target = pass == 0 && slice == 0 ? lane : static_cast < uint32_t > ( ( word >> 32 ) % lanes );
						bool same = target == lane;
						uint64_t area = 0;

						if ( pass == 0 ) {

							if ( slice == 0 )
								area = index - 1;
							else if ( same )
								area = slice * segment + index - 1;
							else
								area = slice * segment - ( index == 0 ? 1 : 0 );

						} else if ( same )
							area = width - segment + index - 1;
						else
							area = width - segment - ( index == 0 ? 1 : 0 );

						uint64_t relative = random * random >> 32;

						relative = area - 1 - ( area * relative >> 32 );

						uint64_t start = pass == 0 || slice == slices - 1 ? 0 : ( slice + 1 ) * static_cast < uint64_t > ( segment );
						uint32_t reference = target * width + static_cast < uint32_t > ( ( start + relative ) % width );

						compress ( memory [ previous ], memory [ reference ], memory [ current ], pass > 0 );

					}

				}

			}

		}

	}

	void argon2d_fill ( argon2_block* memory, uint32_t blocks, uint32_t passes, const void* password, size_t password_size, const void* salt, size_t salt_size ) {

		argon2_input input;

		input.password = password;
		input.password_size = password_size;
		input.salt = salt;
		input.salt_size = salt_size;
		argon2d_fill ( memory, blocks, passes, 1, input );

	}

	void argon2d_tag ( const argon2_block* memory, uint32_t blocks, uint32_t lanes, uint32_t tag_size, uint8_t* out ) {

		lanes = lanes ? lanes : 1;

		uint32_t width = blocks / ( slices * lanes ) * slices;
		argon2_block last = memory [ width - 1 ];
		uint8_t bytes [ 1024 ];

		for ( uint32_t lane = 1; lane < lanes; lane++ ) {

			for ( int32_t i = 0; i < 128; i++ )
				last.v [ i ] ^= memory [ lane * width + width - 1 ].v [ i ];

		}

		for ( int32_t i = 0; i < 128; i++ )
			base::store_little64 ( bytes + i * 8, last.v [ i ] );

		blake2b_long ( out, tag_size, bytes, sizeof ( bytes ) );

	}

}
