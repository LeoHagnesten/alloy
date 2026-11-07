#pragma once

#include "ui/model.hh"

namespace app {

	void freeze_settings ( );

	void load_settings ( ui::preferences& out, uint32_t threads, uint32_t suggested );

	bool save_settings ( const ui::preferences& value );

}
