#include "app/commands.hh"
#include <windows.h>
#include <shellapi.h>

extern "C" void start ( ) {

	int32_t count = 0;
	wchar_t** arguments = CommandLineToArgvW ( GetCommandLineW ( ), &count );
	uint32_t code = app::run ( arguments, count );

	if ( arguments )
		LocalFree ( arguments );

	ExitProcess ( code );

}