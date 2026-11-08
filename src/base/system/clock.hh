#pragma once

#include "base/base.hh"

namespace base {

	struct wall_time {

		uint16_t hour = 0;
		uint16_t minute = 0;
		uint16_t second = 0;

	};

	struct calendar {

		uint16_t year = 0;
		uint16_t month = 0;
		uint16_t day = 0;
		uint16_t weekday = 0;
		uint16_t hour = 0;
		uint16_t minute = 0;
		uint16_t second = 0;

	};

	double seconds ( );

	void sleep ( uint32_t milliseconds );

	uint32_t unix_time ( );

	wall_time local_time ( );

	calendar local_calendar ( );

	uint32_t day_number ( const calendar& date );

	calendar calendar_of_day ( uint32_t day );

	double idle_seconds ( );

	bool on_battery ( );

}
