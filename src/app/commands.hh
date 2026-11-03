#pragma once

#include "base/base.hh"

namespace app {

	uint32_t run ( wchar_t** arguments, int32_t count );

	uint32_t bench ( const wchar_t* algorithm, uint32_t seconds, uint32_t threads );

	uint32_t genesis ( uint32_t from, uint32_t threads );

}