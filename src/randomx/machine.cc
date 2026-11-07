#include "randomx/machine.hh"
#include "hash/aes.hh"
#include "hash/blake2b.hh"
#include "base/bits.hh"
#include "base/memory.hh"
#include <xmmintrin.h>

namespace randomx {

	static constexpr uint32_t level1 = 16384 - 8;
	static constexpr uint32_t level2 = 262144 - 8;
	static constexpr uint32_t level3 = scratchpad_bytes - 8;
	static constexpr uint32_t level3_line = scratchpad_bytes - 64;
	static constexpr uint64_t line_mask = ( dataset_base - 1 ) & ~63ull;
	static constexpr uint64_t extra_lines = dataset_extra / 64;
	static const uint64_t nothing = 0;

	enum kind : uint8_t {

		KIND_IADD_RS,
		KIND_IADD_M,
		KIND_ISUB_R,
		KIND_ISUB_M,
		KIND_IMUL_R,
		KIND_IMUL_M,
		KIND_IMULH_R,
		KIND_IMULH_M,
		KIND_ISMULH_R,
		KIND_ISMULH_M,
		KIND_INEG_R,
		KIND_IXOR_R,
		KIND_IXOR_M,
		KIND_IROR_R,
		KIND_IROL_R,
		KIND_ISWAP_R,
		KIND_FSWAP_R,
		KIND_FADD_R,
		KIND_FADD_M,
		KIND_FSUB_R,
		KIND_FSUB_M,
		KIND_FSCAL_R,
		KIND_FMUL_R,
		KIND_FDIV_M,
		KIND_FSQRT_R,
		KIND_CBRANCH,
		KIND_CFROUND,
		KIND_ISTORE,
		KIND_NOP,

	};

	static inline void set_rounding ( uint32_t mode ) {

		uint32_t value = 0x9FC0 | ( mode & 3 ) << 13;

		__asm__ volatile ( "ldmxcsr %0" : : "m" ( value ) : "memory" );

	}

	static inline uint64_t high_product ( uint64_t x, uint64_t y ) {

		return static_cast < uint64_t > ( static_cast < unsigned __int128 > ( x ) * y >> 64 );

	}

	static inline uint64_t signed_high_product ( uint64_t x, uint64_t y ) {

		return static_cast < uint64_t > ( static_cast < __int128 > ( static_cast < int64_t > ( x ) ) * static_cast < int64_t > ( y ) >> 64 );

	}

	static inline uint64_t small_float ( uint64_t entropy ) {

		uint64_t exponent = ( ( entropy >> 59 ) + 1023 ) & 0x7FF;

		return exponent << 52 | ( entropy & ( ( 1ull << 52 ) - 1 ) );

	}

	static inline uint64_t float_mask ( uint64_t entropy ) {

		return ( entropy & ( ( 1ull << 22 ) - 1 ) ) | ( 0x300ull | ( entropy >> 60 ) << 4 ) << 52;

	}

	static inline __m128d load_pair ( const uint8_t* at ) {

		return _mm_cvtepi32_pd ( _mm_loadl_epi64 ( reinterpret_cast < const __m128i* > ( at ) ) );

	}

	machine::~machine ( ) {

		base::release_pages ( scratchpad );

	}

	bool machine::open ( const cache* light, const dataset* fast ) {

		light_source = light;
		fast_source = fast;

		if ( !scratchpad )
			scratchpad = static_cast < uint8_t* > ( base::allocate_pages ( scratchpad_bytes ) );

		return scratchpad != nullptr && ( fast || ( light && light->ready ( ) ) );

	}

