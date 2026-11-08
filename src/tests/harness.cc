#ifdef ALLOY_DEBUG

#include "tests/harness.hh"
#include "app/settings.hh"
#include "base/system/clock.hh"
#include "base/system/console.hh"
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

	struct group {

		const wchar_t* name;
		void ( *run ) ( );
		bool slow = false;

	};

	static const group groups [ ] = {

		{ L"base", base_tests },
		{ L"hash", hash_tests },
		{ L"scrypt", scrypt_tests },
		{ L"chain", chain_tests },
		{ L"address", address_tests },
		{ L"randomx", randomx_tests },
		{ L"variants", variant_tests },
		{ L"mining", mining_tests },
		{ L"pool", pool_tests },
		{ L"motion", motion_tests },
		{ L"control", control_tests },
		{ L"records", records_tests },
		{ L"kaspa", kaspa_tests },
		{ L"sv2", sv2_tests },
		{ L"xmrig", benchmark_tests, true },

	};

	uint32_t run_all ( const wchar_t* only ) {

		double began = base::seconds ( );

		app::freeze_settings ( );
		passed_count = 0;
		failed_count = 0;

		for ( const group& item : groups ) {

			bool named = only && only [ 0 ] && base::same_text ( only, item.name );

			if ( named || ( !item.slow && ( !only || !only [ 0 ] ) ) )
				item.run ( );

		}

		base::text note;

		base::print_line ( L"" );
		note.number ( passed_count ).add ( L" passed, " ).number ( failed_count ).add ( L" failed in " ).decimal ( base::seconds ( ) - began, 1 ).add ( L" s" );
		base::print_line ( note.get ( ) );

		return failed_count ? 1 : 0;

	}

}

#endif
