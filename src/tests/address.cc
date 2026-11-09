#ifdef ALLOY_DEBUG

#include "tests/harness.hh"
#include "chain/address.hh"
#include "base/memory.hh"
#include "hash/keccak.hh"
#include "base/strings.hh"
#include "base/text.hh"

namespace tests {

	struct address_case {

		uint8_t chosen;
		const char* text;
		chain::address_verdict verdict;
		const wchar_t* coin;

	};

	static const address_case address_cases [ ] = {

		{ chain::COIN_BITCOIN, "1A1zP1eP5QGefi2DMPTfTL5SLmv7DivfNa", chain::ADDRESS_VALID, L"Bitcoin" },
		{ chain::COIN_BITCOIN, "bc1qw508d6qejxtdg4y5r3zarvary0c5xw7kv8f3t4", chain::ADDRESS_VALID, L"Bitcoin" },
		{ chain::COIN_BITCOIN, "bc1p0xlxvlhemja6c4dqv22uapctqupfhlxm9h8z3k2e72q4k9hcz7vqzk5jj0", chain::ADDRESS_VALID, L"Bitcoin" },
		{ chain::COIN_BITCOIN_CASH, "bitcoincash:qpm2qsznhks23z7629mms6s4cwef74vcwvy22gdx6a", chain::ADDRESS_VALID, L"Bitcoin Cash" },
		{ chain::COIN_BITCOIN_CASH, "1BpEi6DfDAUFd7GtittLSdBeYJvcoaVggu", chain::ADDRESS_VALID, L"Bitcoin Cash" },
		{ chain::COIN_BITCOIN_SV, "1A1zP1eP5QGefi2DMPTfTL5SLmv7DivfNa", chain::ADDRESS_VALID, L"Bitcoin SV" },
		{ chain::COIN_BITCOIN, "bitcoincash:qpm2qsznhks23z7629mms6s4cwef74vcwvy22gdx6a", chain::ADDRESS_OTHER_COIN, L"Bitcoin Cash" },
		{ chain::COIN_BITCOIN, "1A1zP1eP5QGefi2DMPTfTL5SLmv7DivfNb", chain::ADDRESS_BROKEN, L"" },
		{ chain::COIN_BITCOIN, "bc1qw508d6qejxtdg4y5r3zarvary0c5xw7kv8f3t5", chain::ADDRESS_BROKEN, L"" },
		{ chain::COIN_LITECOIN, "LKDyUEtTR1HXamkiEphisSiBJu6o3ZPE34", chain::ADDRESS_VALID, L"Litecoin" },
		{ chain::COIN_DOGECOIN, "D6yHKCL7i8EecVeXoPAXAFnysaqTYiWKy4", chain::ADDRESS_VALID, L"Dogecoin" },
		{ chain::COIN_DIGIBYTE, "D6yHKCL7i8EecVeXoPAXAFnysaqTYiWKy4", chain::ADDRESS_VALID, L"DigiByte" },
		{ chain::COIN_LITECOIN, "D6yHKCL7i8EecVeXoPAXAFnysaqTYiWKy4", chain::ADDRESS_OTHER_COIN, L"Dogecoin" },
		{ chain::COIN_LITECOIN, "1A1zP1eP5QGefi2DMPTfTL5SLmv7DivfNa", chain::ADDRESS_OTHER_COIN, L"Bitcoin" },
		{ chain::COIN_MONERO, "44AFFq5kSiGBoZ4NMDwYtN18obc8AemS33DBLWs3H7otXft3XjrpDtQGv7SqSsaBYBb98uNbr2VBBEt7f2wfn3RVGQBEP3A", chain::ADDRESS_VALID, L"Monero" },
		{ chain::COIN_MONERO, "44AFFq5kSiGBoZ4NMDwYtN18obc8AemS33DBLWs3H7otXft3XjrpDtQGv7SqSsaBYBb98uNbr2VBBEt7f2wfn3RVGQBEP3B", chain::ADDRESS_BROKEN, L"" },
		{ chain::COIN_MONERO, "bc1qw508d6qejxtdg4y5r3zarvary0c5xw7kv8f3t4", chain::ADDRESS_OTHER_COIN, L"Bitcoin" },
		{ chain::COIN_KASPA, "kaspa:precqv0krj3r6uyyfa36ga7s0u9jct0v4wg8ctsfde2gkrsgwgw8jgxfzfc98", chain::ADDRESS_VALID, L"Kaspa" },
		{ chain::COIN_KASPA, "kaspa:precqv0krj3r6uyyfa36ga7s0u9jct0v4wg8ctsfde2gkrsgwgw8jgxfzfc99", chain::ADDRESS_BROKEN, L"" },
		{ chain::COIN_MONERO, "kaspa:precqv0krj3r6uyyfa36ga7s0u9jct0v4wg8ctsfde2gkrsgwgw8jgxfzfc98", chain::ADDRESS_OTHER_COIN, L"Kaspa" },
		{ chain::COIN_BITCOIN, "alloy", chain::ADDRESS_NONE, L"" },

	};

