#pragma once

#include "randomx/dataset.hh"
#include "randomx/jit.hh"
#include "base/memory.hh"

namespace randomx {

	class machine {

	public:

		machine ( ) = default;

		machine ( const machine& ) = delete;

		machine& operator = ( const machine& ) = delete;

		~machine ( );

		bool open ( const cache* light, const dataset* fast, uint8_t kind = VARIANT_RX0 );

		void hash ( const void* input, size_t size, uint8_t* out );

		void hash_core ( const void* input, size_t size, uint8_t* out );

		bool compiled ( ) const {

			return code.run != nullptr && !interpret;

		}

		uint8_t kind ( ) const {

			return rules->id;

		}

		bool interpret = false;

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

		bool build ( const uint8_t* bytes );

		void execute ( bool native );

		void run ( );

		void read_dataset ( uint64_t address );

		void mix_registers ( );

		uint8_t* scratchpad = nullptr;
		base::code_view code;
		const cache* light_source = nullptr;
		const dataset* fast_source = nullptr;
		const variant* rules = &variant_of ( VARIANT_RX0 );
		alignas ( 16 ) uint8_t program [ 128 + 8 * program_room ];
		step steps [ program_room ];
		registers state;
		uint32_t ma = 0;
		uint32_t mx = 0;
		uint32_t read_registers [ 4 ] = { };
		uint64_t dataset_offset = 0;

	};

}
