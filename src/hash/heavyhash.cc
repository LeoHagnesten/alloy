#include "hash/heavyhash.hh"
#include "hash/keccak.hh"
#include "base/bits.hh"
#include "base/math.hh"
#include "base/memory.hh"
#include "base/strings.hh"

namespace hash {

	static inline uint64_t turn ( uint64_t value, uint32_t count ) {

		return value << count | value >> ( 64 - count );

	}

	uint64_t xoshiro256::next ( ) {

		uint64_t result = turn ( state [ 0 ] + state [ 3 ], 23 ) + state [ 0 ];
		uint64_t shifted = state [ 1 ] << 17;

		state [ 2 ] ^= state [ 0 ];
		state [ 3 ] ^= state [ 1 ];
		state [ 1 ] ^= state [ 2 ];
		state [ 0 ] ^= state [ 3 ];
		state [ 2 ] ^= shifted;
		state [ 3 ] = turn ( state [ 3 ], 45 );

		return result;

	}

	struct sponge {

		uint64_t state [ 25 ] = { };
		uint8_t block [ 168 ] = { };
		uint32_t rate = 136;
		uint32_t filled = 0;

		void absorb ( const uint8_t* data, size_t size ) {

			for ( size_t i = 0; i < size; i++ ) {

				block [ filled++ ] = data [ i ];

				if ( filled == rate )
					flush ( );

			}

		}

		void flush ( ) {

			for ( uint32_t i = 0; i < rate / 8; i++ )
				state [ i ] ^= base::load_little64 ( block + i * 8 );

			keccak_permute ( state );
			base::zero ( block, sizeof ( block ) );
			filled = 0;

		}

		void encode_left ( uint64_t value ) {

			uint8_t bytes [ 9 ];
			int32_t count = 0;

			for ( uint64_t rest = value; rest || !count; rest >>= 8 )
				bytes [ 8 - count++ ] = static_cast < uint8_t > ( rest );

			bytes [ 8 - count ] = static_cast < uint8_t > ( count );
			absorb ( bytes + 8 - count, static_cast < size_t > ( count + 1 ) );

		}

		void encode_string ( const char* text ) {

			size_t size = static_cast < size_t > ( base::length ( text ) );

			encode_left ( size * 8 );
			absorb ( reinterpret_cast < const uint8_t* > ( text ), size );

		}

		void begin ( uint32_t width, const char* name, const char* customization ) {

			rate = width;
			encode_left ( rate );
			encode_string ( name );
			encode_string ( customization );

			if ( filled )
				flush ( );

		}

	};

	void cshake_start ( uint32_t rate, const char* name, const char* customization, uint64_t* state ) {

		sponge* work = new sponge;

		work->begin ( rate, name, customization );
		base::copy ( state, work->state, sizeof ( work->state ) );
		delete work;

	}

	void cshake ( uint32_t rate, const char* name, const char* customization, const void* data, size_t size, uint8_t* out, size_t out_size ) {

		sponge* work = new sponge;
		bool plain = !name [ 0 ] && !customization [ 0 ];

		if ( !plain )
			work->begin ( rate, name, customization );
		else
			work->rate = rate;

		work->absorb ( static_cast < const uint8_t* > ( data ), size );
		work->block [ work->filled ] ^= plain ? 0x1F : 0x04;
		work->block [ work->rate - 1 ] ^= 0x80;
		work->filled = work->rate - 1;
		work->flush ( );

		for ( size_t given = 0; given < out_size; ) {

			uint8_t lanes [ 168 ];

			for ( uint32_t i = 0; i < work->rate / 8; i++ )
				base::store_little64 ( lanes + i * 8, work->state [ i ] );

			size_t take = base::min ( out_size - given, static_cast < size_t > ( work->rate ) );

			base::copy ( out + given, lanes, take );
			given += take;

			if ( given < out_size )
				keccak_permute ( work->state );

		}

		delete work;

	}

	int32_t heavy_rank ( const heavy_matrix& value ) {

		static constexpr double tiny = 1e-9;
		double* grid = new double [ 64 * 64 ];
		bool taken [ 64 ] = { };
		int32_t rank = 0;

		for ( int32_t i = 0; i < 64 * 64; i++ )
			grid [ i ] = static_cast < double > ( value.cells [ i / 64 ] [ i % 64 ] );

		for ( int32_t column = 0; column < 64; column++ ) {

			int32_t pivot = 0;

			while ( pivot < 64 && ( taken [ pivot ] || base::abs ( grid [ pivot * 64 + column ] ) <= tiny ) )
				pivot++;

			if ( pivot == 64 )
				continue;

			rank++;
			taken [ pivot ] = true;

			for ( int32_t p = column + 1; p < 64; p++ )
				grid [ pivot * 64 + p ] /= grid [ pivot * 64 + column ];

			for ( int32_t row = 0; row < 64; row++ ) {

				if ( row == pivot || base::abs ( grid [ row * 64 + column ] ) <= tiny )
					continue;

				for ( int32_t p = column + 1; p < 64; p++ )
					grid [ row * 64 + p ] -= grid [ pivot * 64 + p ] * grid [ row * 64 + column ];

			}

		}

		delete [ ] grid;

		return rank;

	}

