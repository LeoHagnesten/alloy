#pragma once

#include "base/base.hh"

namespace hash {

	struct argon2_block {

		uint64_t v [ 128 ];

	};

	struct argon2_input {

		const void* password = nullptr;
		size_t password_size = 0;
		const void* salt = nullptr;
		size_t salt_size = 0;
		const void* secret = nullptr;
		size_t secret_size = 0;
		const void* data = nullptr;
		size_t data_size = 0;
		uint32_t tag_size = 0;

	};

	void argon2d_fill ( argon2_block* memory, uint32_t blocks, uint32_t passes, uint32_t lanes, const argon2_input& input );

	void argon2d_fill ( argon2_block* memory, uint32_t blocks, uint32_t passes, const void* password, size_t password_size, const void* salt, size_t salt_size );

	void argon2d_tag ( const argon2_block* memory, uint32_t blocks, uint32_t lanes, uint32_t tag_size, uint8_t* out );

}
