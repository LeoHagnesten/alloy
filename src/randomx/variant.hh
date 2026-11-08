#pragma once

#include "base/base.hh"

namespace randomx {

	enum operation : uint8_t {

		OP_IADD_RS,
		OP_IADD_M,
		OP_ISUB_R,
		OP_ISUB_M,
		OP_IMUL_R,
		OP_IMUL_M,
		OP_IMULH_R,
		OP_IMULH_M,
		OP_ISMULH_R,
		OP_ISMULH_M,
		OP_IMUL_RCP,
		OP_INEG_R,
		OP_IXOR_R,
		OP_IXOR_M,
		OP_IROR_R,
		OP_IROL_R,
		OP_ISWAP_R,
		OP_FSWAP_R,
		OP_FADD_R,
		OP_FADD_M,
		OP_FSUB_R,
		OP_FSUB_M,
		OP_FSCAL_R,
		OP_FMUL_R,
		OP_FDIV_M,
		OP_FSQRT_R,
		OP_CBRANCH,
		OP_CFROUND,
		OP_ISTORE,
		OP_NOP,
		OP_COUNT,

	};

	enum variant_id : uint8_t {

		VARIANT_RX0,
		VARIANT_RX2,
		VARIANT_WOW,
		VARIANT_ARQ,
		VARIANT_GRAFT,
		VARIANT_SFX,
		VARIANT_YADA,
		VARIANT_KEVA,
		VARIANT_XEQ,
		VARIANT_COUNT,

	};

	static constexpr int32_t program_room = 384;
	static constexpr uint32_t scratchpad_room = 2097152;
	static constexpr uint32_t level1_bytes = 16384;

	struct variant {

		uint8_t id;
		const char* key;
		const wchar_t* name;
		const wchar_t* coin;
		const char* salt;
		uint32_t salt_size;
		uint32_t passes;
		uint32_t lanes;
		int32_t latency;
		uint32_t level2;
		uint32_t level3;
		int32_t program_size;
		int32_t iterations;
		int32_t count;
		bool v2;
		const uint8_t ( *keys ) [ 16 ];
		uint8_t frequency [ OP_COUNT ];
		uint8_t decode [ 256 ];

	};

	const variant& variant_of ( uint8_t id );

	int32_t find_variant ( const char* key );

}
