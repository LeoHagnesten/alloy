#pragma once

#include "base/base.hh"

namespace base {

	struct processor {

		wchar_t name [ 64 ] = { };
		wchar_t label [ 64 ] = { };
		uint32_t threads = 1;
		uint32_t cores = 1;
		bool ssse3 = false;
		bool sse41 = false;
		bool sha = false;
		bool aes = false;
		bool avx2 = false;

	};

	void detect ( processor& out );

	bool system_prefers_light ( );

}