	void machine::configure ( const uint8_t* entropy ) {

		uint64_t q [ 16 ];

		for ( int32_t i = 0; i < 16; i++ )
			q [ i ] = base::load_little64 ( entropy + i * 8 );

		for ( int32_t i = 0; i < 4; i++ )
			a [ i ] = _mm_castsi128_pd ( _mm_set_epi64x ( static_cast < int64_t > ( small_float ( q [ i * 2 + 1 ] ) ), static_cast < int64_t > ( small_float ( q [ i * 2 ] ) ) ) );

		ma = static_cast < uint32_t > ( q [ 8 ] & line_mask );
		mx = static_cast < uint32_t > ( q [ 10 ] );

		for ( uint32_t i = 0; i < 4; i++ )
			read_registers [ i ] = i * 2 + ( q [ 12 ] >> i & 1 );

		dataset_offset = q [ 13 ] % ( extra_lines + 1 ) * 64;
		exponent_mask = _mm_castsi128_pd ( _mm_set_epi64x ( static_cast < int64_t > ( float_mask ( q [ 15 ] ) ), static_cast < int64_t > ( float_mask ( q [ 14 ] ) ) ) );

	}

	void machine::compile ( const uint8_t* code ) {

		int16_t usage [ 8 ] = { -1, -1, -1, -1, -1, -1, -1, -1 };

		for ( int32_t i = 0; i < program_size; i++ ) {

			const uint8_t* bytes = code + i * 8;
			uint32_t opcode = bytes [ 0 ];
			uint32_t d8 = bytes [ 1 ] & 7;
			uint32_t s8 = bytes [ 2 ] & 7;
			uint32_t d4 = bytes [ 1 ] & 3;
			uint32_t s4 = bytes [ 2 ] & 3;
			uint32_t mod = bytes [ 3 ];
			uint32_t imm32 = base::load_little ( bytes + 4 );
			uint64_t wide = static_cast < uint64_t > ( static_cast < int64_t > ( static_cast < int32_t > ( imm32 ) ) );
			uint32_t near = mod & 3 ? level1 : level2;
			step& s = steps [ i ];

			s = step { };
			s.idst = &r [ d8 ];

			auto memory_source = [ & ] ( uint8_t which ) {

				s.kind = which;
				s.imm = wide;

				if ( s8 != d8 ) {

					s.isrc = &r [ s8 ];
					s.mask = near;

				} else {

					s.isrc = &nothing;
					s.mask = level3;

				}

				usage [ d8 ] = static_cast < int16_t > ( i );

			};

			auto register_source = [ & ] ( uint8_t which ) {

				s.kind = which;

				if ( s8 != d8 )
					s.isrc = &r [ s8 ];
				else {

					s.imm = wide;
					s.isrc = &s.imm;

				}

				usage [ d8 ] = static_cast < int16_t > ( i );

			};

			auto float_memory = [ & ] ( uint8_t which, __m128d* target ) {

				s.kind = which;
				s.fdst = target;
				s.isrc = &r [ s8 ];
				s.imm = wide;
				s.mask = near;

			};

			if ( opcode < 16 ) {

				s.kind = KIND_IADD_RS;
				s.isrc = &r [ s8 ];
				s.shift = static_cast < uint8_t > ( mod >> 2 & 3 );
				s.imm = d8 == 5 ? wide : 0;
				usage [ d8 ] = static_cast < int16_t > ( i );

			} else if ( opcode < 23 )
				memory_source ( KIND_IADD_M );
			else if ( opcode < 39 )
				register_source ( KIND_ISUB_R );
			else if ( opcode < 46 )
				memory_source ( KIND_ISUB_M );
			else if ( opcode < 62 )
				register_source ( KIND_IMUL_R );
			else if ( opcode < 66 )
				memory_source ( KIND_IMUL_M );
			else if ( opcode < 70 ) {

				s.kind = KIND_IMULH_R;
				s.isrc = &r [ s8 ];
				usage [ d8 ] = static_cast < int16_t > ( i );

			} else if ( opcode < 71 )
				memory_source ( KIND_IMULH_M );
			else if ( opcode < 75 ) {

				s.kind = KIND_ISMULH_R;
				s.isrc = &r [ s8 ];
				usage [ d8 ] = static_cast < int16_t > ( i );

			} else if ( opcode < 76 )
				memory_source ( KIND_ISMULH_M );
			else if ( opcode < 84 ) {

				if ( imm32 && ( imm32 & ( imm32 - 1 ) ) ) {

					s.kind = KIND_IMUL_R;
					s.imm = reciprocal ( imm32 );
					s.isrc = &s.imm;
					usage [ d8 ] = static_cast < int16_t > ( i );

				} else
					s.kind = KIND_NOP;

			} else if ( opcode < 86 ) {

				s.kind = KIND_INEG_R;
				usage [ d8 ] = static_cast < int16_t > ( i );

			} else if ( opcode < 101 )
				register_source ( KIND_IXOR_R );
			else if ( opcode < 106 )
				memory_source ( KIND_IXOR_M );
			else if ( opcode < 114 )
				register_source ( KIND_IROR_R );
			else if ( opcode < 116 )
				register_source ( KIND_IROL_R );
			else if ( opcode < 120 ) {

				if ( s8 != d8 ) {

					s.kind = KIND_ISWAP_R;
					s.isrc = &r [ s8 ];
					usage [ d8 ] = static_cast < int16_t > ( i );
					usage [ s8 ] = static_cast < int16_t > ( i );

				} else
					s.kind = KIND_NOP;

			} else if ( opcode < 124 ) {

				s.kind = KIND_FSWAP_R;
				s.fdst = d8 < 4 ? &f [ d8 ] : &e [ d8 - 4 ];

			} else if ( opcode < 140 ) {

				s.kind = KIND_FADD_R;
				s.fdst = &f [ d4 ];
				s.fsrc = &a [ s4 ];

			} else if ( opcode < 145 )
				float_memory ( KIND_FADD_M, &f [ d4 ] );
			else if ( opcode < 161 ) {

				s.kind = KIND_FSUB_R;
				s.fdst = &f [ d4 ];
				s.fsrc = &a [ s4 ];

			} else if ( opcode < 166 )
				float_memory ( KIND_FSUB_M, &f [ d4 ] );
			else if ( opcode < 172 ) {

				s.kind = KIND_FSCAL_R;
				s.fdst = &f [ d4 ];

			} else if ( opcode < 204 ) {

				s.kind = KIND_FMUL_R;
				s.fdst = &e [ d4 ];
				s.fsrc = &a [ s4 ];

			} else if ( opcode < 208 )
				float_memory ( KIND_FDIV_M, &e [ d4 ] );
			else if ( opcode < 214 ) {

				s.kind = KIND_FSQRT_R;
				s.fdst = &e [ d4 ];

			} else if ( opcode < 239 ) {

				uint32_t shift = ( mod >> 4 ) + 8;

				s.kind = KIND_CBRANCH;
				s.target = usage [ d8 ];
				s.imm = ( wide | 1ull << shift ) & ~( 1ull << ( shift - 1 ) );
				s.mask = 255u << shift;

				for ( int32_t k = 0; k < 8; k++ )
					usage [ k ] = static_cast < int16_t > ( i );

			} else if ( opcode < 240 ) {

				s.kind = KIND_CFROUND;
				s.isrc = &r [ s8 ];
				s.imm = imm32 & 63;

			} else {

				s.kind = KIND_ISTORE;
				s.isrc = &r [ s8 ];
				s.imm = wide;
				s.mask = ( mod >> 4 ) < 14 ? near : level3;

			}

		}

	}

