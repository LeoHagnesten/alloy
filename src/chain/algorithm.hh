#pragma once

#include "base/base.hh"

namespace chain {

	enum algorithm : uint8_t {

		ALGORITHM_SHA256D,
		ALGORITHM_SCRYPT,
		ALGORITHM_RANDOMX,
		ALGORITHM_KHEAVYHASH,
		ALGORITHM_COUNT,

	};

	const wchar_t* algorithm_name ( uint8_t value );

	const wchar_t* algorithm_key ( uint8_t value );

	bool read_algorithm ( const wchar_t* text, uint8_t& out );

	double unit_top ( uint8_t value );

	bool uses_header ( uint8_t value );

}
