#pragma once

#include "base/base.hh"

namespace base {

	bool fullscreen_busy ( );

	bool find_process ( const wchar_t* names, wchar_t* which, int32_t room );

	bool open_in_shell ( const wchar_t* target );

	bool run_elevated ( const wchar_t* program, const wchar_t* arguments );

	bool own_path ( wchar_t* out, int32_t room );

}
