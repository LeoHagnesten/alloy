#pragma once

#include "base/base.hh"

namespace hash {

	class blake2b {

	public:

		explicit blake2b ( uint32_t output_size = 64 );

		void add ( const void* data, size_t size );

		void end ( void* out );

	private:

		void compress ( const uint8_t* block, bool last );

		uint64_t state [ 8 ] = { };
		uint64_t counter = 0;
		uint8_t waiting [ 128 ] = { };
		uint32_t waiting_size = 0;
		uint32_t size = 64;

	};

	void blake2b_once ( void* out, uint32_t output_size, const void* data, size_t size );

	void blake2b_long ( void* out, uint32_t output_size, const void* data, size_t size );

}
