#pragma once

#include "chain/algorithm.hh"

namespace chain {

	enum coin_id : uint8_t {

		COIN_BITCOIN,
		COIN_BITCOIN_CASH,
		COIN_ECASH,
		COIN_BITCOIN_SV,
		COIN_DIGIBYTE,
		COIN_LITECOIN,
		COIN_DOGECOIN,
		COIN_MONERO,
		COIN_MONERO_V2,
		COIN_WOWNERO,
		COIN_ARQMA,
		COIN_GRAFT,
		COIN_SAFEX,
		COIN_YADA,
		COIN_KEVA,
		COIN_EQUILIBRIA,
		COIN_ZEPHYR,
		COIN_KASPA,
		COIN_COUNT,

	};

	struct coin_info {

		const wchar_t* name;
		const wchar_t* label;
		const wchar_t* symbol;
		const char* key;
		uint32_t algorithms;
		uint8_t variant;
		const char* gecko;
		double block_seconds;
		double units;
		bool segwit;
		const wchar_t* data_folder;

	};

	static constexpr int32_t coin_room = 16;

	const coin_info& coin_of ( uint8_t id );

	bool coin_mines ( uint8_t id, uint8_t algorithm );

	int32_t coins_for ( uint8_t algorithm, uint8_t* out, int32_t room );

	uint8_t first_coin ( uint8_t algorithm );

	uint8_t coin_of_variant ( uint8_t variant );

	bool read_coin ( const char* key, uint8_t algorithm, uint8_t& out );

}
