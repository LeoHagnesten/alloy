#pragma once

#include "base/base.hh"

namespace randomx {

	static constexpr int32_t superscalar_latency = 170;
	static constexpr int32_t superscalar_room = 3 * superscalar_latency + 2;

	enum scalar_type : uint8_t {

		SCALAR_ISUB_R,
		SCALAR_IXOR_R,
		SCALAR_IADD_RS,
		SCALAR_IMUL_R,
		SCALAR_IROR_C,
		SCALAR_IADD_C7,
		SCALAR_IXOR_C7,
		SCALAR_IADD_C8,
		SCALAR_IXOR_C8,
		SCALAR_IADD_C9,
		SCALAR_IXOR_C9,
		SCALAR_IMULH_R,
		SCALAR_ISMULH_R,
		SCALAR_IMUL_RCP,
		SCALAR_INVALID = 0xFF,

	};

	struct instruction {

		uint8_t opcode = 0;
		uint8_t dst = 0;
		uint8_t src = 0;
		uint8_t mod = 0;
		uint32_t imm32 = 0;

	};

	struct scalar_program {

		instruction code [ superscalar_room ];
		uint64_t reciprocals [ superscalar_room ] = { };
		int32_t size = 0;
		int32_t address_register = 0;

	};

	class seed_generator {

	public:

		seed_generator ( const void* seed, size_t size );

		uint8_t next_byte ( );

		uint32_t next_word ( );

	private:

		void refill ( uint32_t needed );

		uint8_t data [ 64 ] = { };
		uint32_t used = 64;

	};

	uint64_t reciprocal ( uint32_t divisor );

	void generate_superscalar ( scalar_program& out, seed_generator& random, int32_t latency = superscalar_latency );

	void run_superscalar ( uint64_t* r, const scalar_program& program );

}
