#include "randomx/assembler.hh"
#include "base/bits.hh"

namespace randomx {

	void assembler::word ( uint32_t value ) {

		base::store_little ( at, value );
		at += 4;

	}

	void assembler::quad ( uint64_t value ) {

		base::store_little64 ( at, value );
		at += 8;

	}

	void assembler::op ( uint8_t prefix, bool wide, uint32_t opcode, int32_t opcode_size, int32_t reg, const operand& rm, int32_t imm_size, uint32_t imm ) {

		bool registered = rm.reg >= 0;
		int32_t base = registered ? rm.reg : rm.base;
		bool extended_index = !registered && rm.index >= 0 && ( rm.index & 8 );
		uint32_t rex = 0x40 | ( wide ? 8u : 0u ) | ( reg & 8 ? 4u : 0u ) | ( extended_index ? 2u : 0u ) | ( base & 8 ? 1u : 0u );

		if ( prefix )
			byte ( prefix );

		if ( rex != 0x40 )
			byte ( rex );

		for ( int32_t i = opcode_size - 1; i >= 0; i-- )
			byte ( opcode >> ( i * 8 ) );

		if ( registered )
			byte ( 0xC0 | ( reg & 7 ) << 3 | ( base & 7 ) );
		else {

			bool sib = rm.index >= 0 || ( base & 7 ) == 4;
			uint32_t mode = rm.disp == 0 && ( base & 7 ) != 5 ? 0u : ( rm.disp >= -128 && rm.disp <= 127 ? 1u : 2u );

			byte ( mode << 6 | ( reg & 7 ) << 3 | ( sib ? 4u : static_cast < uint32_t > ( base & 7 ) ) );

			if ( sib )
				byte ( static_cast < uint32_t > ( rm.scale ) << 6 | static_cast < uint32_t > ( rm.index >= 0 ? rm.index & 7 : 4 ) << 3 | ( base & 7 ) );

			if ( mode == 1 )
				byte ( static_cast < uint32_t > ( rm.disp ) );
			else if ( mode == 2 )
				word ( static_cast < uint32_t > ( rm.disp ) );

		}

		if ( imm_size == 1 )
			byte ( imm );
		else if ( imm_size == 4 )
			word ( imm );

	}

	void assembler::push ( int32_t reg ) {

		if ( reg & 8 )
			byte ( 0x41 );

		byte ( 0x50 + ( reg & 7 ) );

	}

	void assembler::pop ( int32_t reg ) {

		if ( reg & 8 )
			byte ( 0x41 );

		byte ( 0x58 + ( reg & 7 ) );

	}

	void assembler::load_constant ( int32_t reg, uint64_t value ) {

		byte ( reg & 8 ? 0x49 : 0x48 );
		byte ( 0xB8 + ( reg & 7 ) );
		quad ( value );

	}

	void assembler::branch ( uint8_t condition, const uint8_t* target ) {

		byte ( 0x0F );
		byte ( condition );
		word ( static_cast < uint32_t > ( target - ( at + 4 ) ) );

	}

}
