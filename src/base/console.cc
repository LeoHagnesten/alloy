#include "base/console.hh"
#include <windows.h>

namespace console {

	static SRWLOCK lock = SRWLOCK_INIT;

	void write ( const wchar_t* text ) {

		HANDLE out = GetStdHandle ( STD_OUTPUT_HANDLE );
		DWORD mode = 0;
		DWORD done = 0;
		int32_t size = base::length ( text );

		if ( !out || out == INVALID_HANDLE_VALUE || !size )
			return;

		AcquireSRWLockExclusive ( &lock );

		if ( GetConsoleMode ( out, &mode ) )
			WriteConsoleW ( out, text, static_cast < DWORD > ( size ), &done, nullptr );
		else {

			char bytes [ 2048 ];
			int32_t length = WideCharToMultiByte ( CP_UTF8, 0, text, size, bytes, sizeof ( bytes ), nullptr, nullptr );

			if ( length > 0 )
				WriteFile ( out, bytes, static_cast < DWORD > ( length ), &done, nullptr );

		}

		ReleaseSRWLockExclusive ( &lock );

	}

	void line ( const wchar_t* text ) {

		write ( text );
		write ( L"\r\n" );

	}

}