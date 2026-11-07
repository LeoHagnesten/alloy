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

	static operand scratch_at ( assembler& out, int32_t source, uint32_t imm, uint32_t mask, int32_t into ) {

		out.op ( 0, false, 0x8D, 1, into, memory ( R8 + source, static_cast < int32_t > ( imm ) ) );
		out.op ( 0, false, 0x81, 1, 4, direct ( into ), 4, mask );

		return memory ( RSI, 0, into );

	}

	static operand source_at ( assembler& out, uint32_t s8, uint32_t d8, uint32_t imm, uint32_t near, int32_t into ) {

		if ( s8 != d8 )
			return scratch_at ( out, static_cast < int32_t > ( s8 ), imm, near, into );

		return memory ( RSI, static_cast < int32_t > ( imm & level3_mask ) );

	}

	static void prologue ( assembler& out, const program_shape& shape ) {

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
		out.op ( 0, false, 0xC7, 1, 0, direct ( RBP ), 4, program_iterations );
		out.op ( 0, false, 0x8B, 1, RAX, memory ( RBX, offset_mx ) );
		out.op ( 0, false, 0x89, 1, RAX, memory ( RSP, slot_first ) );
		out.op ( 0, false, 0x89, 1, RAX, memory ( RSP, slot_mx ) );
		out.op ( 0, false, 0x8B, 1, RAX, memory ( RBX, offset_ma ) );
		out.op ( 0, false, 0x89, 1, RAX, memory ( RSP, slot_second ) );
		out.op ( 0, false, 0x89, 1, RAX, memory ( RSP, slot_ma ) );

	}

	static void loop_head ( assembler& out, const program_shape& shape ) {

		out.op ( 0, true, 0x8B, 1, RAX, direct ( R8 + static_cast < int32_t > ( shape.read_registers [ 0 ] ) ) );
		out.op ( 0, true, 0x33, 1, RAX, direct ( R8 + static_cast < int32_t > ( shape.read_registers [ 1 ] ) ) );
		out.op ( 0, true, 0x8B, 1, RDX, direct ( RAX ) );
		out.op ( 0, true, 0xC1, 1, 5, direct ( RDX ), 1, 32 );
		out.op ( 0, false, 0x33, 1, RAX, memory ( RSP, slot_first ) );
		out.op ( 0, false, 0x81, 1, 4, direct ( RAX ), 4, level3_line );
		out.op ( 0, false, 0x33, 1, RDX, memory ( RSP, slot_second ) );
		out.op ( 0, false, 0x81, 1, 4, direct ( RDX ), 4, level3_line );
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

	static void loop_tail ( assembler& out, const program_shape& shape, const uint8_t* top ) {

		out.op ( 0, true, 0x8B, 1, RAX, direct ( R8 + static_cast < int32_t > ( shape.read_registers [ 2 ] ) ) );
		out.op ( 0, true, 0x33, 1, RAX, direct ( R8 + static_cast < int32_t > ( shape.read_registers [ 3 ] ) ) );
		out.op ( 0, false, 0x33, 1, RAX, memory ( RSP, slot_mx ) );
		out.op ( 0, false, 0x81, 1, 4, direct ( RAX ), 4, dataset_line );
		out.op ( 0, false, 0x0F18, 2, 0, memory ( RDI, 0, RAX ) );
		out.op ( 0, false, 0x8B, 1, RCX, memory ( RSP, slot_ma ) );

		for ( int32_t i = 0; i < 8; i++ )
			out.op ( 0, true, 0x33, 1, R8 + i, memory ( RDI, i * 8, RCX ) );

		out.op ( 0, false, 0x89, 1, RCX, memory ( RSP, slot_mx ) );
		out.op ( 0, false, 0x89, 1, RAX, memory ( RSP, slot_ma ) );
		out.op ( 0, false, 0x8B, 1, RDX, memory ( RSP, slot_second ) );

		for ( int32_t i = 0; i < 8; i++ )
			out.op ( 0, true, 0x89, 1, R8 + i, memory ( RSI, i * 8, RDX ) );

		out.op ( 0, false, 0x8B, 1, RAX, memory ( RSP, slot_first ) );

		for ( int32_t i = 0; i < 4; i++ ) {

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

		assembler out ( destination );
		uint8_t* labels [ program_size ];
		int16_t usage [ 8 ] = { -1, -1, -1, -1, -1, -1, -1, -1 };

		prologue ( out, shape );

		uint8_t* top = out.here ( );

		if ( shape.whole_loop )
			loop_head ( out, shape );

		uint8_t* body = out.here ( );

		for ( int32_t i = 0; i < program_size; i++ ) {

			if ( out.size ( ) + 128 > room )
				return false;

			labels [ i ] = out.here ( );

			const uint8_t* bytes = code + i * 8;
			uint32_t opcode = bytes [ 0 ];
			uint32_t d8 = bytes [ 1 ] & 7;
			uint32_t s8 = bytes [ 2 ] & 7;
			uint32_t d4 = bytes [ 1 ] & 3;
			uint32_t s4 = bytes [ 2 ] & 3;
			uint32_t mod = bytes [ 3 ];
			uint32_t imm = base::load_little ( bytes + 4 );
			uint32_t near = mod & 3 ? level1_mask : level2_mask;
			int32_t dst = R8 + static_cast < int32_t > ( d8 );
			int32_t src = R8 + static_cast < int32_t > ( s8 );
			int16_t mark = static_cast < int16_t > ( i );

			if ( opcode < 16 ) {

				uint32_t shift = mod >> 2 & 3;

				out.op ( 0, true, 0x8D, 1, dst, memory ( dst, d8 == 5 ? static_cast < int32_t > ( imm ) : 0, src, shift ) );
				usage [ d8 ] = mark;

			} else if ( opcode < 23 ) {

				out.op ( 0, true, 0x03, 1, dst, source_at ( out, s8, d8, imm, near, RAX ) );
				usage [ d8 ] = mark;

			} else if ( opcode < 39 ) {

				if ( s8 != d8 )
					out.op ( 0, true, 0x2B, 1, dst, direct ( src ) );
				else
					out.op ( 0, true, 0x81, 1, 5, direct ( dst ), 4, imm );

				usage [ d8 ] = mark;

			} else if ( opcode < 46 ) {

				out.op ( 0, true, 0x2B, 1, dst, source_at ( out, s8, d8, imm, near, RAX ) );
				usage [ d8 ] = mark;

			} else if ( opcode < 62 ) {

				if ( s8 != d8 )
					out.op ( 0, true, 0x0FAF, 2, dst, direct ( src ) );
				else
					out.op ( 0, true, 0x69, 1, dst, direct ( dst ), 4, imm );

				usage [ d8 ] = mark;

			} else if ( opcode < 66 ) {

				out.op ( 0, true, 0x0FAF, 2, dst, source_at ( out, s8, d8, imm, near, RAX ) );
				usage [ d8 ] = mark;

			} else if ( opcode < 76 ) {

				bool memory_form = opcode == 70 || opcode == 75;
				uint32_t form = opcode < 71 ? 4 : 5;
				operand factor = memory_form ? source_at ( out, s8, d8, imm, near, RCX ) : direct ( src );

				out.op ( 0, true, 0x8B, 1, RAX, direct ( dst ) );
				out.op ( 0, true, 0xF7, 1, static_cast < int32_t > ( form ), factor );
				out.op ( 0, true, 0x8B, 1, dst, direct ( RDX ) );
				usage [ d8 ] = mark;

			} else if ( opcode < 84 ) {

				if ( imm && ( imm & ( imm - 1 ) ) ) {

					out.load_constant ( RAX, reciprocal ( imm ) );
					out.op ( 0, true, 0x0FAF, 2, dst, direct ( RAX ) );
					usage [ d8 ] = mark;

				}

			} else if ( opcode < 86 ) {

				out.op ( 0, true, 0xF7, 1, 3, direct ( dst ) );
				usage [ d8 ] = mark;

			} else if ( opcode < 101 ) {

				if ( s8 != d8 )
					out.op ( 0, true, 0x33, 1, dst, direct ( src ) );
				else
					out.op ( 0, true, 0x81, 1, 6, direct ( dst ), 4, imm );

				usage [ d8 ] = mark;

			} else if ( opcode < 106 ) {

				out.op ( 0, true, 0x33, 1, dst, source_at ( out, s8, d8, imm, near, RAX ) );
				usage [ d8 ] = mark;

			} else if ( opcode < 116 ) {

				int32_t way = opcode < 114 ? 1 : 0;

				if ( s8 != d8 ) {

					out.op ( 0, false, 0x8B, 1, RCX, direct ( src ) );
					out.op ( 0, true, 0xD3, 1, way, direct ( dst ) );

				} else
					out.op ( 0, true, 0xC1, 1, way, direct ( dst ), 1, imm & 63 );

				usage [ d8 ] = mark;

			} else if ( opcode < 120 ) {

				if ( s8 != d8 ) {

					out.op ( 0, true, 0x87, 1, dst, direct ( src ) );
					usage [ d8 ] = mark;
					usage [ s8 ] = mark;

				}

			} else if ( opcode < 124 )
				out.op ( 0x66, false, 0x0FC6, 2, static_cast < int32_t > ( d8 ), direct ( static_cast < int32_t > ( d8 ) ), 1, 1 );
			else if ( opcode < 140 )
				out.op ( 0x66, false, 0x0F58, 2, static_cast < int32_t > ( d4 ), direct ( 8 + static_cast < int32_t > ( s4 ) ) );
			else if ( opcode < 145 ) {

				out.op ( 0xF3, false, 0x0FE6, 2, temp, scratch_at ( out, static_cast < int32_t > ( s8 ), imm, near, RAX ) );
				out.op ( 0x66, false, 0x0F58, 2, static_cast < int32_t > ( d4 ), direct ( temp ) );

			} else if ( opcode < 161 )
				out.op ( 0x66, false, 0x0F5C, 2, static_cast < int32_t > ( d4 ), direct ( 8 + static_cast < int32_t > ( s4 ) ) );
			else if ( opcode < 166 ) {

				out.op ( 0xF3, false, 0x0FE6, 2, temp, scratch_at ( out, static_cast < int32_t > ( s8 ), imm, near, RAX ) );
				out.op ( 0x66, false, 0x0F5C, 2, static_cast < int32_t > ( d4 ), direct ( temp ) );

			} else if ( opcode < 172 )
				out.op ( 0x66, false, 0x0F57, 2, static_cast < int32_t > ( d4 ), direct ( sign_mask ) );
			else if ( opcode < 204 )
				out.op ( 0x66, false, 0x0F59, 2, 4 + static_cast < int32_t > ( d4 ), direct ( 8 + static_cast < int32_t > ( s4 ) ) );
			else if ( opcode < 208 ) {

				out.op ( 0xF3, false, 0x0FE6, 2, temp, scratch_at ( out, static_cast < int32_t > ( s8 ), imm, near, RAX ) );
				out.op ( 0x66, false, 0x0F54, 2, temp, direct ( mantissa_mask ) );
				out.op ( 0x66, false, 0x0F56, 2, temp, direct ( exponent_mask ) );
				out.op ( 0x66, false, 0x0F5E, 2, 4 + static_cast < int32_t > ( d4 ), direct ( temp ) );

			} else if ( opcode < 214 )
				out.op ( 0x66, false, 0x0F51, 2, 4 + static_cast < int32_t > ( d4 ), direct ( 4 + static_cast < int32_t > ( d4 ) ) );
			else if ( opcode < 239 ) {

				uint32_t shift = ( mod >> 4 ) + 8;
				uint32_t step = ( imm | 1u << shift ) & ~( 1u << ( shift - 1 ) );
				int16_t target = usage [ d8 ];

				out.op ( 0, true, 0x81, 1, 0, direct ( dst ), 4, step );
				out.op ( 0, true, 0xF7, 1, 0, direct ( dst ), 4, 255u << shift );
				out.branch ( 0x84, target < 0 ? body : labels [ target + 1 ] );

				for ( int16_t& last : usage )
					last = mark;

			} else if ( opcode < 240 ) {

				out.op ( 0, true, 0x8B, 1, RAX, direct ( src ) );

				if ( imm & 63 )
					out.op ( 0, true, 0xC1, 1, 1, direct ( RAX ), 1, imm & 63 );

				out.op ( 0, false, 0x81, 1, 4, direct ( RAX ), 4, 3 );
				out.op ( 0, false, 0xC1, 1, 4, direct ( RAX ), 1, 13 );
				out.op ( 0, false, 0x81, 1, 1, direct ( RAX ), 4, 0x9FC0 );
				out.op ( 0, false, 0x89, 1, RAX, memory ( RSP, slot_control ) );
				out.op ( 0, false, 0x0FAE, 2, 2, memory ( RSP, slot_control ) );

			} else {

				uint32_t mask = ( mod >> 4 ) < 14 ? near : level3_mask;

				out.op ( 0, true, 0x89, 1, src, scratch_at ( out, static_cast < int32_t > ( d8 ), imm, mask, RAX ) );

			}

		}

		if ( out.size ( ) + 512 > room )
			return false;

		if ( shape.whole_loop )
			loop_tail ( out, shape, top );

		epilogue ( out );

		return true;

	}

}
