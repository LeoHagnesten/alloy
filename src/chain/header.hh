#pragma once

#include "base/base.hh"

namespace chain {

	static constexpr uint32_t header_size = 80;

	struct header {

		uint8_t bytes [ header_size ] = { };

		uint32_t field ( uint32_t offset ) const;

		void set_field ( uint32_t offset, uint32_t value );

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

		void set_nonce ( uint32_t value ) {

			set_field ( 76, value );

		}

	};

	class header_hasher {

	public:

		void prepare ( const header& source );

		void hash ( uint32_t nonce, uint8_t* out ) const;

	private:

		uint32_t midstate [ 8 ] = { };
		uint8_t second [ 64 ] = { };

	};

	void genesis ( header& out );

	void target_of ( uint32_t bits, uint8_t* out );

	bool meets ( const uint8_t* digest, uint32_t bits );

	uint32_t leading_zero_bits ( const uint8_t* digest );

}