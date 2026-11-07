#include "mine/offline.hh"
#include "chain/target.hh"
#include "base/memory.hh"

namespace mine {

	bool benchmark_work::fill ( uint64_t index, unit& out ) {

		chain::genesis ( out.header );
		out.header.set_time ( out.header.time ( ) + static_cast < uint32_t > ( index ) );
		base::zero ( out.target, 32 );
		out.index = index;
		out.job = 0;
		out.first = 0;
		out.end = nonce_space;

		return true;

	}

	void genesis_work::reset ( uint32_t from ) {

		base::atomic_store ( finished, 0u );
		winner = 0;
		first = from;
		base::zero ( block_hash, 32 );

	}

	bool genesis_work::fill ( uint64_t index, unit& out ) {

		if ( index || solved ( ) )
			return false;

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
