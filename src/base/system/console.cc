#include "base/system/console.hh"
#include "base/strings.hh"
#include <windows.h>

namespace base {

	static SRWLOCK console_lock = SRWLOCK_INIT;
	static HANDLE console_out = nullptr;

	static void emit ( const wchar_t* text ) {

		DWORD mode = 0;
		DWORD done = 0;
		int32_t size = length ( text );

		if ( !size )
			return;

		if ( GetConsoleMode ( console_out, &mode ) ) {

			WriteConsoleW ( console_out, text, static_cast < DWORD > ( size ), &done, nullptr );

			return;

		}

		char bytes [ 4096 ];
		int32_t count = narrow ( bytes, sizeof ( bytes ), text );

		if ( count > 0 )
			WriteFile ( console_out, bytes, static_cast < DWORD > ( count ), &done, nullptr );

	}

	void attach_console ( ) {

		HANDLE inherited = GetStdHandle ( STD_OUTPUT_HANDLE );

		if ( inherited && inherited != INVALID_HANDLE_VALUE ) {

			console_out = inherited;

			return;

		}

		if ( AttachConsole ( ATTACH_PARENT_PROCESS ) ) {

			console_out = CreateFileW ( L"CONOUT$", GENERIC_WRITE, FILE_SHARE_READ | FILE_SHARE_WRITE, nullptr, OPEN_EXISTING, 0, nullptr );

			if ( console_out == INVALID_HANDLE_VALUE )
				console_out = nullptr;

		}

	}

	void print ( const wchar_t* text ) {

		if ( !console_out )
			return;

		AcquireSRWLockExclusive ( &console_lock );
		emit ( text );
		ReleaseSRWLockExclusive ( &console_lock );

	}

	void print_line ( const wchar_t* text ) {

		if ( !console_out )
			return;

		AcquireSRWLockExclusive ( &console_lock );
		emit ( text );
		emit ( L"\r\n" );
		ReleaseSRWLockExclusive ( &console_lock );

	}

}
