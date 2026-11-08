#pragma once

#include "chain/algorithm.hh"

namespace chain {

	enum address_verdict : uint8_t {

		ADDRESS_VALID,
		ADDRESS_NONE,
		ADDRESS_BROKEN,
		ADDRESS_OTHER_COIN,

	};

	struct address_check {

		address_verdict verdict = ADDRESS_NONE;
		const wchar_t* coin = L"";

	};

	address_check check_address ( uint8_t algorithm, const char* text );

	address_check check_worker ( uint8_t algorithm, const wchar_t* worker );

	bool base58_decode ( const char* text, uint8_t* out, int32_t room, int32_t& size );

	bool monero_decode ( const char* text, uint8_t* out, int32_t room, int32_t& size );

}