	void machine::run ( ) {

		const __m128d sign_scale = _mm_castsi128_pd ( _mm_set1_epi64x ( static_cast < int64_t > ( 0x80F0000000000000ull ) ) );
		const __m128d mantissa = _mm_castsi128_pd ( _mm_set1_epi64x ( static_cast < int64_t > ( ( 1ull << 56 ) - 1 ) ) );
		uint8_t* pad = scratchpad;

		for ( int32_t pc = 0; pc < program_size; pc++ ) {

			step& s = steps [ pc ];

			switch ( s.kind ) {

				case KIND_IADD_RS:
					*s.idst += ( *s.isrc << s.shift ) + s.imm;
					break;

				case KIND_IADD_M:
					*s.idst += base::load_little64 ( pad + ( ( *s.isrc + s.imm ) & s.mask ) );
					break;

				case KIND_ISUB_R:
					*s.idst -= *s.isrc;
					break;

				case KIND_ISUB_M:
					*s.idst -= base::load_little64 ( pad + ( ( *s.isrc + s.imm ) & s.mask ) );
					break;

				case KIND_IMUL_R:
					*s.idst *= *s.isrc;
					break;

				case KIND_IMUL_M:
					*s.idst *= base::load_little64 ( pad + ( ( *s.isrc + s.imm ) & s.mask ) );
					break;

				case KIND_IMULH_R:
					*s.idst = high_product ( *s.idst, *s.isrc );
					break;

				case KIND_IMULH_M:
					*s.idst = high_product ( *s.idst, base::load_little64 ( pad + ( ( *s.isrc + s.imm ) & s.mask ) ) );
					break;

				case KIND_ISMULH_R:
					*s.idst = signed_high_product ( *s.idst, *s.isrc );
					break;

				case KIND_ISMULH_M:
					*s.idst = signed_high_product ( *s.idst, base::load_little64 ( pad + ( ( *s.isrc + s.imm ) & s.mask ) ) );
					break;

				case KIND_INEG_R:
					*s.idst = 0 - *s.idst;
					break;

				case KIND_IXOR_R:
					*s.idst ^= *s.isrc;
					break;

				case KIND_IXOR_M:
					*s.idst ^= base::load_little64 ( pad + ( ( *s.isrc + s.imm ) & s.mask ) );
					break;

				case KIND_IROR_R:
					*s.idst = __builtin_rotateright64 ( *s.idst, *s.isrc & 63 );
					break;

				case KIND_IROL_R:
					*s.idst = __builtin_rotateleft64 ( *s.idst, *s.isrc & 63 );
					break;

				case KIND_ISWAP_R: {

					uint64_t kept = *s.idst;

					*s.idst = *const_cast < uint64_t* > ( s.isrc );
					*const_cast < uint64_t* > ( s.isrc ) = kept;
					break;

				}

				case KIND_FSWAP_R:
					*s.fdst = _mm_shuffle_pd ( *s.fdst, *s.fdst, 1 );
					break;

				case KIND_FADD_R:
					*s.fdst = _mm_add_pd ( *s.fdst, *s.fsrc );
					break;

				case KIND_FADD_M:
					*s.fdst = _mm_add_pd ( *s.fdst, load_pair ( pad + ( ( *s.isrc + s.imm ) & s.mask ) ) );
					break;

				case KIND_FSUB_R:
					*s.fdst = _mm_sub_pd ( *s.fdst, *s.fsrc );
					break;

				case KIND_FSUB_M:
					*s.fdst = _mm_sub_pd ( *s.fdst, load_pair ( pad + ( ( *s.isrc + s.imm ) & s.mask ) ) );
					break;

				case KIND_FSCAL_R:
					*s.fdst = _mm_xor_pd ( *s.fdst, sign_scale );
					break;

				case KIND_FMUL_R:
					*s.fdst = _mm_mul_pd ( *s.fdst, *s.fsrc );
					break;

				case KIND_FDIV_M: {

					__m128d divisor = _mm_or_pd ( _mm_and_pd ( load_pair ( pad + ( ( *s.isrc + s.imm ) & s.mask ) ), mantissa ), exponent_mask );

					*s.fdst = _mm_div_pd ( *s.fdst, divisor );
					break;

				}

				case KIND_FSQRT_R:
					*s.fdst = _mm_sqrt_pd ( *s.fdst );
					break;

				case KIND_CBRANCH:
					*s.idst += s.imm;

					if ( ( *s.idst & s.mask ) == 0 )
						pc = s.target;

					break;

				case KIND_CFROUND:
					set_rounding ( static_cast < uint32_t > ( __builtin_rotateright64 ( *s.isrc, s.imm ) & 3 ) );
					break;

				case KIND_ISTORE:
					base::store_little64 ( pad + ( ( *s.idst + s.imm ) & s.mask ), *s.isrc );
					break;

				default:
					break;

			}

		}

	}

