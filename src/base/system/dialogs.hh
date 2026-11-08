#pragma once

#include "base/base.hh"

namespace base {

	bool ask_save_path ( void* owner, const wchar_t* suggested, const wchar_t* kind, const wchar_t* extension, wchar_t* out, int32_t room );

}
