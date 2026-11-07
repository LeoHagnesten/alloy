#include "randomx/compiler.hh"
#include "base/bits.hh"
#include "base/memory.hh"
#include <windows.h>

namespace randomx {

	static constexpr size_t program_room = 16384;

	class emitter {

	public:

		explicit emitter ( uint8_t* at ) : at ( at ) { }

		void byte ( uint8_t value ) {

			*at++ = value;

		}

		void word ( uint32_t value ) {

			base::store_little ( at, value );
			at += 4;

		}

		void quad ( uint64_t value ) {

			base::store_little64 ( at, value );
			at += 8;

		}

		uint8_t* at;

	};

	static void prologue ( emitter& out ) {

		static const uint8_t saves [ ] = { 0x41, 0x54, 0x41, 0x55, 0x41, 0x56, 0x41, 0x57 };

		for ( uint8_t value : saves )
			out.byte ( value );

		for ( uint32_t i = 0; i < 8; i++ ) {

			out.byte ( 0x4C );
			out.byte ( 0x8B );
			out.byte ( static_cast < uint8_t > ( 0x41 | i << 3 ) );
			out.byte ( static_cast < uint8_t > ( i * 8 ) );

		}

	}

	static void epilogue ( emitter& out ) {

		static const uint8_t restores [ ] = { 0x41, 0x5F, 0x41, 0x5E, 0x41, 0x5D, 0x41, 0x5C, 0xC3 };

		for ( uint32_t i = 0; i < 8; i++ ) {

			out.byte ( 0x4C );
			out.byte ( 0x89 );
			out.byte ( static_cast < uint8_t > ( 0x41 | i << 3 ) );
			out.byte ( static_cast < uint8_t > ( i * 8 ) );

		}

		for ( uint8_t value : restores )
			out.byte ( value );

	}

	static void emit ( emitter& out, const instruction& code, uint64_t factor ) {

		uint8_t dst = code.dst & 7;
		uint8_t src = code.src & 7;

		switch ( code.opcode ) {

			case SCALAR_ISUB_R:
				out.byte ( 0x4D );
				out.byte ( 0x29 );
				out.byte ( static_cast < uint8_t > ( 0xC0 | src << 3 | dst ) );
				break;

			case SCALAR_IXOR_R:
				out.byte ( 0x4D );
				out.byte ( 0x31 );
				out.byte ( static_cast < uint8_t > ( 0xC0 | src << 3 | dst ) );
				break;

			case SCALAR_IADD_RS:
				out.byte ( 0x4F );
				out.byte ( 0x8D );
				out.byte ( static_cast < uint8_t > ( dst << 3 | 4 ) );
				out.byte ( static_cast < uint8_t > ( ( code.mod >> 2 & 3 ) << 6 | src << 3 | dst ) );
				break;

			case SCALAR_IMUL_R:
				out.byte ( 0x4D );
				out.byte ( 0x0F );
				out.byte ( 0xAF );
				out.byte ( static_cast < uint8_t > ( 0xC0 | dst << 3 | src ) );
				break;

			case SCALAR_IROR_C:
				out.byte ( 0x49 );
				out.byte ( 0xC1 );
				out.byte ( static_cast < uint8_t > ( 0xC8 | dst ) );
				out.byte ( static_cast < uint8_t > ( code.imm32 & 63 ) );
				break;

			case SCALAR_IADD_C7:
			case SCALAR_IADD_C8:
			case SCALAR_IADD_C9:
				out.byte ( 0x49 );
				out.byte ( 0x81 );
				out.byte ( static_cast < uint8_t > ( 0xC0 | dst ) );
				out.word ( code.imm32 );
				break;

			case SCALAR_IXOR_C7:
			case SCALAR_IXOR_C8:
			case SCALAR_IXOR_C9:
				out.byte ( 0x49 );
				out.byte ( 0x81 );
				out.byte ( static_cast < uint8_t > ( 0xF0 | dst ) );
				out.word ( code.imm32 );
				break;

			case SCALAR_IMULH_R:
			case SCALAR_ISMULH_R:
				out.byte ( 0x4C );
				out.byte ( 0x89 );
				out.byte ( static_cast < uint8_t > ( 0xC0 | dst << 3 ) );
				out.byte ( 0x49 );
				out.byte ( 0xF7 );
				out.byte ( static_cast < uint8_t > ( ( code.opcode == SCALAR_IMULH_R ? 0xE0 : 0xE8 ) | src ) );
				out.byte ( 0x49 );
				out.byte ( 0x89 );
				out.byte ( static_cast < uint8_t > ( 0xD0 | dst ) );
				break;

			case SCALAR_IMUL_RCP:
				out.byte ( 0x48 );
				out.byte ( 0xB8 );
				out.quad ( factor );
				out.byte ( 0x4C );
				out.byte ( 0x0F );
				out.byte ( 0xAF );
				out.byte ( static_cast < uint8_t > ( 0xC0 | dst << 3 ) );
				break;

		}

	}

	scalar_compiler::~scalar_compiler ( ) {

		release ( );

	}

	void scalar_compiler::release ( ) {

		if ( code )
			VirtualFree ( code, 0, MEM_RELEASE );

		code = nullptr;
		code_size = 0;

	}

	bool scalar_compiler::build ( const scalar_program* programs, int32_t count ) {

		release ( );
		code_size = program_room * static_cast < size_t > ( count );
		code = static_cast < uint8_t* > ( VirtualAlloc ( nullptr, code_size, MEM_RESERVE | MEM_COMMIT, PAGE_READWRITE ) );

		if ( !code )
			return false;

		for ( int32_t p = 0; p < count; p++ ) {

			emitter out ( code + program_room * p );

			entries [ p ] = reinterpret_cast < native_program > ( out.at );
			prologue ( out );

			for ( int32_t i = 0; i < programs [ p ].size; i++ )
				emit ( out, programs [ p ].code [ i ], programs [ p ].reciprocals [ i ] );

			epilogue ( out );

		}

		DWORD previous = 0;

		if ( !VirtualProtect ( code, code_size, PAGE_EXECUTE_READ, &previous ) ) {

			release ( );

			return false;

		}

		FlushInstructionCache ( GetCurrentProcess ( ), code, code_size );

		return true;

	}

}
