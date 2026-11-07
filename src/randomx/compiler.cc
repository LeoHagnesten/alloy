#include "randomx/compiler.hh"
#include "randomx/assembler.hh"
#include "base/memory.hh"

namespace randomx {

	static constexpr size_t program_room = 16384;

	static void emit ( assembler& out, const instruction& code, uint64_t factor ) {

		int32_t dst = R8 + ( code.dst & 7 );
		int32_t src = R8 + ( code.src & 7 );

		switch ( code.opcode ) {

			case SCALAR_ISUB_R:
				out.op ( 0, true, 0x2B, 1, dst, direct ( src ) );
				break;

			case SCALAR_IXOR_R:
				out.op ( 0, true, 0x33, 1, dst, direct ( src ) );
				break;

			case SCALAR_IADD_RS:
				out.op ( 0, true, 0x8D, 1, dst, memory ( dst, 0, src, code.mod >> 2 & 3 ) );
				break;

			case SCALAR_IMUL_R:
				out.op ( 0, true, 0x0FAF, 2, dst, direct ( src ) );
				break;

			case SCALAR_IROR_C:
				out.op ( 0, true, 0xC1, 1, 1, direct ( dst ), 1, code.imm32 & 63 );
				break;

			case SCALAR_IADD_C7:
			case SCALAR_IADD_C8:
			case SCALAR_IADD_C9:
				out.op ( 0, true, 0x81, 1, 0, direct ( dst ), 4, code.imm32 );
				break;

			case SCALAR_IXOR_C7:
			case SCALAR_IXOR_C8:
			case SCALAR_IXOR_C9:
				out.op ( 0, true, 0x81, 1, 6, direct ( dst ), 4, code.imm32 );
				break;

			case SCALAR_IMULH_R:
			case SCALAR_ISMULH_R:
				out.op ( 0, true, 0x8B, 1, RAX, direct ( dst ) );
				out.op ( 0, true, 0xF7, 1, code.opcode == SCALAR_IMULH_R ? 4 : 5, direct ( src ) );
				out.op ( 0, true, 0x8B, 1, dst, direct ( RDX ) );
				break;

			case SCALAR_IMUL_RCP:
				out.load_constant ( RAX, factor );
				out.op ( 0, true, 0x0FAF, 2, dst, direct ( RAX ) );
				break;

		}

	}

	scalar_compiler::~scalar_compiler ( ) {

		release ( );

	}

	void scalar_compiler::release ( ) {

		base::release_pages ( code );
		code = nullptr;
		code_size = 0;

	}

	bool scalar_compiler::build ( const scalar_program* programs, int32_t count ) {

		release ( );
		code_size = program_room * static_cast < size_t > ( count );
		code = static_cast < uint8_t* > ( base::allocate_pages ( code_size ) );

		if ( !code )
			return false;

		for ( int32_t p = 0; p < count; p++ ) {

			assembler out ( code + program_room * p );

			entries [ p ] = reinterpret_cast < native_program > ( out.here ( ) );

			for ( int32_t reg = R12; reg <= R15; reg++ )
				out.push ( reg );

			for ( int32_t i = 0; i < 8; i++ )
				out.op ( 0, true, 0x8B, 1, R8 + i, memory ( RCX, i * 8 ) );

			for ( int32_t i = 0; i < programs [ p ].size; i++ )
				emit ( out, programs [ p ].code [ i ], programs [ p ].reciprocals [ i ] );

			for ( int32_t i = 0; i < 8; i++ )
				out.op ( 0, true, 0x89, 1, R8 + i, memory ( RCX, i * 8 ) );

			for ( int32_t reg = R15; reg >= R12; reg-- )
				out.pop ( reg );

			out.byte ( 0xC3 );

		}

		if ( !base::make_executable ( code, code_size ) ) {

			release ( );

			return false;

		}

		return true;

	}

}
