#include "randomx/jit.hh"
#include "randomx/assembler.hh"
#include "randomx/superscalar.hh"
#include "base/bits.hh"

namespace randomx {

	static constexpr int32_t frame = 200;
	static constexpr int32_t slot_control = 160;
	static constexpr int32_t slot_first = 168;
	static constexpr int32_t slot_second = 176;
	static constexpr int32_t slot_mx = 184;
	static constexpr int32_t slot_ma = 192;
	static constexpr int32_t offset_scratchpad = 304;
	static constexpr int32_t offset_dataset = 312;
	static constexpr int32_t offset_mx = 320;
	static constexpr int32_t offset_ma = 324;
	static constexpr int32_t temp = 12;
	static constexpr int32_t mantissa_mask = 13;
	static constexpr int32_t exponent_mask = 14;
	static constexpr int32_t sign_mask = 15;

	static const int32_t kept [ ] = { RBX, RSI, RDI, RBP, R12, R13, R14, R15 };

	struct layout {

		uint32_t level1 = 0;
		uint32_t level2 = 0;
		uint32_t level3 = 0;
		uint32_t line3 = 0;
		int32_t iterations = 0;
		int32_t size = 0;
		bool v2 = false;
		const uint8_t* decode = nullptr;

	};

	static layout layout_of ( const variant& rules ) {

		layout out;

		out.level1 = level1_bytes - 8;
		out.level2 = rules.level2 - 8;
		out.level3 = rules.level3 - 8;
		out.line3 = rules.level3 - 64;
		out.iterations = rules.iterations;
		out.size = base::min ( rules.program_size, program_room );
		out.v2 = rules.v2;
		out.decode = rules.decode;

		return out;

	}

	static operand scratch_at ( assembler& out, int32_t source, uint32_t imm, uint32_t mask, int32_t into ) {

		out.op ( 0, false, 0x8D, 1, into, memory ( R8 + source, static_cast < int32_t > ( imm ) ) );
		out.op ( 0, false, 0x81, 1, 4, direct ( into ), 4, mask );

		return memory ( RSI, 0, into );

	}

	static operand source_at ( assembler& out, const layout& form, uint32_t s8, uint32_t d8, uint32_t imm, uint32_t near, int32_t into ) {

		if ( s8 != d8 )
			return scratch_at ( out, static_cast < int32_t > ( s8 ), imm, near, into );

		return memory ( RSI, static_cast < int32_t > ( imm & form.level3 ) );

	}

	static void prologue ( assembler& out, const program_shape& shape, const layout& form ) {

		for ( int32_t reg : kept )
			out.push ( reg );

		out.op ( 0, true, 0x81, 1, 5, direct ( RSP ), 4, frame );

		for ( int32_t i = 0; i < 10; i++ )
			out.op ( 0xF3, false, 0x0F7F, 2, 6 + i, memory ( RSP, i * 16 ) );

		out.op ( 0, true, 0x8B, 1, RBX, direct ( RCX ) );
		out.op ( 0, true, 0x8B, 1, RSI, memory ( RBX, offset_scratchpad ) );

		for ( int32_t i = 0; i < 8; i++ )
			out.op ( 0, true, 0x8B, 1, R8 + i, memory ( RBX, i * 8 ) );

		for ( int32_t i = 0; i < 12; i++ )
			out.op ( 0x66, false, 0x0F10, 2, i, memory ( RBX, 64 + i * 16 ) );

		for ( int32_t i = 0; i < 3; i++ )
			out.op ( 0x66, false, 0x0F10, 2, mantissa_mask + i, memory ( RBX, 256 + i * 16 ) );

		if ( !shape.whole_loop )
			return;

		out.op ( 0, true, 0x8B, 1, RDI, memory ( RBX, offset_dataset ) );
		out.op ( 0, false, 0xC7, 1, 0, direct ( RBP ), 4, static_cast < uint32_t > ( form.iterations ) );
		out.op ( 0, false, 0x8B, 1, RAX, memory ( RBX, offset_mx ) );
		out.op ( 0, false, 0x89, 1, RAX, memory ( RSP, slot_first ) );
		out.op ( 0, false, 0x89, 1, RAX, memory ( RSP, slot_mx ) );
		out.op ( 0, false, 0x8B, 1, RAX, memory ( RBX, offset_ma ) );
		out.op ( 0, false, 0x89, 1, RAX, memory ( RSP, slot_second ) );
		out.op ( 0, false, 0x89, 1, RAX, memory ( RSP, slot_ma ) );

	}

