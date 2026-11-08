#pragma once

#include "base/base.hh"

namespace base {

	struct processor {

		wchar_t name [ 64 ] = { };
		wchar_t label [ 64 ] = { };
		uint32_t threads = 1;
		uint32_t cores = 1;
		uint64_t level3 = 0;
		bool ssse3 = false;
		bool sse41 = false;
		bool sha = false;
		bool aes = false;
		bool avx2 = false;

	};

	void detect ( processor& out );

	bool system_prefers_light ( );

	bool allow_large_pages ( );

	bool set_run_at_login ( const wchar_t* name, bool on, const wchar_t* arguments );

	bool computer_name ( wchar_t* out, int32_t room );

	bool random_bytes ( void* out, size_t size );

	bool large_pages_granted ( );

}
