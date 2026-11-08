#include "base/system/processes.hh"
#include "base/strings.hh"
#include <windows.h>
#include <shellapi.h>
#include <tlhelp32.h>

namespace base {

	bool fullscreen_busy ( ) {

		QUERY_USER_NOTIFICATION_STATE state = QUNS_ACCEPTS_NOTIFICATIONS;

		if ( FAILED ( SHQueryUserNotificationState ( &state ) ) )
			return false;

		return state == QUNS_BUSY || state == QUNS_RUNNING_D3D_FULL_SCREEN || state == QUNS_PRESENTATION_MODE;

	}

	static wchar_t lowered ( wchar_t value ) {

		return value >= L'A' && value <= L'Z' ? static_cast < wchar_t > ( value + 32 ) : value;

	}

	static bool matches ( const wchar_t* running, const wchar_t* name, int32_t size ) {

		int32_t length = base::length ( running );
		int32_t stem = length >= 4 && lowered ( running [ length - 4 ] ) == L'.' && lowered ( running [ length - 3 ] ) == L'e' && lowered ( running [ length - 2 ] ) == L'x' && lowered ( running [ length - 1 ] ) == L'e' ? length - 4 : length;

		if ( size != length && size != stem )
			return false;

		for ( int32_t i = 0; i < size; i++ ) {

			if ( lowered ( running [ i ] ) != lowered ( name [ i ] ) )
				return false;

		}

		return true;

	}

	bool find_process ( const wchar_t* names, wchar_t* which, int32_t room ) {

		if ( !names || !names [ 0 ] )
			return false;

		HANDLE snapshot = CreateToolhelp32Snapshot ( TH32CS_SNAPPROCESS, 0 );

		if ( snapshot == INVALID_HANDLE_VALUE )
			return false;

		PROCESSENTRY32W entry = { sizeof ( entry ) };
		bool found = false;

		for ( BOOL more = Process32FirstW ( snapshot, &entry ); more && !found; more = Process32NextW ( snapshot, &entry ) ) {

			for ( int32_t at = 0; names [ at ] && !found; ) {

				while ( names [ at ] == L',' || names [ at ] == L';' || names [ at ] == L' ' )
					at++;

				int32_t start = at;

				while ( names [ at ] && names [ at ] != L',' && names [ at ] != L';' )
					at++;

				int32_t end = at;

				while ( end > start && names [ end - 1 ] == L' ' )
					end--;

				if ( end > start && matches ( entry.szExeFile, names + start, end - start ) ) {

					found = true;
					copy_text ( which, room, entry.szExeFile );

				}

			}

		}

		CloseHandle ( snapshot );

		return found;

	}

	bool open_in_shell ( const wchar_t* target ) {

		return reinterpret_cast < INT_PTR > ( ShellExecuteW ( nullptr, L"open", target, nullptr, nullptr, SW_SHOWNORMAL ) ) > 32;

	}

	bool run_elevated ( const wchar_t* program, const wchar_t* arguments ) {

		SHELLEXECUTEINFOW info = { sizeof ( info ) };

		info.fMask = SEE_MASK_NOCLOSEPROCESS;
		info.lpVerb = L"runas";
		info.lpFile = program;
		info.lpParameters = arguments;
		info.nShow = SW_HIDE;

		if ( !ShellExecuteExW ( &info ) || !info.hProcess )
			return false;

		WaitForSingleObject ( info.hProcess, 30000 );

		DWORD code = 1;

		GetExitCodeProcess ( info.hProcess, &code );
		CloseHandle ( info.hProcess );

		return code == 0;

	}

	bool own_path ( wchar_t* out, int32_t room ) {

		DWORD size = GetModuleFileNameW ( nullptr, out, static_cast < DWORD > ( room ) );

		return size > 0 && size < static_cast < DWORD > ( room );

	}

}