	void heavy_matrix_from ( const uint8_t* seed, heavy_matrix& out ) {

		xoshiro256 random;

		for ( int32_t i = 0; i < 4; i++ )
			random.state [ i ] = base::load_little64 ( seed + i * 8 );

		do {

			for ( int32_t row = 0; row < 64; row++ ) {

				uint64_t value = 0;

				for ( int32_t column = 0; column < 64; column++ ) {

					if ( column % 16 == 0 )
						value = random.next ( );

					out.cells [ row ] [ column ] = static_cast < uint8_t > ( value >> ( 4 * ( column % 16 ) ) & 0x0F );

				}

			}

		} while ( heavy_rank ( out ) != 64 );

	}

	void heavy_pack ( const heavy_matrix& value, uint32_t* out ) {

		for ( int32_t row = 0; row < 64; row++ ) {

			for ( int32_t word = 0; word < 8; word++ ) {

				uint32_t packed = 0;

				for ( int32_t t = 0; t < 8; t++ )
					packed |= static_cast < uint32_t > ( value.cells [ row ] [ word * 8 + t ] ) << ( 4 * t );

				out [ row * 8 + word ] = packed;

			}

		}

	}

	void heavy_start_states ( uint64_t* pow, uint64_t* finish ) {

		cshake_start ( 136, "", "ProofOfWorkHash", pow );
		cshake_start ( 136, "", "HeavyHash", finish );
		pow [ 10 ] ^= 0x04;
		pow [ 16 ] ^= 0x8000000000000000ull;
		finish [ 4 ] ^= 0x04;
		finish [ 16 ] ^= 0x8000000000000000ull;

	}

	void heavy_hasher::prepare ( const uint8_t* pre_pow, uint64_t timestamp ) {

		if ( !started ) {

			heavy_start_states ( pow_start, finish );
			started = true;

		}

		if ( !seeded || !base::same ( seed, pre_pow, 32 ) ) {

			heavy_matrix_from ( pre_pow, grid );
			base::copy ( seed, pre_pow, 32 );
			seeded = true;

		}

		base::copy ( pow, pow_start, sizeof ( pow ) );

		for ( int32_t i = 0; i < 4; i++ )
			pow [ i ] ^= base::load_little64 ( pre_pow + i * 8 );

		pow [ 4 ] ^= timestamp;

	}

	void heavy_hasher::hash ( uint64_t nonce, uint8_t* out ) const {

		uint64_t state [ 25 ];
		uint8_t first [ 32 ];
		uint8_t vector [ 64 ];
		uint8_t mixed [ 32 ];

		base::copy ( state, pow, sizeof ( state ) );
		state [ 9 ] ^= nonce;
		keccak_permute ( state );

		for ( int32_t i = 0; i < 4; i++ )
			base::store_little64 ( first + i * 8, state [ i ] );

		for ( int32_t i = 0; i < 32; i++ ) {

			vector [ i * 2 ] = first [ i ] >> 4;
			vector [ i * 2 + 1 ] = first [ i ] & 0x0F;

		}

		for ( int32_t i = 0; i < 32; i++ ) {

			uint32_t high = 0;
			uint32_t low = 0;

			for ( int32_t j = 0; j < 64; j++ ) {

				high += static_cast < uint32_t > ( grid.cells [ i * 2 ] [ j ] ) * vector [ j ];
				low += static_cast < uint32_t > ( grid.cells [ i * 2 + 1 ] [ j ] ) * vector [ j ];

			}

			mixed [ i ] = static_cast < uint8_t > ( ( high >> 10 ) << 4 | low >> 10 ) ^ first [ i ];

		}

		base::copy ( state, finish, sizeof ( state ) );

		for ( int32_t i = 0; i < 4; i++ )
			state [ i ] ^= base::load_little64 ( mixed + i * 8 );

		keccak_permute ( state );

		for ( int32_t i = 0; i < 4; i++ )
			base::store_little64 ( out + i * 8, state [ i ] );

	}

}
