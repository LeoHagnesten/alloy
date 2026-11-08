#pragma once

#include "base/containers/list.hh"

namespace net {

	class connection;

	static constexpr int32_t noise_first_size = 64;
	static constexpr int32_t noise_second_size = 234;
	static constexpr int32_t certificate_size = 74;

	struct noise_cipher {

		uint8_t key [ 32 ] = { };
		uint64_t nonce = 0;
		bool keyed = false;

		void seal ( const uint8_t* ad, size_t ad_size, const uint8_t* plain, size_t size, uint8_t* out );

		bool open ( const uint8_t* ad, size_t ad_size, const uint8_t* sealed, size_t size, uint8_t* out );

	};

	struct certificate {

		uint16_t version = 0;
		uint32_t valid_from = 0;
		uint32_t valid_until = 0;
		uint8_t signature [ 64 ] = { };

	};

	void certificate_message ( const certificate& value, const uint8_t* server_x, uint8_t* out );

	class noise_handshake {

	public:

		noise_handshake ( );

		~noise_handshake ( );

		bool first ( uint8_t* out );

		bool second ( const uint8_t* in, uint8_t* server_x, certificate& signed_by, noise_cipher& send, noise_cipher& receive );

		bool answer ( const uint8_t* in, const uint8_t* static_secret, const certificate& signed_by, uint8_t* out, noise_cipher& send, noise_cipher& receive );

	private:

		void mix_hash ( const uint8_t* data, size_t size );

		void mix_key ( const uint8_t* input );

		void encrypt_and_hash ( const uint8_t* plain, size_t size, uint8_t* out );

		bool decrypt_and_hash ( const uint8_t* sealed, size_t size, uint8_t* out );

		void split ( noise_cipher& first, noise_cipher& second );

		uint8_t h [ 32 ] = { };
		uint8_t ck [ 32 ] = { };
		noise_cipher state;
		uint8_t ephemeral [ 32 ] = { };
		uint8_t ephemeral_public [ 64 ] = { };

	};

	class noise_link {

	public:

		explicit noise_link ( connection& wire ) : wire ( wire ) { }

		bool read_exact ( uint8_t* out, int32_t size, uint32_t first_wait );

		bool send_frame ( uint16_t extension, uint8_t type, const uint8_t* payload, int32_t size );

		int32_t read_frame ( uint16_t& extension, uint8_t& type, base::list < uint8_t >& payload, uint32_t wait );

		noise_cipher send;
		noise_cipher receive;

	private:

		connection& wire;

	};

}
