#include "mine/offline.hh"
#include "chain/target.hh"
#include "base/bits.hh"
#include "base/memory.hh"
#include "base/strings.hh"

namespace mine {

	bool benchmark_work::fill ( uint64_t index, unit& out ) {

		static const char blob [ ] = "0b0b98bea7e805e0010a2126d287a2a0cc833d312cb786385a7c2f9de69d25537f584a9bc9977b00000000666fd8753bf61a8631f12984e3fd44f4014eca629276817b56f32e9b68bd82f416";

		out.algorithm = kind;
		base::zero ( out.target, 32 );
		out.index = index;
		out.job = 0;
		out.first = 0;
		out.end = nonce_space;

		if ( kind == chain::ALGORITHM_RANDOMX ) {

			out.blob_size = sizeof ( blob ) / 2;
			out.nonce_at = 39;
			out.variant = variant;
			base::read_hex ( blob, out.blob, out.blob_size );
			base::store_little ( out.blob + 43, static_cast < uint32_t > ( index ) );
			hash::sha256_twice ( "alloy benchmark seed", 20, out.seed );

			return true;

		}

		if ( kind == chain::ALGORITHM_SCRYPT )
			chain::litecoin_genesis ( out.header );
		else
			chain::genesis ( out.header );

		out.header.set_time ( out.header.time ( ) + static_cast < uint32_t > ( index ) );

		return true;

	}

	void genesis_work::reset ( uint32_t from, uint8_t algorithm ) {

		base::atomic_store ( finished, 0u );
		kind = algorithm == chain::ALGORITHM_SCRYPT ? algorithm : static_cast < uint8_t > ( chain::ALGORITHM_SHA256D );
		winner = 0;
		first = from;
		base::zero ( block_hash, 32 );

	}

	bool genesis_work::fill ( uint64_t index, unit& out ) {

		if ( index || solved ( ) )
			return false;

		out.algorithm = kind;

		if ( kind == chain::ALGORITHM_SCRYPT )
			chain::litecoin_genesis ( out.header );
		else
			chain::genesis ( out.header );

		chain::target_of ( out.header.bits ( ), out.target );
		out.index = 0;
		out.job = 0;
		out.first = first;
		out.end = nonce_space;

		return true;

	}

	bool genesis_work::found ( const unit& from, uint32_t nonce, const uint8_t* digest ) {

		if ( base::atomic_replace ( finished, 0u, 2u ) ) {

			winner = nonce;
			base::copy ( block_hash, digest, 32 );
			base::atomic_store ( finished, 1u );

		}

		return false;

	}

}
