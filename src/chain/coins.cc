#include "chain/coins.hh"
#include "randomx/variant.hh"
#include "base/strings.hh"

namespace chain {

	static constexpr uint32_t sha = 1u << ALGORITHM_SHA256D;
	static constexpr uint32_t scrypt = 1u << ALGORITHM_SCRYPT;
	static constexpr uint32_t randomx = 1u << ALGORITHM_RANDOMX;
	static constexpr uint32_t heavy = 1u << ALGORITHM_KHEAVYHASH;
	static constexpr uint32_t ethash = 1u << ALGORITHM_ETHASH;

	static const coin_info coins [ COIN_COUNT ] = {

		{ L"Bitcoin", L"Bitcoin", L"BTC", "btc", sha, 0, "bitcoin", 600.0, 1e8, true, L"Bitcoin" },
		{ L"Bitcoin Cash", L"BCH", L"BCH", "bch", sha, 0, "bitcoin-cash", 600.0, 1e8, false, L"Bitcoin" },
		{ L"eCash", L"eCash", L"XEC", "xec", sha, 0, "ecash", 600.0, 1e2, false, L"Bitcoin" },
		{ L"Bitcoin SV", L"BSV", L"BSV", "bsv", sha, 0, "bitcoin-cash-sv", 600.0, 1e8, false, L"Bitcoin" },
		{ L"DigiByte", L"DigiByte", L"DGB", "dgb", sha | scrypt, 0, "digibyte", 75.0, 1e8, true, L"DigiByte" },
		{ L"Litecoin", L"Litecoin", L"LTC", "ltc", scrypt, 0, "litecoin", 150.0, 1e8, true, L"Litecoin" },
		{ L"Dogecoin", L"Dogecoin", L"DOGE", "doge", scrypt, 0, "dogecoin", 60.0, 1e8, false, L"Dogecoin" },
		{ L"Monero", L"Monero", L"XMR", "xmr", randomx, randomx::VARIANT_RX0, "monero", 120.0, 1e12, false, L"" },
		{ L"Monero after the RandomX v2 fork", L"RX v2", L"XMR", "xmr2", randomx, randomx::VARIANT_RX2, "monero", 120.0, 1e12, false, L"" },
		{ L"Wownero", L"Wownero", L"WOW", "wow", randomx, randomx::VARIANT_WOW, "wownero", 300.0, 1e11, false, L"" },
		{ L"ArQmA", L"ArQmA", L"ARQ", "arq", randomx, randomx::VARIANT_ARQ, "arqma", 120.0, 1e11, false, L"" },
		{ L"Graft", L"Graft", L"GRFT", "grft", randomx, randomx::VARIANT_GRAFT, "graft-blockchain", 120.0, 1e11, false, L"" },
		{ L"Safex Cash", L"Safex", L"SFX", "sfx", randomx, randomx::VARIANT_SFX, "safex-cash", 120.0, 1e11, false, L"" },
		{ L"YadaCoin", L"Yada", L"YDA", "yda", randomx, randomx::VARIANT_YADA, "yadacoin", 600.0, 1e11, false, L"" },
		{ L"Kevacoin", L"Keva", L"KVA", "kva", randomx, randomx::VARIANT_KEVA, "kevacoin", 120.0, 1e11, false, L"" },
		{ L"Equilibria", L"Equilibria", L"XEQ", "xeq", randomx, randomx::VARIANT_XEQ, "", 120.0, 1e11, false, L"" },
		{ L"Zephyr", L"Zephyr", L"ZEPH", "zeph", randomx, randomx::VARIANT_RX0, "zephyr-protocol", 120.0, 1e12, false, L"" },
		{ L"Kaspa", L"Kaspa", L"KAS", "kas", heavy, 0, "kaspa", 0.1, 1e8, false, L"" },
		{ L"Ethereum Classic", L"Ethereum Classic", L"ETC", "etc", ethash, 0, "ethereum-classic", 13.0, 1e18, false, L"" },
		{ L"EthereumPoW", L"EthereumPoW", L"ETHW", "ethw", ethash, 0, "ethereum-pow-iou", 13.0, 1e18, false, L"" },

	};

	const coin_info& coin_of ( uint8_t id ) {

		return coins [ id < COIN_COUNT ? id : 0 ];

	}

	bool coin_mines ( uint8_t id, uint8_t algorithm ) {

		return id < COIN_COUNT && algorithm < ALGORITHM_COUNT && ( coins [ id ].algorithms >> algorithm & 1u );

	}

	int32_t coins_for ( uint8_t algorithm, uint8_t* out, int32_t room ) {

		int32_t count = 0;

		for ( uint8_t i = 0; i < COIN_COUNT && count < room; i++ ) {

			if ( coin_mines ( i, algorithm ) )
				out [ count++ ] = i;

		}

		return count;

	}

	uint8_t first_coin ( uint8_t algorithm ) {

		uint8_t found = COIN_BITCOIN;

		return coins_for ( algorithm, &found, 1 ) ? found : static_cast < uint8_t > ( COIN_BITCOIN );

	}

	uint8_t coin_of_variant ( uint8_t variant ) {

		for ( uint8_t i = 0; i < COIN_COUNT; i++ ) {

			if ( coin_mines ( i, ALGORITHM_RANDOMX ) && coins [ i ].variant == variant )
				return i;

		}

		return COIN_MONERO;

	}

	bool read_coin ( const char* key, uint8_t algorithm, uint8_t& out ) {

		for ( uint8_t i = 0; i < COIN_COUNT; i++ ) {

			if ( coin_mines ( i, algorithm ) && base::same_folded ( key, coins [ i ].key ) ) {

				out = i;

				return true;

			}

		}

		return false;

	}

}