	void machine::read_dataset ( uint64_t address ) {

		if ( fast_source ) {

			const uint8_t* item = fast_source->data ( ) + address;

			for ( int32_t i = 0; i < 8; i++ )
				r [ i ] ^= base::load_little64 ( item + i * 8 );

			return;

		}

		uint64_t item [ 8 ];

		light_source->item ( address / 64, item );

		for ( int32_t i = 0; i < 8; i++ )
			r [ i ] ^= item [ i ];

	}

	void machine::execute ( ) {

		const __m128d mantissa = _mm_castsi128_pd ( _mm_set1_epi64x ( static_cast < int64_t > ( ( 1ull << 56 ) - 1 ) ) );
		uint32_t first = mx;
		uint32_t second = ma;

		for ( int32_t i = 0; i < 8; i++ )
			r [ i ] = 0;

		for ( int32_t iteration = 0; iteration < program_iterations; iteration++ ) {

			uint64_t mix = r [ read_registers [ 0 ] ] ^ r [ read_registers [ 1 ] ];

			first = ( first ^ static_cast < uint32_t > ( mix ) ) & level3_line;
			second = ( second ^ static_cast < uint32_t > ( mix >> 32 ) ) & level3_line;

			for ( int32_t i = 0; i < 8; i++ )
				r [ i ] ^= base::load_little64 ( scratchpad + first + i * 8 );

			for ( int32_t i = 0; i < 4; i++ ) {

				f [ i ] = load_pair ( scratchpad + second + i * 8 );
				e [ i ] = _mm_or_pd ( _mm_and_pd ( load_pair ( scratchpad + second + 32 + i * 8 ), mantissa ), exponent_mask );

			}

			run ( );

			mx ^= static_cast < uint32_t > ( r [ read_registers [ 2 ] ] ^ r [ read_registers [ 3 ] ] );
			mx &= static_cast < uint32_t > ( line_mask );

			if ( fast_source )
				_mm_prefetch ( reinterpret_cast < const char* > ( fast_source->data ( ) + dataset_offset + mx ), _MM_HINT_NTA );

			read_dataset ( dataset_offset + ma );

			uint32_t kept = mx;

			mx = ma;
			ma = kept;

			for ( int32_t i = 0; i < 8; i++ )
				base::store_little64 ( scratchpad + second + i * 8, r [ i ] );

			for ( int32_t i = 0; i < 4; i++ ) {

				f [ i ] = _mm_xor_pd ( f [ i ], e [ i ] );
				_mm_storeu_pd ( reinterpret_cast < double* > ( scratchpad + first + i * 16 ), f [ i ] );

			}

			first = 0;
			second = 0;

		}

	}