	void address_tests ( ) {

		section ( L"addresses" );

		uint8_t digest [ 32 ];

		hash::keccak_256 ( "", 0, digest );
		check ( hex_is ( digest, 32, "c5d2460186f7233c927e7db2dcc703c0e500b653ca82273b7bfad8045d85a470" ), L"Keccak-256 of the empty string matches the reference" );

		int32_t right = 0;

		for ( const address_case& item : address_cases ) {

			chain::address_check result = chain::check_address ( item.chosen, item.text );

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

		note.add ( L"Bitcoin, segwit, taproot, Bitcoin Cash in both forms, Bitcoin SV, Litecoin, Dogecoin, DigiByte, Monero and Kaspa addresses check out against the chosen coin (" ).number ( static_cast < uint64_t > ( right ) ).add ( L" of " ).number ( static_cast < uint64_t > ( base::count_of ( address_cases ) ) ).put ( L')' );
		check ( right == base::count_of ( address_cases ), note.get ( ) );

		chain::address_check worker = chain::check_worker ( chain::COIN_MONERO, L"44AFFq5kSiGBoZ4NMDwYtN18obc8AemS33DBLWs3H7otXft3XjrpDtQGv7SqSsaBYBb98uNbr2VBBEt7f2wfn3RVGQBEP3A.rig" );
		chain::address_check name = chain::check_worker ( chain::COIN_BITCOIN, L"alloy.worker" );

		check ( worker.verdict == chain::ADDRESS_VALID && name.verdict == chain::ADDRESS_NONE, L"a worker name after the address is ignored, and plain usernames aren't flagged" );

		uint8_t legacy [ 32 ];
		int32_t legacy_size = 0;
		char encoded [ 80 ];
		char ecash [ 80 ];
		bool spec = chain::base58_decode ( "1BpEi6DfDAUFd7GtittLSdBeYJvcoaVggu", legacy, sizeof ( legacy ), legacy_size ) && chain::cash_encode ( chain::COIN_BITCOIN_CASH, false, legacy + 1, encoded, sizeof ( encoded ) ) && base::same_text ( encoded, "bitcoincash:qpm2qsznhks23z7629mms6s4cwef74vcwvy22gdx6a" );
		bool moved = chain::cash_encode ( chain::COIN_ECASH, false, legacy + 1, ecash, sizeof ( ecash ) ) && chain::check_address ( chain::COIN_ECASH, ecash ).verdict == chain::ADDRESS_VALID && chain::check_address ( chain::COIN_BITCOIN_CASH, ecash ).verdict == chain::ADDRESS_OTHER_COIN;

		check ( spec && moved, L"CashAddr encodes the spec's example and the same key as an eCash address, which only an eCash setting accepts" );

		uint8_t from_cash [ 64 ];
		uint8_t from_legacy [ 64 ];
		uint8_t from_script [ 64 ];
		int32_t cash_size = 0;
		int32_t legacy_script = 0;
		int32_t hashed_size = 0;
		bool same_key = chain::payout_script ( chain::COIN_BITCOIN_CASH, L"bitcoincash:qpm2qsznhks23z7629mms6s4cwef74vcwvy22gdx6a", from_cash, sizeof ( from_cash ), cash_size ) && chain::payout_script ( chain::COIN_BITCOIN, L"1BpEi6DfDAUFd7GtittLSdBeYJvcoaVggu", from_legacy, sizeof ( from_legacy ), legacy_script ) && cash_size == 25 && cash_size == legacy_script && base::same ( from_cash, from_legacy, 25 );
		bool same_script = chain::payout_script ( chain::COIN_BITCOIN_CASH, L"bitcoincash:ppm2qsznhks23z7629mms6s4cwef74vcwvn0h829pq", from_script, sizeof ( from_script ), hashed_size ) && hashed_size == 23 && from_script [ 0 ] == 0xA9 && base::same ( from_script + 2, from_legacy + 3, 20 );

		check ( same_key && same_script, L"solo mining pays a CashAddr exactly like the legacy address of the same key, for both key and script hashes" );

	}

}

#endif
