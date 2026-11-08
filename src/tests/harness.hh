#pragma once

#ifdef ALLOY_DEBUG

#include "base/base.hh"

namespace tests {

	uint32_t run_all ( const wchar_t* only );

	void check ( bool passed, const wchar_t* what );

	void section ( const wchar_t* name );

	bool hex_is ( const uint8_t* bytes, size_t count, const char* expected );

	void base_tests ( );

	void hash_tests ( );

	void scrypt_tests ( );

	void chain_tests ( );

	void address_tests ( );

	void randomx_tests ( );

	void mining_tests ( );

	void pool_tests ( );

	void motion_tests ( );

}

#endif
