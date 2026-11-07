#pragma once

#include "base/base.hh"

namespace hash {

	void aes_prepare ( bool hardware );

	bool aes_hardware ( );

	void aes_encrypt_round ( uint8_t* block, const uint8_t* key );

	void aes_decrypt_round ( uint8_t* block, const uint8_t* key );

	void aes_fill_1r ( uint8_t* state, uint8_t* out, size_t size );

	void aes_fill_4r ( const uint8_t* state, uint8_t* out, size_t size );

	void aes_hash_1r ( const uint8_t* input, size_t size, uint8_t* out );

}
