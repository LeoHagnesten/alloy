#pragma once

#include "base/base.hh"

namespace hash {

	static constexpr uint32_t sha256_size = 32;

	extern const uint32_t sha256_start [ 8 ];

	void sha256_block ( uint32_t* state, const uint8_t* block );

	void sha256_digest ( const uint32_t* state, uint8_t* out );

	class sha256 {

	public:

		sha256 ( ) {

			begin ( );

		}

		void begin ( );

		void add ( const void* data, size_t size );

		void end ( uint8_t* out );

	private:

		uint32_t state [ 8 ] = { };
		uint8_t waiting [ 64 ] = { };
		uint32_t waiting_size = 0;
		uint64_t total = 0;

	};

	void sha256_once ( const void* data, size_t size, uint8_t* out );

	void sha256_twice ( const void* data, size_t size, uint8_t* out );

}