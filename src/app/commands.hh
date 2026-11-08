#pragma once

#include "base/base.hh"

namespace ui {

	struct preferences;

}

namespace app {

	struct launch_options {

		bool minimized = false;
		const wchar_t* profile = nullptr;
		const wchar_t* sets [ 16 ] = { };
		int32_t set_count = 0;

	};

	bool is_command ( wchar_t** arguments, int32_t count );

	void read_launch ( wchar_t** arguments, int32_t count, launch_options& out );

	void apply_launch ( const launch_options& options, ui::preferences& settings, uint32_t threads );

	uint32_t run_command ( wchar_t** arguments, int32_t count );

	uint32_t open_window ( const launch_options& options );

#ifdef ALLOY_DEBUG

	uint32_t capture ( const wchar_t* folder, uint32_t warmup, bool clips );

	uint32_t export_icon ( const wchar_t* path );

	uint32_t export_logo ( const wchar_t* path, uint32_t size );

	uint32_t smoke_window ( const wchar_t* shot );

	uint32_t tls_check ( const wchar_t* address, bool http );

#endif

}
