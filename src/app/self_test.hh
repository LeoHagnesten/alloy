#pragma once

#include "base/base.hh"

namespace app {

	static constexpr int32_t check_room = 24;

	struct check_result {

		wchar_t name [ 112 ] = { };
		bool passed = false;
		bool skipped = false;
		double seconds = 0.0;

	};

	struct self_report {

		check_result checks [ check_room ];
		int32_t count = 0;
		int32_t failed = 0;
		double seconds = 0.0;
		bool sha_extensions = true;
		uint32_t broken_sha_cards = 0;
		uint32_t broken_scrypt_cards = 0;

	};

	void run_self_test ( self_report& out, bool deep );

	uint32_t self_test_command ( );

}