	static void loop_head ( assembler& out, const program_shape& shape, const layout& form ) {

		out.op ( 0, true, 0x8B, 1, RAX, direct ( R8 + static_cast < int32_t > ( shape.read_registers [ 0 ] ) ) );
		out.op ( 0, true, 0x33, 1, RAX, direct ( R8 + static_cast < int32_t > ( shape.read_registers [ 1 ] ) ) );
		out.op ( 0, true, 0x8B, 1, RDX, direct ( RAX ) );
		out.op ( 0, true, 0xC1, 1, 5, direct ( RDX ), 1, 32 );
		out.op ( 0, false, 0x33, 1, RAX, memory ( RSP, slot_first ) );
		out.op ( 0, false, 0x81, 1, 4, direct ( RAX ), 4, form.line3 );
		out.op ( 0, false, 0x33, 1, RDX, memory ( RSP, slot_second ) );
		out.op ( 0, false, 0x81, 1, 4, direct ( RDX ), 4, form.line3 );
		out.op ( 0, false, 0x89, 1, RAX, memory ( RSP, slot_first ) );
		out.op ( 0, false, 0x89, 1, RDX, memory ( RSP, slot_second ) );

		for ( int32_t i = 0; i < 8; i++ )
			out.op ( 0, true, 0x33, 1, R8 + i, memory ( RSI, i * 8, RAX ) );

		for ( int32_t i = 0; i < 4; i++ )
			out.op ( 0xF3, false, 0x0FE6, 2, i, memory ( RSI, i * 8, RDX ) );

		for ( int32_t i = 0; i < 4; i++ ) {

			out.op ( 0xF3, false, 0x0FE6, 2, 4 + i, memory ( RSI, 32 + i * 8, RDX ) );
			out.op ( 0x66, false, 0x0F54, 2, 4 + i, direct ( mantissa_mask ) );
			out.op ( 0x66, false, 0x0F56, 2, 4 + i, direct ( exponent_mask ) );

		}

	}

	static void loop_tail ( assembler& out, const program_shape& shape, const layout& form, const uint8_t* top ) {

		out.op ( 0, true, 0x8B, 1, RAX, direct ( R8 + static_cast < int32_t > ( shape.read_registers [ 2 ] ) ) );
		out.op ( 0, true, 0x33, 1, RAX, direct ( R8 + static_cast < int32_t > ( shape.read_registers [ 3 ] ) ) );

		if ( form.v2 ) {

			out.op ( 0, false, 0x8B, 1, RCX, memory ( RSP, slot_ma ) );
			out.op ( 0, false, 0x33, 1, RAX, direct ( RCX ) );
			out.op ( 0, false, 0x81, 1, 4, direct ( RCX ), 4, dataset_line );
			out.op ( 0, false, 0x8B, 1, RDX, direct ( RAX ) );
			out.op ( 0, false, 0x81, 1, 4, direct ( RDX ), 4, dataset_line );
			out.op ( 0, false, 0x0F18, 2, 0, memory ( RDI, 0, RDX ) );

			for ( int32_t i = 0; i < 8; i++ )
				out.op ( 0, true, 0x33, 1, R8 + i, memory ( RDI, i * 8, RCX ) );

			out.op ( 0, false, 0x8B, 1, RCX, memory ( RSP, slot_mx ) );
			out.op ( 0, false, 0x89, 1, RCX, memory ( RSP, slot_ma ) );
			out.op ( 0, false, 0x89, 1, RAX, memory ( RSP, slot_mx ) );

		} else {

			out.op ( 0, false, 0x33, 1, RAX, memory ( RSP, slot_mx ) );
			out.op ( 0, false, 0x81, 1, 4, direct ( RAX ), 4, dataset_line );
			out.op ( 0, false, 0x0F18, 2, 0, memory ( RDI, 0, RAX ) );
			out.op ( 0, false, 0x8B, 1, RCX, memory ( RSP, slot_ma ) );

			for ( int32_t i = 0; i < 8; i++ )
				out.op ( 0, true, 0x33, 1, R8 + i, memory ( RDI, i * 8, RCX ) );

			out.op ( 0, false, 0x89, 1, RCX, memory ( RSP, slot_mx ) );
			out.op ( 0, false, 0x89, 1, RAX, memory ( RSP, slot_ma ) );

		}

		out.op ( 0, false, 0x8B, 1, RDX, memory ( RSP, slot_second ) );

		for ( int32_t i = 0; i < 8; i++ )
			out.op ( 0, true, 0x89, 1, R8 + i, memory ( RSI, i * 8, RDX ) );

		out.op ( 0, false, 0x8B, 1, RAX, memory ( RSP, slot_first ) );

		if ( form.v2 ) {

			for ( int32_t k = 0; k < 4; k++ ) {

				for ( int32_t i = 0; i < 4; i++ )
					out.op ( 0x66, false, i & 1 ? 0x0F38DE : 0x0F38DC, 3, i, direct ( 4 + k ) );

			}

		}

		for ( int32_t i = 0; i < 4; i++ ) {

			if ( !form.v2 )
				out.op ( 0x66, false, 0x0F57, 2, i, direct ( 4 + i ) );

			out.op ( 0x66, false, 0x0F11, 2, i, memory ( RSI, i * 16, RAX ) );

		}

		out.op ( 0, false, 0xC7, 1, 0, memory ( RSP, slot_first ), 4, 0 );
		out.op ( 0, false, 0xC7, 1, 0, memory ( RSP, slot_second ), 4, 0 );
		out.op ( 0, true, 0xFF, 1, 1, direct ( RBP ) );
		out.branch ( 0x85, top );

	}

