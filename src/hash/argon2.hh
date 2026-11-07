#pragma once

#include "base/base.hh"

namespace hash {

	struct argon2_block {

		uint64_t v [ 128 ];

	};

	void argon2d_fill ( argon2_block* memory, uint32_t blocks, uint32_t passes, const void* password, size_t password_size, const void* salt, size_t salt_size );

}
