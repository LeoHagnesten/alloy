#include "app/commands.hh"
#include "base/system/console.hh"
#include "base/strings.hh"
#include <windows.h>
#include <shellapi.h>

extern "C" void start ( ) {

	int32_t count = 0;
	wchar_t** arguments = CommandLineToArgvW ( GetCommandLineW ( ), &count );
	uint32_t code = 0;

	bool minimized = count == 2 && arguments && base::same_text ( arguments [ 1 ], L"--minimized" );

	if ( !minimized && app::is_command ( arguments, count ) ) {

		base::attach_console ( );
		code = app::run_command ( arguments, count );

	} else
		code = app::open_window ( minimized );

	if ( arguments )
		LocalFree ( arguments );

	ExitProcess ( code );

}
