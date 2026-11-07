#pragma once

#include "base/base.hh"

namespace base {

	bool read_clipboard ( void* owner, wchar_t* out, int32_t room );

	bool write_clipboard ( void* owner, const wchar_t* text );

}