	static void epilogue ( assembler& out ) {

		for ( int32_t i = 0; i < 8; i++ )
			out.op ( 0, true, 0x89, 1, R8 + i, memory ( RBX, i * 8 ) );

		for ( int32_t i = 0; i < 8; i++ )
			out.op ( 0x66, false, 0x0F11, 2, i, memory ( RBX, 64 + i * 16 ) );

		for ( int32_t i = 0; i < 10; i++ )
			out.op ( 0xF3, false, 0x0F6F, 2, 6 + i, memory ( RSP, i * 16 ) );

		out.op ( 0, true, 0x81, 1, 0, direct ( RSP ), 4, frame );

		for ( int32_t i = base::count_of ( kept ) - 1; i >= 0; i-- )
			out.pop ( kept [ i ] );

		out.byte ( 0xC3 );

	}

	bool emit_program ( const uint8_t* code, const program_shape& shape, uint8_t* destination, size_t room ) {

		layout form = layout_of ( shape.rules ? *shape.rules : variant_of ( VARIANT_RX0 ) );
		assembler out ( destination );
		uint8_t* labels [ program_room ];
		int16_t usage [ 8 ] = { -1, -1, -1, -1, -1, -1, -1, -1 };

		prologue ( out, shape, form );

		uint8_t* top = out.here ( );

		if ( shape.whole_loop )
			loop_head ( out, shape, form );

		uint8_t* body = out.here ( );

		for ( int32_t i = 0; i < form.size; i++ ) {

			if ( out.size ( ) + 128 > room )
				return false;

			labels [ i ] = out.here ( );

			const uint8_t* bytes = code + i * 8;
			uint32_t d8 = bytes [ 1 ] & 7;
			uint32_t s8 = bytes [ 2 ] & 7;
			uint32_t d4 = bytes [ 1 ] & 3;
			uint32_t s4 = bytes [ 2 ] & 3;
			uint32_t mod = bytes [ 3 ];
			uint32_t imm = base::load_little ( bytes + 4 );
			uint32_t near = mod & 3 ? form.level1 : form.level2;
			int32_t dst = R8 + static_cast < int32_t > ( d8 );
			int32_t src = R8 + static_cast < int32_t > ( s8 );
			int32_t fd = static_cast < int32_t > ( d4 );
			int32_t fs = static_cast < int32_t > ( s4 );
			int16_t mark = static_cast < int16_t > ( i );

			switch ( form.decode [ bytes [ 0 ] ] ) {

				case OP_IADD_RS:
					out.op ( 0, true, 0x8D, 1, dst, memory ( dst, d8 == 5 ? static_cast < int32_t > ( imm ) : 0, src, mod >> 2 & 3 ) );
					usage [ d8 ] = mark;
					break;

				case OP_IADD_M:
					out.op ( 0, true, 0x03, 1, dst, source_at ( out, form, s8, d8, imm, near, RAX ) );
					usage [ d8 ] = mark;
					break;

				case OP_ISUB_R:
					if ( s8 != d8 )
						out.op ( 0, true, 0x2B, 1, dst, direct ( src ) );
					else
						out.op ( 0, true, 0x81, 1, 5, direct ( dst ), 4, imm );

					usage [ d8 ] = mark;
					break;

				case OP_ISUB_M:
					out.op ( 0, true, 0x2B, 1, dst, source_at ( out, form, s8, d8, imm, near, RAX ) );
					usage [ d8 ] = mark;
					break;

				case OP_IMUL_R:
					if ( s8 != d8 )
						out.op ( 0, true, 0x0FAF, 2, dst, direct ( src ) );
					else
						out.op ( 0, true, 0x69, 1, dst, direct ( dst ), 4, imm );

					usage [ d8 ] = mark;
					break;

				case OP_IMUL_M:
					out.op ( 0, true, 0x0FAF, 2, dst, source_at ( out, form, s8, d8, imm, near, RAX ) );
					usage [ d8 ] = mark;
					break;

				case OP_IMULH_R:
				case OP_IMULH_M:
				case OP_ISMULH_R:
				case OP_ISMULH_M: {

					uint8_t kind = form.decode [ bytes [ 0 ] ];
					bool memory_form = kind == OP_IMULH_M || kind == OP_ISMULH_M;
					uint32_t which = kind == OP_IMULH_R || kind == OP_IMULH_M ? 4 : 5;
					operand factor = memory_form ? source_at ( out, form, s8, d8, imm, near, RCX ) : direct ( src );

					out.op ( 0, true, 0x8B, 1, RAX, direct ( dst ) );
					out.op ( 0, true, 0xF7, 1, static_cast < int32_t > ( which ), factor );
					out.op ( 0, true, 0x8B, 1, dst, direct ( RDX ) );
					usage [ d8 ] = mark;
					break;

				}

				case OP_IMUL_RCP:
					if ( imm && ( imm & ( imm - 1 ) ) ) {

						out.load_constant ( RAX, reciprocal ( imm ) );
						out.op ( 0, true, 0x0FAF, 2, dst, direct ( RAX ) );
						usage [ d8 ] = mark;

					}

					break;

				case OP_INEG_R:
					out.op ( 0, true, 0xF7, 1, 3, direct ( dst ) );
					usage [ d8 ] = mark;
					break;

				case OP_IXOR_R:
					if ( s8 != d8 )
						out.op ( 0, true, 0x33, 1, dst, direct ( src ) );
					else
						out.op ( 0, true, 0x81, 1, 6, direct ( dst ), 4, imm );

					usage [ d8 ] = mark;
					break;

				case OP_IXOR_M:
					out.op ( 0, true, 0x33, 1, dst, source_at ( out, form, s8, d8, imm, near, RAX ) );
					usage [ d8 ] = mark;
					break;

				case OP_IROR_R:
				case OP_IROL_R: {

					int32_t way = form.decode [ bytes [ 0 ] ] == OP_IROR_R ? 1 : 0;

					if ( s8 != d8 ) {

						out.op ( 0, false, 0x8B, 1, RCX, direct ( src ) );
						out.op ( 0, true, 0xD3, 1, way, direct ( dst ) );

					} else
						out.op ( 0, true, 0xC1, 1, way, direct ( dst ), 1, imm & 63 );

					usage [ d8 ] = mark;
					break;

				}

				case OP_ISWAP_R:
					if ( s8 != d8 ) {

						out.op ( 0, true, 0x87, 1, dst, direct ( src ) );
						usage [ d8 ] = mark;
						usage [ s8 ] = mark;

					}

					break;

				case OP_FSWAP_R:
					out.op ( 0x66, false, 0x0FC6, 2, static_cast < int32_t > ( d8 ), direct ( static_cast < int32_t > ( d8 ) ), 1, 1 );
					break;

				case OP_FADD_R:
					out.op ( 0x66, false, 0x0F58, 2, fd, direct ( 8 + fs ) );
					break;

				case OP_FADD_M:
					out.op ( 0xF3, false, 0x0FE6, 2, temp, scratch_at ( out, static_cast < int32_t > ( s8 ), imm, near, RAX ) );
					out.op ( 0x66, false, 0x0F58, 2, fd, direct ( temp ) );
					break;

				case OP_FSUB_R:
					out.op ( 0x66, false, 0x0F5C, 2, fd, direct ( 8 + fs ) );
					break;

				case OP_FSUB_M:
					out.op ( 0xF3, false, 0x0FE6, 2, temp, scratch_at ( out, static_cast < int32_t > ( s8 ), imm, near, RAX ) );
					out.op ( 0x66, false, 0x0F5C, 2, fd, direct ( temp ) );
					break;

				case OP_FSCAL_R:
					out.op ( 0x66, false, 0x0F57, 2, fd, direct ( sign_mask ) );
					break;

				case OP_FMUL_R:
					out.op ( 0x66, false, 0x0F59, 2, 4 + fd, direct ( 8 + fs ) );
					break;

				case OP_FDIV_M:
					out.op ( 0xF3, false, 0x0FE6, 2, temp, scratch_at ( out, static_cast < int32_t > ( s8 ), imm, near, RAX ) );
					out.op ( 0x66, false, 0x0F54, 2, temp, direct ( mantissa_mask ) );
					out.op ( 0x66, false, 0x0F56, 2, temp, direct ( exponent_mask ) );
					out.op ( 0x66, false, 0x0F5E, 2, 4 + fd, direct ( temp ) );
					break;

				case OP_FSQRT_R:
					out.op ( 0x66, false, 0x0F51, 2, 4 + fd, direct ( 4 + fd ) );
					break;

				case OP_CBRANCH: {

					uint32_t shift = ( mod >> 4 ) + 8;
					uint32_t step = ( imm | 1u << shift ) & ~( 1u << ( shift - 1 ) );
					int16_t target = usage [ d8 ];

					out.op ( 0, true, 0x81, 1, 0, direct ( dst ), 4, step );
					out.op ( 0, true, 0xF7, 1, 0, direct ( dst ), 4, 255u << shift );
					out.branch ( 0x84, target < 0 ? body : labels [ target + 1 ] );

					for ( int16_t& last : usage )
						last = mark;

					break;

				}

				case OP_CFROUND: {

					uint8_t* skip = nullptr;

					out.op ( 0, true, 0x8B, 1, RAX, direct ( src ) );

					if ( imm & 63 )
						out.op ( 0, true, 0xC1, 1, 1, direct ( RAX ), 1, imm & 63 );

					if ( form.v2 ) {

						out.byte ( 0xA8 );
						out.byte ( 0x3C );
						skip = out.here ( );
						out.byte ( 0x75 );
						out.byte ( 0 );

					}

					out.op ( 0, false, 0x81, 1, 4, direct ( RAX ), 4, 3 );
					out.op ( 0, false, 0xC1, 1, 4, direct ( RAX ), 1, 13 );
					out.op ( 0, false, 0x81, 1, 1, direct ( RAX ), 4, 0x9FC0 );
					out.op ( 0, false, 0x89, 1, RAX, memory ( RSP, slot_control ) );
					out.op ( 0, false, 0x0FAE, 2, 2, memory ( RSP, slot_control ) );

					if ( skip )
						skip [ 1 ] = static_cast < uint8_t > ( out.here ( ) - ( skip + 2 ) );

					break;

				}

				case OP_ISTORE: {

					uint32_t mask = ( mod >> 4 ) < 14 ? near : form.level3;

					out.op ( 0, true, 0x89, 1, src, scratch_at ( out, static_cast < int32_t > ( d8 ), imm, mask, RAX ) );
					break;

				}

				default:
					break;

			}

		}

		if ( out.size ( ) + 512 > room )
			return false;

		if ( shape.whole_loop )
			loop_tail ( out, shape, form, top );

		epilogue ( out );

		return true;

	}

}
