#pragma once

#include "ui/model.hh"

namespace app {

	bool export_shares ( const ui::snapshot& view, const wchar_t* path );

	bool export_log ( const ui::snapshot& view, const wchar_t* path );

	bool export_days ( const ui::snapshot& view, const wchar_t* path );

}
