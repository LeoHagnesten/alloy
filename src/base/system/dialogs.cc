#include "base/system/dialogs.hh"
#include "base/strings.hh"
#include "base/text.hh"
#include <windows.h>
#include <commdlg.h>

namespace base {

	bool ask_save_path ( void* owner, const wchar_t* suggested, const wchar_t* kind, const wchar_t* extension, wchar_t* out, int32_t room ) {

		wchar_t filter [ 160 ] = { };
		writer < wchar_t, 80 > label;
		writer < wchar_t, 32 > pattern;
		int32_t at = 0;

		label.add ( kind ).add ( L" (*." ).add ( extension ).put ( L')' );
		pattern.add ( L"*." ).add ( extension );
		copy_text ( filter, 160, label.get ( ) );
		at = length ( filter ) + 1;
		copy_text ( filter + at, 160 - at, pattern.get ( ) );
		copy_text ( out, room, suggested );

		OPENFILENAMEW dialog = { sizeof ( dialog ) };

		dialog.hwndOwner = static_cast < HWND > ( owner );
		dialog.lpstrFilter = filter;
		dialog.lpstrFile = out;
		dialog.nMaxFile = static_cast < DWORD > ( room );
		dialog.lpstrDefExt = extension;
		dialog.Flags = OFN_OVERWRITEPROMPT | OFN_PATHMUSTEXIST | OFN_NOCHANGEDIR;

		return GetSaveFileNameW ( &dialog ) != FALSE;

	}

}
