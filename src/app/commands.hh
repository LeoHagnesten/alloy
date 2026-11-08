#pragma once

#include "base/base.hh"

namespace app {

	bool is_command ( wchar_t** arguments, int32_t count );

	uint32_t run_command ( wchar_t** arguments, int32_t count );

	uint32_t open_window ( bool minimized );

#ifdef ALLOY_DEBUG

	uint32_t capture ( const wchar_t* folder, uint32_t warmup, bool clips );

	uint32_t export_icon ( const wchar_t* path );

	uint32_t export_logo ( const wchar_t* path, uint32_t size );

	uint32_t smoke_window ( const wchar_t* shot );

#endif

}
