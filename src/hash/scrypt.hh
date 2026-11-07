#pragma once

#include "hash/sha256.hh"

namespace hash {

	static constexpr uint32_t scrypt_cost = 1024;
	static constexpr size_t scrypt_scratch_bytes = scrypt_cost * 128 + 512;
	static constexpr size_t scrypt_pair_scratch_bytes = scrypt_cost * 256 + 512;

	class hmac_sha256 {

	public:

		hmac_sha256 ( const void* key, size_t size );

		void add ( const void* data, size_t size );

		void end ( uint8_t* out );

	private:

		sha256 inner;
		sha256 outer;

	};

	void pbkdf2_sha256 ( const hmac_sha256& keyed, const void* salt, size_t salt_size, uint32_t rounds, uint8_t* out, size_t size );

	void pbkdf2_sha256 ( const void* password, size_t password_size, const void* salt, size_t salt_size, uint32_t rounds, uint8_t* out, size_t size );

	void salsa20_8 ( uint32_t* block );

	bool scrypt ( const void* password, size_t password_size, const void* salt, size_t salt_size, uint32_t cost, uint32_t block_size, uint32_t parallel, uint8_t* out, size_t size );

	void scrypt_header ( const uint8_t* header, uint8_t* out, void* scratch );

	void scrypt_header_pair ( const uint8_t* first, const uint8_t* second, uint8_t* first_out, uint8_t* second_out, void* scratch );

}
