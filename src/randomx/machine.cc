#include "randomx/machine.hh"
#include "hash/aes.hh"
#include "hash/blake2b.hh"
#include "base/bits.hh"
#include "base/memory.hh"
#include "base/system/hardware.hh"
#include <xmmintrin.h>

namespace randomx {

	static constexpr uint64_t line_mask = ( dataset_base - 1 ) & ~63ull;
	static constexpr uint64_t extra_lines = dataset_extra / 64;
	static const uint64_t nothing = 0;

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
		base::close_code_view ( code );

	}

	bool machine::open ( const cache* light, const dataset* fast, uint8_t kind ) {

		light_source = light;
		fast_source = fast;
		rules = &variant_of ( kind );

		if ( !hash::aes_ready ( ) ) {

			base::processor cpu;

			base::detect ( cpu );
			hash::aes_prepare ( cpu.aes );

		}

		if ( !scratchpad ) {

			bool large = false;

			scratchpad = static_cast < uint8_t* > ( base::allocate_large ( scratchpad_room, large ) );

		}

		if ( !code.run )
			base::open_code_view ( code, native_room );

		state.scratchpad = scratchpad;
		state.mantissa = _mm_castsi128_pd ( _mm_set1_epi64x ( static_cast < int64_t > ( ( 1ull << 56 ) - 1 ) ) );
		state.sign = _mm_castsi128_pd ( _mm_set1_epi64x ( static_cast < int64_t > ( 0x80F0000000000000ull ) ) );

		return scratchpad != nullptr && ( fast || ( light && light->ready ( ) ) );

	}

	void machine::configure ( const uint8_t* entropy ) {

		uint64_t q [ 16 ];

		for ( int32_t i = 0; i < 16; i++ )
			q [ i ] = base::load_little64 ( entropy + i * 8 );

		for ( int32_t i = 0; i < 4; i++ )
			state.a [ i ] = _mm_castsi128_pd ( _mm_set_epi64x ( static_cast < int64_t > ( small_float ( q [ i * 2 + 1 ] ) ), static_cast < int64_t > ( small_float ( q [ i * 2 ] ) ) ) );

		ma = static_cast < uint32_t > ( q [ 8 ] & line_mask );
		mx = static_cast < uint32_t > ( q [ 10 ] );

		for ( uint32_t i = 0; i < 4; i++ )
			read_registers [ i ] = i * 2 + ( q [ 12 ] >> i & 1 );

		dataset_offset = q [ 13 ] % ( extra_lines + 1 ) * 64;
		state.exponent = _mm_castsi128_pd ( _mm_set_epi64x ( static_cast < int64_t > ( float_mask ( q [ 15 ] ) ), static_cast < int64_t > ( float_mask ( q [ 14 ] ) ) ) );

	}

	bool machine::build ( const uint8_t* bytes ) {

		if ( interpret || !code.write )
			return false;

		program_shape shape;

		for ( int32_t i = 0; i < 4; i++ )
			shape.read_registers [ i ] = read_registers [ i ];

		shape.whole_loop = fast_source != nullptr && ( !rules->v2 || hash::aes_hardware ( ) );
		shape.rules = rules;

		return emit_program ( bytes, shape, code.write, native_room );

	}

	void machine::compile ( const uint8_t* code ) {

		uint64_t* r = state.r;
		__m128d* f = state.f;
		__m128d* e = state.e;
		__m128d* a = state.a;
		int16_t usage [ 8 ] = { -1, -1, -1, -1, -1, -1, -1, -1 };
		uint32_t level1 = level1_bytes - 8;
		uint32_t level2 = rules->level2 - 8;
		uint32_t level3 = rules->level3 - 8;

		for ( int32_t i = 0; i < rules->program_size; i++ ) {

			const uint8_t* bytes = code + i * 8;
			uint8_t operation = rules->decode [ bytes [ 0 ] ];
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
			s.kind = operation;
			s.idst = &r [ d8 ];

			auto memory_source = [ & ] ( ) {

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

			auto register_source = [ & ] ( ) {

				if ( s8 != d8 )
					s.isrc = &r [ s8 ];
				else {

					s.imm = wide;
					s.isrc = &s.imm;

				}

				usage [ d8 ] = static_cast < int16_t > ( i );

			};

			auto float_memory = [ & ] ( __m128d* target ) {

				s.fdst = target;
				s.isrc = &r [ s8 ];
				s.imm = wide;
				s.mask = near;

			};

			switch ( operation ) {

				case OP_IADD_RS:
					s.isrc = &r [ s8 ];
					s.shift = static_cast < uint8_t > ( mod >> 2 & 3 );
					s.imm = d8 == 5 ? wide : 0;
					usage [ d8 ] = static_cast < int16_t > ( i );
					break;

				case OP_IADD_M:
				case OP_ISUB_M:
				case OP_IMUL_M:
				case OP_IMULH_M:
				case OP_ISMULH_M:
				case OP_IXOR_M:
					memory_source ( );
					break;

				case OP_ISUB_R:
				case OP_IMUL_R:
				case OP_IXOR_R:
				case OP_IROR_R:
				case OP_IROL_R:
					register_source ( );
					break;

				case OP_IMULH_R:
				case OP_ISMULH_R:
					s.isrc = &r [ s8 ];
					usage [ d8 ] = static_cast < int16_t > ( i );
					break;

				case OP_IMUL_RCP:
					if ( imm32 && ( imm32 & ( imm32 - 1 ) ) ) {

						s.kind = OP_IMUL_R;
						s.imm = reciprocal ( imm32 );
						s.isrc = &s.imm;
						usage [ d8 ] = static_cast < int16_t > ( i );

					} else
						s.kind = OP_NOP;

					break;

				case OP_INEG_R:
					usage [ d8 ] = static_cast < int16_t > ( i );
					break;

				case OP_ISWAP_R:
					if ( s8 != d8 ) {

						s.isrc = &r [ s8 ];
						usage [ d8 ] = static_cast < int16_t > ( i );
						usage [ s8 ] = static_cast < int16_t > ( i );

					} else
						s.kind = OP_NOP;

					break;

				case OP_FSWAP_R:
					s.fdst = d8 < 4 ? &f [ d8 ] : &e [ d8 - 4 ];
					break;

				case OP_FADD_R:
				case OP_FSUB_R:
					s.fdst = &f [ d4 ];
					s.fsrc = &a [ s4 ];
					break;

				case OP_FADD_M:
				case OP_FSUB_M:
					float_memory ( &f [ d4 ] );
					break;

				case OP_FSCAL_R:
					s.fdst = &f [ d4 ];
					break;

				case OP_FMUL_R:
					s.fdst = &e [ d4 ];
					s.fsrc = &a [ s4 ];
					break;

				case OP_FDIV_M:
					float_memory ( &e [ d4 ] );
					break;

				case OP_FSQRT_R:
					s.fdst = &e [ d4 ];
					break;

				case OP_CBRANCH: {

					uint32_t shift = ( mod >> 4 ) + 8;

					s.target = usage [ d8 ];
					s.imm = ( wide | 1ull << shift ) & ~( 1ull << ( shift - 1 ) );
					s.mask = 255u << shift;

					for ( int32_t k = 0; k < 8; k++ )
						usage [ k ] = static_cast < int16_t > ( i );

					break;

				}

				case OP_CFROUND:
					s.isrc = &r [ s8 ];
					s.imm = imm32 & 63;
					break;

				case OP_ISTORE:
					s.isrc = &r [ s8 ];
					s.imm = wide;
					s.mask = ( mod >> 4 ) < 14 ? near : level3;
					break;

				default:
					s.kind = OP_NOP;
					break;

			}

		}

	}

	void machine::run ( ) {

		uint8_t* pad = scratchpad;
		bool v2 = rules->v2;
		int32_t size = rules->program_size;

		for ( int32_t pc = 0; pc < size; pc++ ) {

			step& s = steps [ pc ];

			switch ( s.kind ) {

				case OP_IADD_RS:
					*s.idst += ( *s.isrc << s.shift ) + s.imm;
					break;

				case OP_IADD_M:
					*s.idst += base::load_little64 ( pad + ( ( *s.isrc + s.imm ) & s.mask ) );
					break;

				case OP_ISUB_R:
					*s.idst -= *s.isrc;
					break;

				case OP_ISUB_M:
					*s.idst -= base::load_little64 ( pad + ( ( *s.isrc + s.imm ) & s.mask ) );
					break;

				case OP_IMUL_R:
					*s.idst *= *s.isrc;
					break;

				case OP_IMUL_M:
					*s.idst *= base::load_little64 ( pad + ( ( *s.isrc + s.imm ) & s.mask ) );
					break;

				case OP_IMULH_R:
					*s.idst = high_product ( *s.idst, *s.isrc );
					break;

				case OP_IMULH_M:
					*s.idst = high_product ( *s.idst, base::load_little64 ( pad + ( ( *s.isrc + s.imm ) & s.mask ) ) );
					break;

				case OP_ISMULH_R:
					*s.idst = signed_high_product ( *s.idst, *s.isrc );
					break;

				case OP_ISMULH_M:
					*s.idst = signed_high_product ( *s.idst, base::load_little64 ( pad + ( ( *s.isrc + s.imm ) & s.mask ) ) );
					break;

				case OP_INEG_R:
					*s.idst = 0 - *s.idst;
					break;

				case OP_IXOR_R:
					*s.idst ^= *s.isrc;
					break;

				case OP_IXOR_M:
					*s.idst ^= base::load_little64 ( pad + ( ( *s.isrc + s.imm ) & s.mask ) );
					break;

				case OP_IROR_R:
					*s.idst = __builtin_rotateright64 ( *s.idst, *s.isrc & 63 );
					break;

				case OP_IROL_R:
					*s.idst = __builtin_rotateleft64 ( *s.idst, *s.isrc & 63 );
					break;

				case OP_ISWAP_R: {

					uint64_t held = *s.idst;

					*s.idst = *const_cast < uint64_t* > ( s.isrc );
					*const_cast < uint64_t* > ( s.isrc ) = held;
					break;

				}

				case OP_FSWAP_R:
					*s.fdst = _mm_shuffle_pd ( *s.fdst, *s.fdst, 1 );
					break;

				case OP_FADD_R:
					*s.fdst = _mm_add_pd ( *s.fdst, *s.fsrc );
					break;

				case OP_FADD_M:
					*s.fdst = _mm_add_pd ( *s.fdst, load_pair ( pad + ( ( *s.isrc + s.imm ) & s.mask ) ) );
					break;

				case OP_FSUB_R:
					*s.fdst = _mm_sub_pd ( *s.fdst, *s.fsrc );
					break;

				case OP_FSUB_M:
					*s.fdst = _mm_sub_pd ( *s.fdst, load_pair ( pad + ( ( *s.isrc + s.imm ) & s.mask ) ) );
					break;

				case OP_FSCAL_R:
					*s.fdst = _mm_xor_pd ( *s.fdst, state.sign );
					break;

				case OP_FMUL_R:
					*s.fdst = _mm_mul_pd ( *s.fdst, *s.fsrc );
					break;

				case OP_FDIV_M: {

					__m128d divisor = _mm_or_pd ( _mm_and_pd ( load_pair ( pad + ( ( *s.isrc + s.imm ) & s.mask ) ), state.mantissa ), state.exponent );

					*s.fdst = _mm_div_pd ( *s.fdst, divisor );
					break;

				}

				case OP_FSQRT_R:
					*s.fdst = _mm_sqrt_pd ( *s.fdst );
					break;

				case OP_CBRANCH:
					*s.idst += s.imm;

					if ( ( *s.idst & s.mask ) == 0 )
						pc = s.target;

					break;

				case OP_CFROUND: {

					uint64_t value = __builtin_rotateright64 ( *s.isrc, s.imm );

					if ( !v2 || ( value & 0x3C ) == 0 )
						set_rounding ( static_cast < uint32_t > ( value & 3 ) );

					break;

				}

				case OP_ISTORE:
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
				state.r [ i ] ^= base::load_little64 ( item + i * 8 );

			return;

		}

		uint64_t item [ 8 ];

		light_source->item ( address / 64, item );

		for ( int32_t i = 0; i < 8; i++ )
			state.r [ i ] ^= item [ i ];

	}

	void machine::mix_registers ( ) {

		if ( !rules->v2 ) {

			for ( int32_t i = 0; i < 4; i++ )
				state.f [ i ] = _mm_xor_pd ( state.f [ i ], state.e [ i ] );

			return;

		}

		alignas ( 16 ) uint8_t blocks [ 4 ] [ 16 ];
		alignas ( 16 ) uint8_t keys [ 4 ] [ 16 ];

		for ( int32_t i = 0; i < 4; i++ ) {

			_mm_store_pd ( reinterpret_cast < double* > ( blocks [ i ] ), state.f [ i ] );
			_mm_store_pd ( reinterpret_cast < double* > ( keys [ i ] ), state.e [ i ] );

		}

		for ( int32_t k = 0; k < 4; k++ ) {

			for ( int32_t i = 0; i < 4; i++ ) {

				if ( i & 1 )
					hash::aes_decrypt_round ( blocks [ i ], keys [ k ] );
				else
					hash::aes_encrypt_round ( blocks [ i ], keys [ k ] );

			}

		}

		for ( int32_t i = 0; i < 4; i++ )
			state.f [ i ] = _mm_load_pd ( reinterpret_cast < const double* > ( blocks [ i ] ) );

	}

	void machine::execute ( bool native ) {

		uint64_t* r = state.r;
		uint32_t first = mx;
		uint32_t second = ma;
		uint32_t line3 = rules->level3 - 64;
		native_body body = reinterpret_cast < native_body > ( code.run );

		for ( int32_t i = 0; i < 8; i++ )
			r [ i ] = 0;

		if ( native && fast_source && ( !rules->v2 || hash::aes_hardware ( ) ) ) {

			state.mx = mx;
			state.ma = ma;
			state.dataset = fast_source->data ( ) + dataset_offset;
			body ( &state );

			return;

		}

		for ( int32_t iteration = 0; iteration < rules->iterations; iteration++ ) {

			uint64_t mix = r [ read_registers [ 0 ] ] ^ r [ read_registers [ 1 ] ];

			first = ( first ^ static_cast < uint32_t > ( mix ) ) & line3;
			second = ( second ^ static_cast < uint32_t > ( mix >> 32 ) ) & line3;

			for ( int32_t i = 0; i < 8; i++ )
				r [ i ] ^= base::load_little64 ( scratchpad + first + i * 8 );

			for ( int32_t i = 0; i < 4; i++ ) {

				state.f [ i ] = load_pair ( scratchpad + second + i * 8 );
				state.e [ i ] = _mm_or_pd ( _mm_and_pd ( load_pair ( scratchpad + second + 32 + i * 8 ), state.mantissa ), state.exponent );

			}

			if ( native )
				body ( &state );
			else
				run ( );

			uint32_t read_at = static_cast < uint32_t > ( ma & line_mask );
			uint32_t& moving = rules->v2 ? ma : mx;

			moving ^= static_cast < uint32_t > ( r [ read_registers [ 2 ] ] ^ r [ read_registers [ 3 ] ] );

			if ( fast_source )
				_mm_prefetch ( reinterpret_cast < const char* > ( fast_source->data ( ) + dataset_offset + ( moving & line_mask ) ), _MM_HINT_NTA );

			read_dataset ( dataset_offset + read_at );

			uint32_t held = mx;

			mx = ma;
			ma = held;

			for ( int32_t i = 0; i < 8; i++ )
				base::store_little64 ( scratchpad + second + i * 8, r [ i ] );

			mix_registers ( );

			for ( int32_t i = 0; i < 4; i++ )
				_mm_storeu_pd ( reinterpret_cast < double* > ( scratchpad + first + i * 16 ), state.f [ i ] );

			first = 0;
			second = 0;

		}

	}

	void machine::hash_core ( const void* input, size_t size, uint8_t* out ) {

		alignas ( 16 ) uint8_t seed [ 64 ];
		alignas ( 16 ) uint8_t file [ 256 ];
		uint32_t pad = rules->level3;
		size_t program_bytes = 128 + 8 * static_cast < size_t > ( rules->program_size );

		auto pack = [ & ] ( ) {

			for ( int32_t i = 0; i < 8; i++ )
				base::store_little64 ( file + i * 8, state.r [ i ] );

			for ( int32_t i = 0; i < 4; i++ ) {

				_mm_storeu_pd ( reinterpret_cast < double* > ( file + 64 + i * 16 ), state.f [ i ] );
				_mm_storeu_pd ( reinterpret_cast < double* > ( file + 128 + i * 16 ), state.e [ i ] );
				_mm_storeu_pd ( reinterpret_cast < double* > ( file + 192 + i * 16 ), state.a [ i ] );

			}

		};

		hash::blake2b_once ( seed, 64, input, size );
		hash::aes_fill_1r ( seed, scratchpad, pad );
		set_rounding ( 0 );

		for ( int32_t chain = 0; chain < rules->count; chain++ ) {

			hash::aes_fill_4r ( seed, program, program_bytes, rules->keys );
			configure ( program );

			bool native = build ( program + 128 );

			if ( !native )
				compile ( program + 128 );

			execute ( native );

			if ( chain + 1 < rules->count ) {

				pack ( );
				hash::blake2b_once ( seed, 64, file, 256 );

			}

		}

		pack ( );
		hash::aes_hash_1r ( scratchpad, pad, file + 192 );
		hash::blake2b_once ( out, 32, file, 256 );
		set_rounding ( 0 );

	}

	void machine::hash ( const void* input, size_t size, uint8_t* out ) {

		hash_core ( input, size, out );

		if ( !rules->v2 )
			return;

		uint8_t result [ 32 ];
		hash::blake2b commitment ( 32 );

		base::copy ( result, out, 32 );
		commitment.add ( input, size );
		commitment.add ( result, 32 );
		commitment.end ( out );

	}

}
