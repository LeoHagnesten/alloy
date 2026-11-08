#include "app/commands.hh"
#include "base/system/console.hh"
#include "base/strings.hh"
#include <windows.h>
#include <shellapi.h>

extern "C" void start ( ) {

	int32_t count = 0;
	wchar_t** arguments = CommandLineToArgvW ( GetCommandLineW ( ), &count );
	uint32_t code = 0;

	if ( app::is_command ( arguments, count ) ) {

		base::attach_console ( );
		code = app::run_command ( arguments, count );

	} else {

		app::launch_options options;

		app::read_launch ( arguments, count, options );
		code = app::open_window ( options );

	}

	if ( arguments )
		LocalFree ( arguments );

	ExitProcess ( code );

}
