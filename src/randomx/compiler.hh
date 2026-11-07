#pragma once

#include "randomx/superscalar.hh"

namespace randomx {

	using native_program = void ( * ) ( uint64_t* registers );

	class scalar_compiler {

	public:

		scalar_compiler ( ) = default;

		scalar_compiler ( const scalar_compiler& ) = delete;

		scalar_compiler& operator = ( const scalar_compiler& ) = delete;

		~scalar_compiler ( );

		bool build ( const scalar_program* programs, int32_t count );

		void release ( );

		native_program program ( int32_t index ) const {

			return entries [ index ];

		}

		bool ready ( ) const {

			return code != nullptr;

		}

	private:

		uint8_t* code = nullptr;
		size_t code_size = 0;
		native_program entries [ 8 ] = { };

	};

}
