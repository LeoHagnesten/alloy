#ifdef ALLOY_DEBUG

#include "tests/harness.hh"
#include "app/export.hh"
#include "app/ledger.hh"
#include "app/rigs.hh"
#include "app/settings.hh"
#include "chain/coinbase.hh"
#include "randomx/variant.hh"
#include "base/system/clock.hh"
#include "base/system/files.hh"
#include "base/memory.hh"
#include "base/strings.hh"
#include "base/text.hh"

namespace tests {

	static bool file_starts ( const wchar_t* path, const char* expected ) {

		base::list < uint8_t > bytes;

		if ( !base::read_file ( path, bytes ) || !bytes.push ( 0 ) )
			return false;

		return base::starts_with ( reinterpret_cast < const char* > ( bytes.data ( ) ), expected );

	}

	void records_tests ( ) {

		section ( L"records" );

		wchar_t folder [ base::path_room ];
		wchar_t path [ base::path_room ];

		if ( !base::temp_folder ( folder, base::path_room, L"alloy-tests" ) ) {

			check ( false, L"a scratch folder in %TEMP% for these tests" );

			return;

		}

		ui::preferences* prefs = new ui::preferences;
		bool applied = app::apply_setting ( *prefs, "randomx.threads=6", 16 ) && app::apply_setting ( *prefs, "theme=light", 16 ) && app::apply_setting ( *prefs, "schedule_days=mon,fri", 16 );
		bool refused = !app::apply_setting ( *prefs, "no equals sign here", 16 );

		check ( applied && refused && prefs->tunings [ chain::ALGORITHM_RANDOMX ].threads == 6 && prefs->theme == ui::THEME_LIGHT && prefs->schedule_days == 0x11, L"--set applies any settings.ini line, per-algorithm keys included, and refuses lines without a value" );

		wchar_t profiles [ app::profile_room ] [ app::profile_name_room ];
		ui::preferences* night = new ui::preferences;
		ui::preferences* loaded = new ui::preferences;

		night->schedule = true;
		night->schedule_from = 23;
		night->algorithm = chain::ALGORITHM_RANDOMX;
		base::copy_text ( night->pools [ chain::ALGORITHM_RANDOMX ].worker, 128, L"night.worker" );

		bool saved = app::save_profile ( L"Night: cheap power", *night, folder );
		int32_t listed = app::list_profiles ( profiles, app::profile_room, folder );
		bool found = false;

		for ( int32_t i = 0; i < listed; i++ )
			found = found || base::same_text ( profiles [ i ], L"Night_ cheap power" );

		bool back = app::load_profile ( L"Night: cheap power", *loaded, 16, folder ) && loaded->schedule && loaded->schedule_from == 23 && loaded->algorithm == chain::ALGORITHM_RANDOMX && base::same_text ( loaded->pools [ chain::ALGORITHM_RANDOMX ].worker, L"night.worker" );
		bool gone = app::delete_profile ( L"Night: cheap power", folder ) && app::list_profiles ( profiles, app::profile_room, folder ) == listed - 1;

		check ( saved && found && back && gone, L"profiles save every setting under a safe file name, list, load back exactly and delete" );
		delete loaded;
		delete night;
		delete prefs;

		app::ledger* book = new app::ledger;
		app::ledger* again = new app::ledger;
		uint32_t today = base::day_number ( base::calendar { 2026, 11, 8 } );

		book->add_time ( today - 1, 3600, 1000000 );
		book->add_time ( today, 60, 5000 );
		book->add_share ( today, true, chain::COIN_MONERO, 0.000125 );
		book->add_share ( today, false, chain::COIN_MONERO, 0.0 );
		base::join_path ( path, base::path_room, folder, L"history.csv" );

		bool kept = book->save ( path ) && again->load ( path );
		const app::day_record& last = again->at ( again->count ( ) - 1 );

		check ( kept && again->count ( ) == 2 && again->at ( 0 ).day == today - 1 && last.accepted == 1 && last.rejected == 1 && last.seconds == 60 && again->lifetime ( ).hashes == 1005000 && last.earned [ chain::COIN_MONERO ] > 0.000124 && last.earned [ chain::COIN_MONERO ] < 0.000126, L"the history file keeps every day's time, hashes, shares and estimated earnings across a restart" );
		delete again;
		delete book;

		{

			static const char legacy [ ] = "2026-11-07,1000,3,0,60,0.5,0.25,0.125,4\r\n";
			app::ledger* old = new app::ledger;
			wchar_t old_path [ base::path_room ];

			base::join_path ( old_path, base::path_room, folder, L"old-history.csv" );

			bool loaded = base::write_file ( old_path, legacy, sizeof ( legacy ) - 1 ) && old->load ( old_path ) && old->count ( ) == 1;
			const app::day_record& day = old->at ( 0 );

			check ( loaded && day.earned [ chain::COIN_BITCOIN ] == 0.5 && day.earned [ chain::COIN_LITECOIN ] == 0.25 && day.earned [ chain::COIN_MONERO ] == 0.125 && day.earned [ chain::COIN_KASPA ] == 4.0 && day.earned [ chain::COIN_BITCOIN_CASH ] == 0.0, L"a history file from before coins had their own columns loads into Bitcoin, Litecoin, Monero and Kaspa" );
			delete old;

		}

		{

			ui::preferences* chosen = new ui::preferences;
			ui::preferences* back = new ui::preferences;

			app::apply_setting ( *chosen, "sha256d.coin=bch", 16 );
			app::apply_setting ( *chosen, "scrypt.coin=doge", 16 );
			app::apply_setting ( *chosen, "randomx.coin=zeph", 16 );

			bool read = chosen->coins [ chain::ALGORITHM_SHA256D ] == chain::COIN_BITCOIN_CASH && chosen->coins [ chain::ALGORITHM_SCRYPT ] == chain::COIN_DOGECOIN && chosen->coins [ chain::ALGORITHM_RANDOMX ] == chain::COIN_ZEPHYR && chosen->variant == randomx::VARIANT_RX0;
			bool refused = !app::apply_setting ( *chosen, "sha256d.coin=doge", 16 ) || chosen->coins [ chain::ALGORITHM_SHA256D ] == chain::COIN_BITCOIN_CASH;

			bool kept = app::save_profile ( L"coins", *chosen, folder ) && app::load_profile ( L"coins", *back, 16, folder ) && back->coins [ chain::ALGORITHM_SHA256D ] == chain::COIN_BITCOIN_CASH && back->coins [ chain::ALGORITHM_RANDOMX ] == chain::COIN_ZEPHYR;

			check ( read && refused && kept, L"each algorithm remembers its coin across a save and load, Zephyr included even though it shares Monero's RandomX, and a coin from another algorithm is ignored" );
			delete back;
			delete chosen;

		}

		ui::snapshot* view = new ui::snapshot;

		view->share_count = 1;
		view->shares [ 0 ].accepted = true;
		view->shares [ 0 ].difficulty = 1234.5;
		view->shares [ 0 ].nonce = 0xDEADBEEF;
		base::copy_text ( view->shares [ 0 ].reason, 80, L"said \"ok\"" );
		view->log_count = 1;
		base::copy_text ( view->logs [ 0 ].text, 160, L"Connected to the pool" );

		wchar_t shares [ base::path_room ];
		wchar_t log [ base::path_room ];

		base::join_path ( shares, base::path_room, folder, L"shares.csv" );
		base::join_path ( log, base::path_room, folder, L"log.txt" );

		bool exported = app::export_shares ( *view, shares ) && app::export_log ( *view, log ) && file_starts ( shares, "time,result,difficulty,latency_ms,nonce,reason\r\n00:00:00,accepted,1234.500000,0.0,deadbeef,\"said \"\"ok\"\"\"" ) && file_starts ( log, "00:00:00  info   Connected to the pool" );

		check ( exported, L"shares export as CSV with quoting and the log as plain text" );
		delete view;

		ui::rig_entry rig;
		bool plain = app::parse_rig ( L"192.168.1.20", rig ) && base::same_text ( rig.address, L"192.168.1.20:4067" ) && !rig.token [ 0 ];
		bool link = app::parse_rig ( L"http://10.0.0.5:4070/#token=0123456789abcdef0123456789abcdef", rig ) && base::same_text ( rig.address, L"10.0.0.5:4070" ) && base::same_text ( rig.token, "0123456789abcdef0123456789abcdef" );
		bool spaced = app::parse_rig ( L"office-pc 0123456789ABCDEF0123456789ABCDEF", rig ) && base::same_text ( rig.address, L"office-pc:4067" ) && base::same_text ( rig.token, "0123456789ABCDEF0123456789ABCDEF" );

		check ( plain && link && spaced, L"a rig can be added as an address, the dashboard link, or an address and its token" );

		static const char genesis [ ] = "01000000010000000000000000000000000000000000000000000000000000000000000000ffffffff4d04ffff001d0104455468652054696d65732030332f4a616e2f32303039204368616e63656c6c6f72206f6e206272696e6b206f66207365636f6e64206261696c6f757420666f722062616e6b73ffffffff0100f2052a01000000434104678afdb0fe5548271967f1a67130b7105cd6a828e03909a67962e0ea1f61deb649f6bc3f4cef38c4f35504e51ec112de5c384df7ba0b8d578a4c702b6bf11d5fac00000000";
		uint8_t tx [ 256 ];
		uint64_t value = 0;
		int32_t size = base::length ( genesis ) / 2;

		base::read_hex ( genesis, tx, static_cast < size_t > ( size ) );
		check ( chain::coinbase_value ( tx, size, value ) && value == 5000000000ull, L"the coinbase reader finds the 50 BTC in Bitcoin's first transaction" );

		double per_share = chain::share_hashes ( chain::ALGORITHM_SHA256D, 1.0 );
		double scrypt_share = chain::share_hashes ( chain::ALGORITHM_SCRYPT, 1.0 );

		check ( per_share == 4294967296.0 && scrypt_share == 65536.0 && chain::block_hashes ( chain::ALGORITHM_RANDOMX, 100.0 ) == 100.0, L"a difficulty-1 share is 2^32 hashes for SHA-256d, 2^16 for scrypt and one per unit for RandomX" );

	}

}

#endif
