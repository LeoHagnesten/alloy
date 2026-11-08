#include "pool/presets.hh"

namespace pool {

	static const preset known [ ] = {

		{ chain::ALGORITHM_SHA256D, L"Solo CKPool", L"stratum+tcp://stratum.ckpool.org:3333", L"", L"Solo mining: you only get paid if this PC finds a whole block. Put your Bitcoin address in Worker." },
		{ chain::ALGORITHM_SCRYPT, L"litecoinpool.org", L"stratum+ssl://eu.litecoinpool.org:3443", L"stratum+tcp://us.litecoinpool.org:3333", L"Needs an account there. Worker is your username, a dot, then a name for this PC." },
		{ chain::ALGORITHM_RANDOMX, L"SupportXMR", L"stratum+ssl://pool.supportxmr.com:443", L"stratum+tcp://pool.supportxmr.com:3333", L"No account needed. Put your Monero address in Worker." },

	};

	int32_t preset_count ( ) {

		return base::count_of ( known );

	}

	const preset& preset_at ( int32_t index ) {

		return known [ index ];

	}

	int32_t preset_for ( uint8_t algorithm ) {

		for ( int32_t i = 0; i < preset_count ( ); i++ ) {

			if ( known [ i ].algorithm == algorithm )
				return i;

		}

		return -1;

	}

}
