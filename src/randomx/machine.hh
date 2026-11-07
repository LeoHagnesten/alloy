#pragma once

#include "randomx/dataset.hh"
#include <emmintrin.h>

namespace randomx {

	static constexpr int32_t program_size = 256;
	static constexpr int32_t program_iterations = 2048;
	static constexpr int32_t program_count = 8;
	static constexpr uint32_t scratchpad_bytes = 2097152;

	class machine {

	public:

		machine ( ) = default;

		machine ( const machine& ) = delete;

		machine& operator = ( const machine& ) = delete;

		~machine ( );

		bool open ( const cache* light, const dataset* fast );

		void hash ( const void* input, size_t size, uint8_t* out );

	private:

		struct step {

			uint8_t kind = 0;
			uint8_t shift = 0;
			int16_t target = -1;
			uint32_t mask = 0;
			uint64_t imm = 0;
			uint64_t* idst = nullptr;
			const uint64_t* isrc = nullptr;
			__m128d* fdst = nullptr;
			const __m128d* fsrc = nullptr;

		};

		void configure ( const uint8_t* entropy );

		void compile ( const uint8_t* code );

		void execute ( );

		void run ( );

		void read_dataset ( uint64_t address );

		uint8_t* scratchpad = nullptr;
		const cache* light_source = nullptr;
		const dataset* fast_source = nullptr;
		alignas ( 16 ) uint8_t program [ 128 + 8 * program_size ];
		step steps [ program_size ];
		uint64_t r [ 8 ] = { };
		__m128d f [ 4 ];
		__m128d e [ 4 ];
		__m128d a [ 4 ];
		__m128d exponent_mask;
		uint32_t ma = 0;
		uint32_t mx = 0;
		uint32_t read_registers [ 4 ] = { };
		uint64_t dataset_offset = 0;

	};

}
