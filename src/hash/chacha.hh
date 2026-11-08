#pragma once

#include "base/base.hh"

namespace hash {

	void chacha20_block ( const uint8_t* key, uint32_t counter, const uint8_t* nonce, uint8_t* out );

	void chacha20_xor ( const uint8_t* key, uint32_t counter, const uint8_t* nonce, const uint8_t* in, uint8_t* out, size_t size );

	void poly1305 ( const uint8_t* key, const uint8_t* data, size_t size, uint8_t* tag );

	void aead_seal ( const uint8_t* key, const uint8_t* nonce, const uint8_t* aad, size_t aad_size, const uint8_t* plain, size_t size, uint8_t* out );

	bool aead_open ( const uint8_t* key, const uint8_t* nonce, const uint8_t* aad, size_t aad_size, const uint8_t* sealed, size_t size, uint8_t* out );

	void tagged_hash ( const char* tag, const void* data, size_t size, uint8_t* out );

}
