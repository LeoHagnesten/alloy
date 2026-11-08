#pragma once

#include "ui/model.hh"

namespace app {

	static constexpr int32_t profile_room = 12;
	static constexpr int32_t profile_name_room = 48;

	void freeze_settings ( );

	bool settings_frozen ( );

	void load_settings ( ui::preferences& out, uint32_t threads, uint32_t suggested );

	bool save_settings ( const ui::preferences& value );

	bool settings_exist ( );

	bool apply_setting ( ui::preferences& out, const char* line, uint32_t threads );

	int32_t list_profiles ( wchar_t ( *names ) [ profile_name_room ], int32_t room, const wchar_t* folder = nullptr );

	bool save_profile ( const wchar_t* name, const ui::preferences& value, const wchar_t* folder = nullptr );

	bool load_profile ( const wchar_t* name, ui::preferences& out, uint32_t threads, const wchar_t* folder = nullptr );

	bool delete_profile ( const wchar_t* name, const wchar_t* folder = nullptr );

}
