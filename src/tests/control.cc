#ifdef ALLOY_DEBUG

#include "tests/harness.hh"
#include "app/session.hh"
#include "pool/test_pool.hh"
#include "base/system/clock.hh"
#include "base/system/processes.hh"
#include "base/strings.hh"
#include "base/text.hh"

namespace tests {

	static base::calendar moment ( uint16_t weekday, uint16_t hour ) {

		base::calendar out;

		out.year = 2026;
		out.month = 11;
		out.day = static_cast < uint16_t > ( 1 + weekday );
		out.weekday = weekday;
		out.hour = hour;

		return out;

	}

	template < typename condition >
	static bool settle_until ( app::session& run, const ui::preferences& settings, double limit, condition done ) {

		double began = base::seconds ( );

		while ( base::seconds ( ) - began < limit ) {

			run.tick ( base::seconds ( ), settings );

			if ( done ( run.view ( ) ) )
				return true;

			base::sleep ( 20 );

		}

		return false;

	}

	static int32_t logged ( const ui::snapshot& view, const wchar_t* prefix ) {

		int32_t found = 0;

		for ( int32_t i = 0; i < view.log_count; i++ )
			found += base::starts_with ( view.logs [ i ].text, prefix ) ? 1 : 0;

		return found;

	}

	void control_tests ( ) {

		section ( L"control" );

		static constexpr uint32_t weekdays = 0x1F;
		static constexpr uint16_t sunday = 0;
		static constexpr uint16_t monday = 1;
		static constexpr uint16_t friday = 5;
		static constexpr uint16_t saturday = 6;

		bool daytime = app::within_schedule ( moment ( monday, 10 ), 9, 17, weekdays ) && !app::within_schedule ( moment ( monday, 18 ), 9, 17, weekdays ) && !app::within_schedule ( moment ( saturday, 10 ), 9, 17, weekdays );
		bool overnight = app::within_schedule ( moment ( friday, 23 ), 22, 7, weekdays ) && app::within_schedule ( moment ( saturday, 3 ), 22, 7, weekdays ) && !app::within_schedule ( moment ( sunday, 23 ), 22, 7, weekdays ) && !app::within_schedule ( moment ( monday, 3 ), 22, 7, weekdays ) && !app::within_schedule ( moment ( monday, 12 ), 22, 7, weekdays );
		bool all_day = app::within_schedule ( moment ( sunday, 4 ), 0, 0, 0x7F );

		check ( daytime && overnight && all_day, L"the schedule handles office hours, nights that cross midnight into the next day, and chosen weekdays" );

		base::calendar date;

		date.year = 2026;
		date.month = 11;
		date.day = 8;

		base::calendar back = base::calendar_of_day ( base::day_number ( date ) );
		base::calendar leap = base::calendar_of_day ( base::day_number ( base::calendar { 2024, 2, 29 } ) + 1 );

		check ( back.year == 2026 && back.month == 11 && back.day == 8 && back.weekday == 0 && leap.month == 3 && leap.day == 1 && base::day_number ( base::calendar { 1970, 1, 1 } ) == 0, L"day numbers round-trip through the calendar, across a leap day and back to 1970" );

		wchar_t which [ 64 ] = { };
		bool own = base::find_process ( L"notepad-that-isnt-running.exe, alloy", which, base::count_of ( which ) ) && base::same_folded ( which, L"Alloy.exe" );
		bool missing = !base::find_process ( L"surely-not-running-anywhere", which, base::count_of ( which ) );

		check ( own && missing, L"the program watch finds Alloy itself by name with or without .exe and ignores names that aren't running" );

		pool::test_server* server = new pool::test_server;
		pool::server_settings config;

		config.port = 0;
		config.difficulty = 0.02;
		config.job_seconds = 30;

		if ( !server->start ( config ) ) {

			check ( false, L"the test pool starts" );
			delete server;

			return;

		}

		while ( !server->ready ( ) )
			base::sleep ( 10 );

		app::session* run = new app::session;
		ui::preferences settings;
		base::writer < wchar_t, 64 > address;

		address.add ( L"127.0.0.1:" ).number ( server->port ( ) );
		base::copy_text ( settings.pool ( ).address, base::count_of ( settings.pool ( ).address ), address.get ( ) );
		settings.threads = 2;
		settings.gpu = false;
		settings.pause_battery = false;
		run->start ( settings );

		bool mining = settle_until ( *run, settings, 20.0, [ ] ( const ui::snapshot& view ) { return view.accepted >= 1; } );

		run->set_held ( true );

		bool held = settle_until ( *run, settings, 3.0, [ ] ( const ui::snapshot& view ) { return view.paused && base::same_text ( view.pause_reason, L"Paused by you" ); } );

		base::sleep ( 400 );
		run->tick ( base::seconds ( ), settings );

		uint64_t frozen_at = run->view ( ).hashes;

		base::sleep ( 600 );
		run->tick ( base::seconds ( ), settings );

		bool still = run->view ( ).hashes - frozen_at < 20000;

		run->set_held ( false );
		run->set_sleeping ( true );

		bool sleeping = settle_until ( *run, settings, 3.0, [ ] ( const ui::snapshot& view ) { return view.paused && base::same_text ( view.pause_reason, L"Paused while the computer sleeps" ); } );
		uint32_t before = run->view ( ).accepted;

		run->set_sleeping ( false );

		bool woke = settle_until ( *run, settings, 20.0, [ & ] ( const ui::snapshot& view ) {

			return !view.paused && logged ( view, L"Reconnecting to the pool after the network changed" ) > 0 && view.accepted > before && view.link == ui::PHASE_READY;

		} );

		run->stop ( );
		check ( mining && held && still, L"pausing by hand stops every thread and says why" );
		check ( sleeping && woke, L"after sleep Alloy drops the stale connection, reconnects at once and gets shares accepted again" );
		delete run;
		server->stop ( );
		delete server;

	}

}

#endif
