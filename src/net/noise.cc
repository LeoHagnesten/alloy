#include "net/noise.hh"
#include "net/socket.hh"
#include "chain/secp256k1.hh"
#include "hash/chacha.hh"
#include "hash/scrypt.hh"
#include "hash/sha256.hh"
#include "base/system/clock.hh"
#include "base/bits.hh"
#include "base/memory.hh"
#include "base/strings.hh"

namespace net {

	static constexpr int32_t chunk_plain = 65519;
	static const char protocol_name [ ] = "Noise_NX_Secp256k1+EllSwift_ChaChaPoly_SHA256";

	static void nonce_bytes ( uint64_t value, uint8_t* out ) {

		base::zero ( out, 4 );
		base::store_little64 ( out + 4, value );

	}

	void noise_cipher::seal ( const uint8_t* ad, size_t ad_size, const uint8_t* plain, size_t size, uint8_t* out ) {

		uint8_t nonce_value [ 12 ];

		nonce_bytes ( nonce++, nonce_value );
		hash::aead_seal ( key, nonce_value, ad, ad_size, plain, size, out );

	}

	bool noise_cipher::open ( const uint8_t* ad, size_t ad_size, const uint8_t* sealed, size_t size, uint8_t* out ) {

		uint8_t nonce_value [ 12 ];

		nonce_bytes ( nonce, nonce_value );

		if ( !hash::aead_open ( key, nonce_value, ad, ad_size, sealed, size, out ) )
			return false;

		nonce++;

		return true;

	}

	void certificate_message ( const certificate& value, const uint8_t* server_x, uint8_t* out ) {

		uint8_t fields [ 42 ];

		fields [ 0 ] = static_cast < uint8_t > ( value.version );
		fields [ 1 ] = static_cast < uint8_t > ( value.version >> 8 );
		base::store_little ( fields + 2, value.valid_from );
		base::store_little ( fields + 6, value.valid_until );
		base::copy ( fields + 10, server_x, 32 );
		hash::sha256_once ( fields, sizeof ( fields ), out );

	}

	static void hmac ( const uint8_t* key, const uint8_t* data, size_t size, uint8_t* out ) {

		hash::hmac_sha256 keyed ( key, 32 );

		keyed.add ( data, size );
		keyed.end ( out );

	}

	static void derive ( const uint8_t* chain_key, const uint8_t* input, size_t size, uint8_t* first, uint8_t* second ) {

		uint8_t temporary [ 32 ];
		uint8_t joined [ 33 ];
		uint8_t one = 1;

		hmac ( chain_key, input, size, temporary );
		hmac ( temporary, &one, 1, first );
		base::copy ( joined, first, 32 );
		joined [ 32 ] = 2;
		hmac ( temporary, joined, 33, second );
		base::zero ( temporary, sizeof ( temporary ) );

	}

	noise_handshake::noise_handshake ( ) {

		hash::sha256_once ( protocol_name, static_cast < size_t > ( base::length ( protocol_name ) ), h );
		base::copy ( ck, h, 32 );
		hash::sha256_once ( h, 32, h );

	}

	noise_handshake::~noise_handshake ( ) {

		base::zero ( ephemeral, sizeof ( ephemeral ) );
		base::zero ( ck, sizeof ( ck ) );
		base::zero ( state.key, sizeof ( state.key ) );

	}

	void noise_handshake::mix_hash ( const uint8_t* data, size_t size ) {

		hash::sha256 work;

		work.add ( h, 32 );

		if ( size )
			work.add ( data, size );

		work.end ( h );

	}

	void noise_handshake::mix_key ( const uint8_t* input ) {

		uint8_t next [ 32 ];

		derive ( ck, input, 32, next, state.key );
		base::copy ( ck, next, 32 );
		state.nonce = 0;
		state.keyed = true;

	}

	void noise_handshake::encrypt_and_hash ( const uint8_t* plain, size_t size, uint8_t* out ) {

		if ( state.keyed ) {

			state.seal ( h, 32, plain, size, out );
			mix_hash ( out, size + 16 );

		} else {

			base::copy ( out, plain, size );
			mix_hash ( out, size );

		}

	}

	bool noise_handshake::decrypt_and_hash ( const uint8_t* sealed, size_t size, uint8_t* out ) {

		if ( !state.keyed ) {

			base::copy ( out, sealed, size );
			mix_hash ( sealed, size );

			return true;

		}

		if ( !state.open ( h, 32, sealed, size, out ) )
			return false;

		mix_hash ( sealed, size );

		return true;

	}

	void noise_handshake::split ( noise_cipher& first, noise_cipher& second ) {

		derive ( ck, nullptr, 0, first.key, second.key );
		first.nonce = 0;
		second.nonce = 0;
		first.keyed = true;
		second.keyed = true;

	}

	bool noise_handshake::first ( uint8_t* out ) {

		chain::random_secret ( ephemeral );

		if ( !chain::ellswift_create ( ephemeral, ephemeral_public ) )
			return false;

		base::copy ( out, ephemeral_public, 64 );
		mix_hash ( ephemeral_public, 64 );
		mix_hash ( nullptr, 0 );

		return true;

	}

