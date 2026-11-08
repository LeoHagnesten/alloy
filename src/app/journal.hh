#pragma once

#include "base/base.hh"

namespace app {

	void open_journal ( const wchar_t* elsewhere = nullptr );

	void close_journal ( );

	void journal ( uint8_t level, const wchar_t* text );

	bool journal_folder ( wchar_t* out, int32_t room );

	bool previous_crash ( wchar_t* report, int32_t room, bool& unclean );

}
