#pragma once

#include "hash/sha256.hh"

namespace chain {

	static constexpr uint32_t header_size = 80;

	struct header {

		uint8_t bytes [ header_size ] = { };

		uint32_t field ( uint32_t offset ) const;

		void set_field ( uint32_t offset, uint32_t value );

		uint8_t* previous ( ) {

			return bytes + 4;

		}

		uint8_t* merkle_root ( ) {

			return bytes + 36;

		}

		uint32_t version ( ) const {

			return field ( 0 );

		}

		uint32_t time ( ) const {

			return field ( 68 );

		}

		uint32_t bits ( ) const {

			return field ( 72 );

		}

		uint32_t nonce ( ) const {

			return field ( 76 );

		}

		void set_version ( uint32_t value ) {

			set_field ( 0, value );

		}

		void set_time ( uint32_t value ) {

			set_field ( 68, value );

		}

		void set_bits ( uint32_t value ) {

			set_field ( 72, value );

		}

		void set_nonce ( uint32_t value ) {

			set_field ( 76, value );

		}

	};

	class header_hasher {

	public:

		void prepare ( const header& source, hash::compressor kernel );

		void hash ( uint32_t nonce, uint8_t* out ) const;

	private:

		hash::compressor compress = hash::sha256_block;
		uint32_t midstate [ 8 ] = { };
		uint8_t second [ 64 ] = { };

	};

	void genesis ( header& out );

}
