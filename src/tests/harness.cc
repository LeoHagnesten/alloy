#ifdef ALLOY_DEBUG

#include "tests/harness.hh"
#include "base/console.hh"
#include "base/text.hh"

namespace tests {

	static uint32_t passed_count = 0;
	static uint32_t failed_count = 0;

	void check ( bool passed, const wchar_t* what ) {

		base::text note;

		note.add ( passed ? L"  pass  " : L"  FAIL  " ).add ( what );
		console::line ( note.get ( ) );

		if ( passed )
			passed_count++;
		else
			failed_count++;

	}

	void section ( const wchar_t* name ) {

		console::line ( L"" );
		console::line ( name );

	}

	uint32_t run_all ( ) {

		passed_count = 0;
		failed_count = 0;
		hash_tests ( );
		chain_tests ( );

		base::text note;

		console::line ( L"" );
		note.number ( passed_count ).add ( L" passed, " ).number ( failed_count ).add ( L" failed" );
		console::line ( note.get ( ) );

		return failed_count ? 1 : 0;

	}

}

#endif