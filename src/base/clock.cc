#include "base/clock.hh"
#include <windows.h>

namespace base {

	double seconds ( ) {

		LARGE_INTEGER counter;
		LARGE_INTEGER frequency;

		QueryPerformanceCounter ( &counter );
		QueryPerformanceFrequency ( &frequency );

		return static_cast < double > ( counter.QuadPart ) / static_cast < double > ( frequency.QuadPart );

	}

	void sleep ( uint32_t milliseconds ) {

		Sleep ( milliseconds );

	}

	uint32_t unix_time ( ) {

		FILETIME now;

		GetSystemTimeAsFileTime ( &now );

		uint64_t ticks = static_cast < uint64_t > ( now.dwHighDateTime ) << 32 | now.dwLowDateTime;

		return static_cast < uint32_t > ( ticks / 10000000ull - 11644473600ull );

	}

	wall_time local_time ( ) {

		SYSTEMTIME now;

		GetLocalTime ( &now );

		return wall_time { now.wHour, now.wMinute, now.wSecond };

	}

	double idle_seconds ( ) {

		LASTINPUTINFO input = { sizeof ( input ) };

		if ( !GetLastInputInfo ( &input ) )
			return 0.0;

		return static_cast < double > ( GetTickCount ( ) - input.dwTime ) / 1000.0;

	}

	bool on_battery ( ) {

		SYSTEM_POWER_STATUS power;

		return GetSystemPowerStatus ( &power ) && power.ACLineStatus == 0;

	}

}
