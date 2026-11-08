#include "app/ledger.hh"
#include "app/settings.hh"
#include "base/system/clock.hh"
#include "base/system/files.hh"
#include "base/memory.hh"
#include "base/strings.hh"
#include "base/text.hh"

namespace app {

	static bool ledger_path ( wchar_t* out, int32_t room ) {

		wchar_t folder [ base::path_room ];

		return base::data_folder ( folder, base::path_room ) && base::join_path ( out, room, folder, L"history.csv" );

	}

	static const char* field ( const char* at, char* out, int32_t room ) {

		int32_t used = 0;

		while ( *at && *at != ',' && *at != '\r' && *at != '\n' ) {

			if ( used < room - 1 )
				out [ used++ ] = *at;

			at++;

		}

		out [ used ] = 0;

		return *at == ',' ? at + 1 : at;

	}

	static uint32_t day_of_text ( const char* text ) {

		base::calendar date;
		uint32_t parts [ 3 ] = { };
		int32_t part = 0;

		for ( int32_t i = 0; text [ i ] && part < 3; i++ ) {

			if ( text [ i ] == '-' )
				part++;
			else if ( text [ i ] >= '0' && text [ i ] <= '9' )
				parts [ part ] = parts [ part ] * 10 + static_cast < uint32_t > ( text [ i ] - '0' );

		}

		date.year = static_cast < uint16_t > ( parts [ 0 ] );
		date.month = static_cast < uint16_t > ( parts [ 1 ] );
		date.day = static_cast < uint16_t > ( parts [ 2 ] );

		return date.year > 2000 && date.month >= 1 && date.month <= 12 && date.day >= 1 ? base::day_number ( date ) : 0;

	}

	bool ledger::load ( const wchar_t* path ) {

		wchar_t own [ base::path_room ];
		base::list < uint8_t > bytes;

		used = 0;
		total = day_record { };
		changed = false;

		if ( !path && ( settings_frozen ( ) || !ledger_path ( own, base::path_room ) ) )
			return false;

		if ( !base::read_file ( path ? path : own, bytes ) || !bytes.push ( 0 ) )
			return false;

		const char* at = reinterpret_cast < const char* > ( bytes.data ( ) );

		while ( *at ) {

			char cell [ 40 ];
			day_record row;

			at = field ( at, cell, sizeof ( cell ) );
			row.day = day_of_text ( cell );
			at = field ( at, cell, sizeof ( cell ) );
			row.hashes = base::read_number ( cell );
			at = field ( at, cell, sizeof ( cell ) );
			row.accepted = static_cast < uint32_t > ( base::read_number ( cell ) );
			at = field ( at, cell, sizeof ( cell ) );
			row.rejected = static_cast < uint32_t > ( base::read_number ( cell ) );
			at = field ( at, cell, sizeof ( cell ) );
			row.seconds = static_cast < uint32_t > ( base::read_number ( cell ) );

			for ( int32_t i = 0; i < chain::ALGORITHM_COUNT; i++ ) {

				at = field ( at, cell, sizeof ( cell ) );
				row.earned [ i ] = base::read_decimal ( cell, base::length ( cell ) );

			}

			while ( *at && *at != '\n' )
				at++;

			if ( *at )
				at++;

			if ( !row.day )
				continue;

			day_record& slot = on ( row.day );

			slot = row;
			total.hashes += row.hashes;
			total.accepted += row.accepted;
			total.rejected += row.rejected;
			total.seconds += row.seconds;

			for ( int32_t i = 0; i < chain::ALGORITHM_COUNT; i++ )
				total.earned [ i ] += row.earned [ i ];

		}

		changed = false;

		return true;

	}

	bool ledger::save ( const wchar_t* path ) {

		wchar_t own [ base::path_room ];

		if ( !path && ( settings_frozen ( ) || !ledger_path ( own, base::path_room ) ) )
			return false;

		base::line* out = new base::line;
		base::list < char >* text = new base::list < char >;

		for ( int32_t i = 0; i < used; i++ ) {

			const day_record& row = days [ i ];
			base::calendar date = base::calendar_of_day ( row.day );

			out->clear ( );
			out->number ( date.year ).put ( '-' ).padded ( date.month, 2 ).put ( '-' ).padded ( date.day, 2 );
			out->put ( ',' ).number ( row.hashes ).put ( ',' ).number ( row.accepted ).put ( ',' ).number ( row.rejected ).put ( ',' ).number ( row.seconds );

			for ( int32_t k = 0; k < chain::ALGORITHM_COUNT; k++ )
				out->put ( ',' ).decimal ( row.earned [ k ], 12 );

			out->add ( "\r\n" );
			text->append ( out->get ( ), out->size ( ) );

		}

		bool good = base::write_file ( path ? path : own, text->data ( ), static_cast < size_t > ( text->size ( ) ) );

		changed = !good;
		delete text;
		delete out;

		return good;

	}

	day_record& ledger::on ( uint32_t day ) {

		for ( int32_t i = used - 1; i >= 0; i-- ) {

			if ( days [ i ].day == day )
				return days [ i ];

		}

		if ( used == day_room ) {

			base::move ( days, days + 1, sizeof ( day_record ) * ( day_room - 1 ) );
			used--;

		}

		int32_t at = used;

		while ( at > 0 && days [ at - 1 ].day > day ) {

			days [ at ] = days [ at - 1 ];
			at--;

		}

		days [ at ] = day_record { };
		days [ at ].day = day;
		used++;

		return days [ at ];

	}

	void ledger::add_time ( uint32_t day, uint32_t seconds, uint64_t hashes ) {

		day_record& slot = on ( day );

		slot.seconds += seconds;
		slot.hashes += hashes;
		total.seconds += seconds;
		total.hashes += hashes;
		changed = true;

	}

	void ledger::add_share ( uint32_t day, bool accepted, uint8_t algorithm, double value ) {

		day_record& slot = on ( day );

		if ( accepted ) {

			slot.accepted++;
			total.accepted++;
			slot.earned [ algorithm % chain::ALGORITHM_COUNT ] += value;
			total.earned [ algorithm % chain::ALGORITHM_COUNT ] += value;

		} else {

			slot.rejected++;
			total.rejected++;

		}

		changed = true;

	}

}
