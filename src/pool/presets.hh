#pragma once

#include "chain/coins.hh"

namespace pool {

	struct preset {

		uint8_t algorithm;
		uint8_t coin;
		const wchar_t* name;
		const wchar_t* address;
		const wchar_t* backup;
		const wchar_t* tip;

	};

	int32_t preset_count ( );

	const preset& preset_at ( int32_t index );

	int32_t preset_for ( uint8_t coin );

}
