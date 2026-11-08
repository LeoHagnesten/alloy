#pragma once

#include "base/base.hh"

namespace hash {

	struct heavy_matrix {

		uint8_t cells [ 64 ] [ 64 ] = { };

	};

	struct xoshiro256 {

		uint64_t state [ 4 ] = { };

		uint64_t next ( );

	};

	void cshake ( uint32_t rate, const char* name, const char* customization, const void* data, size_t size, uint8_t* out, size_t out_size );

	void cshake_start ( uint32_t rate, const char* name, const char* customization, uint64_t* state );

	int32_t heavy_rank ( const heavy_matrix& value );

	void heavy_matrix_from ( const uint8_t* seed, heavy_matrix& out );

	void heavy_pack ( const heavy_matrix& value, uint32_t* out );

	void heavy_start_states ( uint64_t* pow, uint64_t* finish );

	class heavy_hasher {

	public:

		void prepare ( const uint8_t* pre_pow, uint64_t timestamp );

		void hash ( uint64_t nonce, uint8_t* out ) const;

		const uint64_t* pow_state ( ) const {

			return pow;

		}

		const uint64_t* finish_state ( ) const {

			return finish;

		}

		const heavy_matrix& matrix ( ) const {

			return grid;

		}

	private:

		uint8_t seed [ 32 ] = { };
		bool seeded = false;
		heavy_matrix grid;
		uint64_t pow [ 25 ] = { };
		uint64_t pow_start [ 25 ] = { };
		uint64_t finish [ 25 ] = { };
		bool started = false;

	};

}