	bool noise_handshake::second ( const uint8_t* in, uint8_t* server_x, certificate& signed_by, noise_cipher& send, noise_cipher& receive ) {

		uint8_t shared [ 32 ];
		uint8_t server [ 64 ];
		uint8_t signed_bytes [ certificate_size ];

		mix_hash ( in, 64 );

		if ( !chain::ellswift_xdh ( in, ephemeral_public, ephemeral, true, shared ) )
			return false;

		mix_key ( shared );

		if ( !decrypt_and_hash ( in + 64, 80, server ) || !chain::ellswift_decode ( server, server_x ) )
			return false;

		if ( !chain::ellswift_xdh ( server, ephemeral_public, ephemeral, true, shared ) )
			return false;

		mix_key ( shared );

		if ( !decrypt_and_hash ( in + 144, certificate_size + 16, signed_bytes ) )
			return false;

		signed_by.version = static_cast < uint16_t > ( signed_bytes [ 0 ] | signed_bytes [ 1 ] << 8 );
		signed_by.valid_from = base::load_little ( signed_bytes + 2 );
		signed_by.valid_until = base::load_little ( signed_bytes + 6 );
		base::copy ( signed_by.signature, signed_bytes + 10, 64 );
		split ( send, receive );
		base::zero ( shared, sizeof ( shared ) );

		return true;

	}

	bool noise_handshake::answer ( const uint8_t* in, const uint8_t* static_secret, const certificate& signed_by, uint8_t* out, noise_cipher& send, noise_cipher& receive ) {

		uint8_t shared [ 32 ];
		uint8_t static_public [ 64 ];
		uint8_t signed_bytes [ certificate_size ];

		mix_hash ( in, 64 );
		mix_hash ( nullptr, 0 );
		chain::random_secret ( ephemeral );

		if ( !chain::ellswift_create ( ephemeral, ephemeral_public ) || !chain::ellswift_create ( static_secret, static_public ) )
			return false;

		base::copy ( out, ephemeral_public, 64 );
		mix_hash ( ephemeral_public, 64 );

		if ( !chain::ellswift_xdh ( in, ephemeral_public, ephemeral, false, shared ) )
			return false;

		mix_key ( shared );
		encrypt_and_hash ( static_public, 64, out + 64 );

		if ( !chain::ellswift_xdh ( in, static_public, static_secret, false, shared ) )
			return false;

		mix_key ( shared );
		signed_bytes [ 0 ] = static_cast < uint8_t > ( signed_by.version );
		signed_bytes [ 1 ] = static_cast < uint8_t > ( signed_by.version >> 8 );
		base::store_little ( signed_bytes + 2, signed_by.valid_from );
		base::store_little ( signed_bytes + 6, signed_by.valid_until );
		base::copy ( signed_bytes + 10, signed_by.signature, 64 );
		encrypt_and_hash ( signed_bytes, certificate_size, out + 144 );
		split ( receive, send );
		base::zero ( shared, sizeof ( shared ) );

		return true;

	}

	bool noise_link::read_exact ( uint8_t* out, int32_t size, uint32_t first_wait ) {

		int32_t have = 0;
		double began = base::seconds ( );

		while ( have < size ) {

			int32_t got = wire.read ( reinterpret_cast < char* > ( out + have ), size - have, have ? 100 : first_wait );

			if ( got < 0 )
				return false;

			if ( !got && ( !have || base::seconds ( ) - began > 10.0 ) )
				return false;

			have += got;

		}

		return true;

	}

	bool noise_link::send_frame ( uint16_t extension, uint8_t type, const uint8_t* payload, int32_t size ) {

		uint8_t head [ 6 ];
		int32_t chunks = size ? ( size + chunk_plain - 1 ) / chunk_plain : 0;
		int32_t total = 22 + size + chunks * 16;
		uint8_t* frame = new uint8_t [ total ];
		int32_t at = 22;

		head [ 0 ] = static_cast < uint8_t > ( extension );
		head [ 1 ] = static_cast < uint8_t > ( extension >> 8 );
		head [ 2 ] = type;
		head [ 3 ] = static_cast < uint8_t > ( size );
		head [ 4 ] = static_cast < uint8_t > ( size >> 8 );
		head [ 5 ] = static_cast < uint8_t > ( size >> 16 );
		send.seal ( nullptr, 0, head, 6, frame );

		for ( int32_t done = 0; done < size; done += chunk_plain ) {

			int32_t take = base::min ( size - done, chunk_plain );

			send.seal ( nullptr, 0, payload + done, static_cast < size_t > ( take ), frame + at );
			at += take + 16;

		}

		bool sent = wire.send ( reinterpret_cast < const char* > ( frame ), total );

		delete [ ] frame;

		return sent;

	}

	int32_t noise_link::read_frame ( uint16_t& extension, uint8_t& type, base::list < uint8_t >& payload, uint32_t wait ) {

		uint8_t sealed_head [ 22 ];
		uint8_t head [ 6 ];
		int32_t got = wire.read ( reinterpret_cast < char* > ( sealed_head ), 22, wait );

		if ( got < 0 )
			return -1;

		if ( !got )
			return 0;

		if ( got < 22 && !read_exact ( sealed_head + got, 22 - got, 5000 ) )
			return -1;

		if ( !receive.open ( nullptr, 0, sealed_head, 22, head ) )
			return -1;

		extension = static_cast < uint16_t > ( head [ 0 ] | head [ 1 ] << 8 );
		type = head [ 2 ];

		int32_t size = head [ 3 ] | head [ 4 ] << 8 | head [ 5 ] << 16;
		int32_t chunks = size ? ( size + chunk_plain - 1 ) / chunk_plain : 0;
		int32_t sealed_size = size + chunks * 16;
		uint8_t* sealed = new uint8_t [ sealed_size + 1 ];
		bool good = !sealed_size || read_exact ( sealed, sealed_size, 5000 );

		payload.set_size ( size );

		for ( int32_t done = 0, at = 0; good && done < size; done += chunk_plain ) {

			int32_t take = base::min ( size - done, chunk_plain );

			good = receive.open ( nullptr, 0, sealed + at, static_cast < size_t > ( take + 16 ), payload.data ( ) + done );
			at += take + 16;

		}

		delete [ ] sealed;

		return good ? 1 : -1;

	}

}
