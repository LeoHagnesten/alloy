#ifdef ALLOY_DEBUG

#include "tests/harness.hh"
#include "chain/address.hh"
#include "hash/keccak.hh"
#include "base/strings.hh"
#include "base/text.hh"

namespace tests {

	struct address_case {

		uint8_t algorithm;
		const char* text;
		chain::address_verdict verdict;
		const wchar_t* coin;

	};

	static const address_case address_cases [ ] = {

		{ chain::ALGORITHM_SHA256D, "1A1zP1eP5QGefi2DMPTfTL5SLmv7DivfNa", chain::ADDRESS_VALID, L"Bitcoin" },
		{ chain::ALGORITHM_SHA256D, "bc1qw508d6qejxtdg4y5r3zarvary0c5xw7kv8f3t4", chain::ADDRESS_VALID, L"Bitcoin" },
		{ chain::ALGORITHM_SHA256D, "bc1p0xlxvlhemja6c4dqv22uapctqupfhlxm9h8z3k2e72q4k9hcz7vqzk5jj0", chain::ADDRESS_VALID, L"Bitcoin" },
		{ chain::ALGORITHM_SHA256D, "bitcoincash:qpm2qsznhks23z7629mms6s4cwef74vcwvy22gdx6a", chain::ADDRESS_VALID, L"Bitcoin Cash" },
		{ chain::ALGORITHM_SHA256D, "1A1zP1eP5QGefi2DMPTfTL5SLmv7DivfNb", chain::ADDRESS_BROKEN, L"" },
		{ chain::ALGORITHM_SHA256D, "bc1qw508d6qejxtdg4y5r3zarvary0c5xw7kv8f3t5", chain::ADDRESS_BROKEN, L"" },
		{ chain::ALGORITHM_SCRYPT, "LKDyUEtTR1HXamkiEphisSiBJu6o3ZPE34", chain::ADDRESS_VALID, L"Litecoin" },
		{ chain::ALGORITHM_SCRYPT, "D6yHKCL7i8EecVeXoPAXAFnysaqTYiWKy4", chain::ADDRESS_VALID, L"Dogecoin" },
		{ chain::ALGORITHM_SCRYPT, "1A1zP1eP5QGefi2DMPTfTL5SLmv7DivfNa", chain::ADDRESS_OTHER_COIN, L"Bitcoin" },
		{ chain::ALGORITHM_RANDOMX, "44AFFq5kSiGBoZ4NMDwYtN18obc8AemS33DBLWs3H7otXft3XjrpDtQGv7SqSsaBYBb98uNbr2VBBEt7f2wfn3RVGQBEP3A", chain::ADDRESS_VALID, L"Monero" },
		{ chain::ALGORITHM_RANDOMX, "44AFFq5kSiGBoZ4NMDwYtN18obc8AemS33DBLWs3H7otXft3XjrpDtQGv7SqSsaBYBb98uNbr2VBBEt7f2wfn3RVGQBEP3B", chain::ADDRESS_BROKEN, L"" },
		{ chain::ALGORITHM_RANDOMX, "bc1qw508d6qejxtdg4y5r3zarvary0c5xw7kv8f3t4", chain::ADDRESS_OTHER_COIN, L"Bitcoin" },
		{ chain::ALGORITHM_KHEAVYHASH, "kaspa:precqv0krj3r6uyyfa36ga7s0u9jct0v4wg8ctsfde2gkrsgwgw8jgxfzfc98", chain::ADDRESS_VALID, L"Kaspa" },
		{ chain::ALGORITHM_KHEAVYHASH, "kaspa:precqv0krj3r6uyyfa36ga7s0u9jct0v4wg8ctsfde2gkrsgwgw8jgxfzfc99", chain::ADDRESS_BROKEN, L"" },
		{ chain::ALGORITHM_RANDOMX, "kaspa:precqv0krj3r6uyyfa36ga7s0u9jct0v4wg8ctsfde2gkrsgwgw8jgxfzfc98", chain::ADDRESS_OTHER_COIN, L"Kaspa" },
		{ chain::ALGORITHM_SHA256D, "alloy", chain::ADDRESS_NONE, L"" },

	};

	void address_tests ( ) {

		section ( L"addresses" );

		uint8_t digest [ 32 ];

		hash::keccak_256 ( "", 0, digest );
		check ( hex_is ( digest, 32, "c5d2460186f7233c927e7db2dcc703c0e500b653ca82273b7bfad8045d85a470" ), L"Keccak-256 of the empty string matches the reference" );

		int32_t right = 0;

		for ( const address_case& item : address_cases ) {

			chain::address_check result = chain::check_address ( item.algorithm, item.text );

			if ( result.verdict == item.verdict && base::same_text ( result.coin, item.coin ) )
				right++;
			else {

				wchar_t wide [ 128 ];
				base::text note;

				base::widen ( wide, 128, item.text );
				note.add ( L"unexpected verdict " ).number ( result.verdict ).add ( L" for " ).add ( wide );
				check ( false, note.get ( ) );

			}

		}

		base::text note;

		note.add ( L"Bitcoin, segwit, taproot, Bitcoin Cash, Litecoin, Dogecoin, Monero and Kaspa addresses check out (" ).number ( static_cast < uint64_t > ( right ) ).add ( L" of " ).number ( static_cast < uint64_t > ( base::count_of ( address_cases ) ) ).put ( L')' );
		check ( right == base::count_of ( address_cases ), note.get ( ) );

		chain::address_check worker = chain::check_worker ( chain::ALGORITHM_RANDOMX, L"44AFFq5kSiGBoZ4NMDwYtN18obc8AemS33DBLWs3H7otXft3XjrpDtQGv7SqSsaBYBb98uNbr2VBBEt7f2wfn3RVGQBEP3A.rig" );
		chain::address_check name = chain::check_worker ( chain::ALGORITHM_SHA256D, L"alloy.worker" );

		check ( worker.verdict == chain::ADDRESS_VALID && name.verdict == chain::ADDRESS_NONE, L"a worker name after the address is ignored, and plain usernames aren't flagged" );

	}

}

#endif
