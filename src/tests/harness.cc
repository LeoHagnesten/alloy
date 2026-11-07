#ifdef ALLOY_DEBUG

#include "tests/harness.hh"
#include "app/settings.hh"
#include "base/clock.hh"
#include "base/console.hh"
#include "base/memory.hh"
#include "base/strings.hh"
#include "base/text.hh"

namespace tests {

	static uint32_t passed_count = 0;
	static uint32_t failed_count = 0;

	void check ( bool passed, const wchar_t* what ) {

		base::text note;

		note.add ( passed ? L"  pass  " : L"  FAIL  " ).add ( what );
		base::print_line ( note.get ( ) );

		if ( passed )
			passed_count++;
		else
			failed_count++;

	}

	void section ( const wchar_t* name ) {

		base::print_line ( L"" );
		base::print_line ( name );

	}

	bool hex_is ( const uint8_t* bytes, size_t count, const char* expected ) {

		uint8_t wanted [ 128 ];

		return count <= sizeof ( wanted ) && base::read_hex ( expected, wanted, count ) && base::same ( bytes, wanted, count );

	}

	uint32_t run_all ( ) {

		double began = base::seconds ( );

		app::freeze_settings ( );
		passed_count = 0;
		failed_count = 0;
		base_tests ( );
		hash_tests ( );
		chain_tests ( );
		mining_tests ( );
		pool_tests ( );
		motion_tests ( );

		base::text note;

		base::print_line ( L"" );
		note.number ( passed_count ).add ( L" passed, " ).number ( failed_count ).add ( L" failed in " ).decimal ( base::seconds ( ) - began, 1 ).add ( L" s" );
		base::print_line ( note.get ( ) );

		return failed_count ? 1 : 0;

	}

}

#endif
