#pragma once

#include "randomx/variant.hh"
#include <emmintrin.h>

namespace randomx {

	static constexpr uint32_t dataset_line = 0x7FFFFFC0u;
	static constexpr size_t native_room = 32768;

	struct alignas ( 16 ) registers {

		uint64_t r [ 8 ];
		__m128d f [ 4 ];
		__m128d e [ 4 ];
		__m128d a [ 4 ];
		__m128d mantissa;
		__m128d exponent;
		__m128d sign;
		uint8_t* scratchpad;
		const uint8_t* dataset;
		uint32_t mx;
		uint32_t ma;

	};

	static_assert ( __builtin_offsetof ( registers, f ) == 64, "f moved" );
	static_assert ( __builtin_offsetof ( registers, mantissa ) == 256, "constants moved" );
	static_assert ( __builtin_offsetof ( registers, scratchpad ) == 304, "pointers moved" );
	static_assert ( __builtin_offsetof ( registers, mx ) == 320, "addresses moved" );

	using native_body = void ( * ) ( registers* state );

	struct program_shape {

		uint32_t read_registers [ 4 ] = { };
		bool whole_loop = false;
		const variant* rules = nullptr;

	};

	bool emit_program ( const uint8_t* code, const program_shape& shape, uint8_t* out, size_t room );

}
