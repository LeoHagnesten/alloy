#include "app/rigs.hh"
#include "net/http.hh"
#include "base/formats/json.hh"
#include "base/system/clock.hh"
#include "base/memory.hh"
#include "base/strings.hh"
#include "base/text.hh"

namespace app {

	static bool hex_char ( wchar_t value ) {

		return ( value >= L'0' && value <= L'9' ) || ( value >= L'a' && value <= L'f' ) || ( value >= L'A' && value <= L'F' );

	}

	bool parse_rig ( const wchar_t* text, ui::rig_entry& out ) {

		out = ui::rig_entry { };

		const wchar_t* at = text;

		while ( *at == L' ' )
			at++;

		if ( base::starts_with ( at, L"http://" ) )
			at += 7;

		int32_t used = 0;
		bool port = false;

		while ( *at && *at != L'/' && *at != L'#' && *at != L' ' && *at != L'?' && used < 90 ) {

			port = port || *at == L':';
			out.address [ used++ ] = *at++;

		}

		if ( !used )
			return false;

		if ( !port ) {

			base::writer < wchar_t, 16 > suffix;

			suffix.add ( L":4067" );
			base::copy_text ( out.address + used, 96 - used, suffix.get ( ) );

		}

		const wchar_t* token = nullptr;

		for ( const wchar_t* scan = at; *scan; scan++ ) {

			if ( base::starts_with ( scan, L"token=" ) ) {

				token = scan + 6;

				break;

			}

		}

		if ( !token ) {

			while ( *at && !hex_char ( *at ) )
				at++;

			token = at;

		}

		int32_t size = 0;

		while ( hex_char ( token [ size ] ) && size < 32 ) {

			out.token [ size ] = static_cast < char > ( token [ size ] );
			size++;

		}

		out.token [ size ] = 0;

		if ( size && size != 32 )
			out.token [ 0 ] = 0;

		return true;

	}

	void rig_watch::configure ( const ui::rig_entry* rigs, int32_t limit ) {

		int32_t used = 0;

		{

			base::guard hold ( guard_lock );

			for ( int32_t i = 0; i < limit && i < ui::rig_room; i++ ) {

				if ( !rigs [ i ].address [ 0 ] )
					continue;

				if ( !base::same_text ( wanted [ used ].address, rigs [ i ].address ) ) {

					found [ used ] = ui::rig_status { };
					base::copy_text ( found [ used ].address, base::count_of ( found [ used ].address ), rigs [ i ].address );

				}

				wanted [ used++ ] = rigs [ i ];

			}

			count = used;

		}

		if ( used && !thread ) {

			quit = 0;
			thread = base::start_thread ( &main, this );

		} else if ( !used )
			stop ( );

	}

	void rig_watch::read ( ui::rig_status* out, int32_t& total ) {

		base::guard hold ( guard_lock );

		total = count;

		for ( int32_t i = 0; i < count; i++ )
			out [ i ] = found [ i ];

	}

	void rig_watch::stop ( ) {

		if ( !thread )
			return;

		base::atomic_store ( quit, 1u );
		base::join_thread ( thread );
		thread = nullptr;

	}

	unsigned long rig_watch::main ( void* context ) {

		static_cast < rig_watch* > ( context )->run ( );

		return 0;

	}

	void rig_watch::run ( ) {

		base::json* parsed = new base::json;
		net::http_reply* reply = new net::http_reply;

		while ( !base::atomic_load ( quit ) ) {

			ui::rig_entry asked [ ui::rig_room ];
			int32_t total = 0;

			{

				base::guard hold ( guard_lock );

				total = count;

				for ( int32_t i = 0; i < total; i++ )
					asked [ i ] = wanted [ i ];

			}

			for ( int32_t i = 0; i < total && !base::atomic_load ( quit ); i++ ) {

				char address [ 96 ];
				base::writer < char, 160 > url;
				base::writer < char, 96 > header;
				ui::rig_status status;

				base::narrow ( address, sizeof ( address ), asked [ i ].address );
				url.add ( "http://" ).add ( address ).add ( "/api.json" );

				if ( asked [ i ].token [ 0 ] )
					header.add ( "Authorization: Bearer " ).add ( asked [ i ].token ).add ( "\r\n" );

				base::copy_text ( status.address, base::count_of ( status.address ), asked [ i ].address );

				bool reached = net::http_request ( "GET", url.get ( ), header.get ( ), nullptr, 0, *reply, 2500 );

				if ( reached && reply->status == 200 && parsed->parse ( reply->body.data ( ), reply->body.size ( ) - 1 ) ) {

					char text [ 96 ];

					status.state = ui::RIG_ONLINE;
					parsed->text ( parsed->member ( 0, "name" ), text, sizeof ( text ) );
					base::widen ( status.name, base::count_of ( status.name ), text );
					parsed->text ( parsed->member ( 0, "variant" ), text, sizeof ( text ) );
					base::widen ( status.algorithm, base::count_of ( status.algorithm ), text );
					status.running = parsed->truth ( parsed->member ( 0, "running" ) );
					status.paused = parsed->truth ( parsed->member ( 0, "paused" ) );
					status.rate = parsed->number ( parsed->member ( parsed->member ( 0, "hashrate" ), "total" ) );
					status.accepted = static_cast < uint32_t > ( parsed->number ( parsed->member ( parsed->member ( 0, "shares" ), "accepted" ) ) );
					status.rejected = static_cast < uint32_t > ( parsed->number ( parsed->member ( parsed->member ( 0, "shares" ), "rejected" ) ) );
					status.uptime = parsed->number ( parsed->member ( 0, "uptime" ) );
					status.gpus = parsed->count ( parsed->member ( 0, "gpus" ) );
					status.seen = base::seconds ( );

				} else {

					base::guard hold ( guard_lock );

					status = found [ i ];
					status.state = reached && ( reply->status == 401 || reply->status == 403 ) ? ui::RIG_LOCKED : ui::RIG_UNREACHABLE;

				}

				base::guard hold ( guard_lock );

				if ( i < count && base::same_text ( wanted [ i ].address, asked [ i ].address ) )
					found [ i ] = status;

			}

			for ( int32_t wait = 0; wait < 30 && !base::atomic_load ( quit ); wait++ )
				base::sleep ( 100 );

		}

		delete reply;
		delete parsed;

	}

}
