#include "app/export.hh"
#include "base/system/files.hh"
#include "base/strings.hh"
#include "base/text.hh"

namespace app {

	static void quoted ( base::line& out, const wchar_t* value ) {

		char text [ 512 ];

		base::narrow ( text, sizeof ( text ), value );
		out.put ( '"' );

		for ( int32_t i = 0; text [ i ]; i++ ) {

			if ( text [ i ] == '"' )
				out.put ( '"' );

			out.put ( text [ i ] );

		}

		out.put ( '"' );

	}

	static bool flush ( base::list < char >& text, const wchar_t* path ) {

		return base::write_file ( path, text.data ( ), static_cast < size_t > ( text.size ( ) ) );

	}

	bool export_shares ( const ui::snapshot& view, const wchar_t* path ) {

		base::list < char >* text = new base::list < char >;
		base::line* row = new base::line;

		row->add ( "time,result,difficulty,latency_ms,nonce,reason\r\n" );
		text->append ( row->get ( ), row->size ( ) );

		for ( int32_t i = 0; i < view.share_count; i++ ) {

			const ui::share_row& item = view.shares [ i ];

			row->clear ( );
			row->clock ( item.time.hour, item.time.minute, item.time.second ).put ( ',' ).add ( item.accepted ? "accepted" : "rejected" );
			row->put ( ',' ).decimal ( item.difficulty, 6 ).put ( ',' ).decimal ( item.latency, 1 ).put ( ',' ).hex32 ( item.nonce ).put ( ',' );
			quoted ( *row, item.reason );
			row->add ( "\r\n" );
			text->append ( row->get ( ), row->size ( ) );

		}

		bool good = flush ( *text, path );

		delete row;
		delete text;

		return good;

	}

	bool export_log ( const ui::snapshot& view, const wchar_t* path ) {

		static const char* const levels [ ] = { "info ", "ok   ", "warn ", "error" };
		base::list < char >* text = new base::list < char >;
		base::line* row = new base::line;
		char body [ 512 ];

		for ( int32_t i = 0; i < view.log_count; i++ ) {

			const ui::log_row& item = view.logs [ i ];

			base::narrow ( body, sizeof ( body ), item.text );
			row->clear ( );
			row->clock ( item.time.hour, item.time.minute, item.time.second ).add ( "  " ).add ( levels [ item.level < 4 ? item.level : 0 ] ).add ( "  " ).add ( body ).add ( "\r\n" );
			text->append ( row->get ( ), row->size ( ) );

		}

		bool good = flush ( *text, path );

		delete row;
		delete text;

		return good;

	}

	bool export_days ( const ui::snapshot& view, const wchar_t* path ) {

		base::list < char >* text = new base::list < char >;
		base::line* row = new base::line;

		row->add ( "date,seconds_mined,hashes,accepted,rejected" );

		for ( int32_t k = 0; k < chain::COIN_COUNT; k++ )
			row->put ( ',' ).add ( chain::coin_of ( static_cast < uint8_t > ( k ) ).key );

		row->add ( "\r\n" );
		text->append ( row->get ( ), row->size ( ) );

		for ( int32_t i = view.day_count - 1; i >= 0; i-- ) {

			const ui::day_row& item = view.days [ i ];
			base::calendar date = base::calendar_of_day ( item.day );

			row->clear ( );
			row->number ( date.year ).put ( '-' ).padded ( date.month, 2 ).put ( '-' ).padded ( date.day, 2 );
			row->put ( ',' ).number ( item.seconds ).put ( ',' ).number ( item.hashes ).put ( ',' ).number ( item.accepted ).put ( ',' ).number ( item.rejected );

			for ( int32_t k = 0; k < chain::COIN_COUNT; k++ )
				row->put ( ',' ).decimal ( item.earned [ k ], 12 );

			row->add ( "\r\n" );
			text->append ( row->get ( ), row->size ( ) );

		}

		bool good = flush ( *text, path );

		delete row;
		delete text;

		return good;

	}

}
