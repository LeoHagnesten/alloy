#pragma once

#include "base/base.hh"

namespace hash {

	void keccak_permute ( uint64_t* state );

	void keccak_256 ( const void* data, size_t size, uint8_t* out );

	void keccak_512 ( const void* data, size_t size, uint8_t* out );

}
