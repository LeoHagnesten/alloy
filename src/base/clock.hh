#pragma once

#include "base/base.hh"

namespace base {

	struct wall_time {

		uint16_t hour = 0;
		uint16_t minute = 0;
		uint16_t second = 0;

	};

	double seconds ( );

	void sleep ( uint32_t milliseconds );

	uint32_t unix_time ( );

	wall_time local_time ( );

	double idle_seconds ( );

	bool on_battery ( );

}
