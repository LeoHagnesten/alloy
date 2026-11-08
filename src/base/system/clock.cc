#include "base/system/clock.hh"
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

	calendar local_calendar ( ) {

		SYSTEMTIME now;

		GetLocalTime ( &now );

		return calendar { now.wYear, now.wMonth, now.wDay, now.wDayOfWeek, now.wHour, now.wMinute, now.wSecond };

	}

	uint32_t day_number ( const calendar& date ) {

		int32_t year = date.year - ( date.month <= 2 ? 1 : 0 );
		int32_t era = year / 400;
		int32_t within = year - era * 400;
		int32_t month = date.month > 2 ? date.month - 3 : date.month + 9;
		int32_t yearday = ( 153 * month + 2 ) / 5 + date.day - 1;
		int32_t eraday = within * 365 + within / 4 - within / 100 + yearday;

		return static_cast < uint32_t > ( era * 146097 + eraday - 719468 );

	}

	calendar calendar_of_day ( uint32_t day ) {

		int32_t z = static_cast < int32_t > ( day ) + 719468;
		int32_t era = z / 146097;
		int32_t eraday = z - era * 146097;
		int32_t within = ( eraday - eraday / 1460 + eraday / 36524 - eraday / 146096 ) / 365;
		int32_t yearday = eraday - ( 365 * within + within / 4 - within / 100 );
		int32_t month = ( 5 * yearday + 2 ) / 153;
		calendar out;

		out.day = static_cast < uint16_t > ( yearday - ( 153 * month + 2 ) / 5 + 1 );
		out.month = static_cast < uint16_t > ( month < 10 ? month + 3 : month - 9 );
		out.year = static_cast < uint16_t > ( within + era * 400 + ( out.month <= 2 ? 1 : 0 ) );
		out.weekday = static_cast < uint16_t > ( ( day + 4 ) % 7 );

		return out;

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
