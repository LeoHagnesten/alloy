#include "base/system/clipboard.hh"
#include "base/memory.hh"
#include "base/strings.hh"
#include <windows.h>

namespace base {

	bool read_clipboard ( void* owner, wchar_t* out, int32_t room ) {

		if ( !OpenClipboard ( static_cast < HWND > ( owner ) ) )
			return false;

		HANDLE data = GetClipboardData ( CF_UNICODETEXT );
		const wchar_t* text = data ? static_cast < const wchar_t* > ( GlobalLock ( data ) ) : nullptr;

		if ( text ) {

			copy_text ( out, room, text );
			GlobalUnlock ( data );

		}

		CloseClipboard ( );

		return text != nullptr;

	}

	bool write_clipboard ( void* owner, const wchar_t* text ) {

		size_t size = sizeof ( wchar_t ) * static_cast < size_t > ( length ( text ) + 1 );
		HGLOBAL block = GlobalAlloc ( GMEM_MOVEABLE, size );

		if ( !block )
			return false;

		void* target = GlobalLock ( block );

		copy ( target, text, size );
		GlobalUnlock ( block );

		if ( !OpenClipboard ( static_cast < HWND > ( owner ) ) ) {

			GlobalFree ( block );

			return false;

		}

		EmptyClipboard ( );

		bool ok = SetClipboardData ( CF_UNICODETEXT, block ) != nullptr;

		if ( !ok )
			GlobalFree ( block );

		CloseClipboard ( );

		return ok;

	}

}