	void machine::hash ( const void* input, size_t size, uint8_t* out ) {

		alignas ( 16 ) uint8_t seed [ 64 ];
		alignas ( 16 ) uint8_t file [ 256 ];

		auto pack = [ & ] ( ) {

			for ( int32_t i = 0; i < 8; i++ )
				base::store_little64 ( file + i * 8, r [ i ] );

			for ( int32_t i = 0; i < 4; i++ ) {

				_mm_storeu_pd ( reinterpret_cast < double* > ( file + 64 + i * 16 ), f [ i ] );
				_mm_storeu_pd ( reinterpret_cast < double* > ( file + 128 + i * 16 ), e [ i ] );
				_mm_storeu_pd ( reinterpret_cast < double* > ( file + 192 + i * 16 ), a [ i ] );

			}

		};

		hash::blake2b_once ( seed, 64, input, size );
		hash::aes_fill_1r ( seed, scratchpad, scratchpad_bytes );
		set_rounding ( 0 );

		for ( int32_t chain = 0; chain < program_count; chain++ ) {

			hash::aes_fill_4r ( seed, program, sizeof ( program ) );
			configure ( program );
			compile ( program + 128 );
			execute ( );

			if ( chain + 1 < program_count ) {

				pack ( );
				hash::blake2b_once ( seed, 64, file, 256 );

			}

		}

		pack ( );
		hash::aes_hash_1r ( scratchpad, scratchpad_bytes, file + 192 );
		hash::blake2b_once ( out, 32, file, 256 );
		set_rounding ( 0 );

	}

}
