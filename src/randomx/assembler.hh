#pragma once

#include "base/base.hh"

namespace randomx {

	enum gpr : uint8_t {

		RAX,
		RCX,
		RDX,
		RBX,
		RSP,
		RBP,
		RSI,
		RDI,
		R8,
		R9,
		R10,
		R11,
		R12,
		R13,
		R14,
		R15,

	};

	struct operand {

		int8_t reg = -1;
		int8_t base = -1;
		int8_t index = -1;
		uint8_t scale = 0;
		int32_t disp = 0;

	};

	inline operand direct ( int32_t reg ) {

		operand out;

		out.reg = static_cast < int8_t > ( reg );

		return out;

	}

	inline operand memory ( int32_t base, int32_t disp = 0, int32_t index = -1, uint32_t scale = 0 ) {

		operand out;

		out.base = static_cast < int8_t > ( base );
		out.index = static_cast < int8_t > ( index );
		out.scale = static_cast < uint8_t > ( scale );
		out.disp = disp;

		return out;

	}

	class assembler {

	public:

		explicit assembler ( uint8_t* origin ) : origin ( origin ), at ( origin ) { }

		void byte ( uint32_t value ) {

			*at++ = static_cast < uint8_t > ( value );

		}

		void word ( uint32_t value );

		void quad ( uint64_t value );

		void op ( uint8_t prefix, bool wide, uint32_t opcode, int32_t opcode_size, int32_t reg, const operand& rm, int32_t imm_size = 0, uint32_t imm = 0 );

		void push ( int32_t reg );

		void pop ( int32_t reg );

		void load_constant ( int32_t reg, uint64_t value );

		void branch ( uint8_t condition, const uint8_t* target );

		size_t size ( ) const {

			return static_cast < size_t > ( at - origin );

		}

		uint8_t* here ( ) const {

			return at;

		}

	private:

		uint8_t* origin;
		uint8_t* at;

	};

}
