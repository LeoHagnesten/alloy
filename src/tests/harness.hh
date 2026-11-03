#pragma once

#ifdef ALLOY_DEBUG

#include "base/base.hh"

namespace tests {

	uint32_t run_all ( );

	void check ( bool passed, const wchar_t* what );

	void section ( const wchar_t* name );

	void hash_tests ( );

	void chain_tests ( );

}

#endif