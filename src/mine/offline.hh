#pragma once

#include "mine/engine.hh"

namespace mine {

	class benchmark_work : public feeder {

	public:

		void set_algorithm ( uint8_t value ) {

			kind = value;

		}

		void set_variant ( uint8_t value ) {

			variant = value;

		}

		bool fill ( uint64_t index, unit& out ) override;

	private:

		uint8_t kind = chain::ALGORITHM_SHA256D;
		uint8_t variant = 0;

	};

	class genesis_work : public feeder {

	public:

		void reset ( uint32_t from, uint8_t algorithm = chain::ALGORITHM_SHA256D );

		uint8_t algorithm ( ) const {

			return kind;

		}

		bool fill ( uint64_t index, unit& out ) override;

		bool found ( const unit& from, uint32_t nonce, const uint8_t* digest ) override;

		bool solved ( ) const {

			return base::atomic_load ( finished ) == 1;

		}

		uint32_t nonce ( ) const {

			return winner;

		}

		const uint8_t* digest ( ) const {

			return block_hash;

		}

		uint32_t start ( ) const {

			return first;

		}

	private:

		volatile uint32_t finished = 0;
		uint8_t kind = chain::ALGORITHM_SHA256D;
		uint32_t winner = 0;
		uint32_t first = 0;
		uint8_t block_hash [ 32 ] = { };

	};

}
