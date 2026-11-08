#include "pool/client.hh"
#include "net/socket.hh"
#include "base/bits.hh"
#include "base/memory.hh"
#include "base/strings.hh"
#include "base/text.hh"

namespace pool {

	bool parse_kaspa_notify ( const base::json& message, int32_t params, kaspa_job& out ) {

		int32_t id = message.item ( params, 0 );
		int32_t header = message.item ( params, 1 );

		out = kaspa_job { };

		if ( id < 0 || header < 0 || message.raw_size ( id ) <= 0 || message.raw_size ( id ) >= static_cast < int32_t > ( sizeof ( out.id ) ) )
			return false;

		base::copy_span ( out.id, sizeof ( out.id ), message.raw ( id ), message.raw_size ( id ) );

		if ( message.kind ( header ) == base::JSON_STRING ) {

			uint8_t bytes [ 40 ];

			if ( message.raw_size ( header ) != 80 || !base::read_hex ( message.raw ( header ), 80, bytes, 40 ) )
				return false;

			base::copy ( out.pre_pow, bytes, 32 );
			out.timestamp = base::load_little64 ( bytes + 32 );

			return true;

		}

		int32_t stamp = message.item ( params, 2 );

		if ( message.kind ( header ) != base::JSON_ARRAY || message.count ( header ) != 4 || message.kind ( stamp ) != base::JSON_NUMBER )
			return false;

		for ( int32_t i = 0; i < 4; i++ ) {

			int32_t word = message.item ( header, i );

			if ( message.kind ( word ) != base::JSON_NUMBER )
				return false;

			base::store_little64 ( out.pre_pow + i * 8, base::read_number ( message.raw ( word ) ) );

		}

		out.timestamp = base::read_number ( message.raw ( stamp ) );

		return true;

	}

	void write_kaspa_notify ( base::line& out, const kaspa_job& work, bool hex_form ) {

		out.add ( "{\"id\":null,\"method\":\"mining.notify\",\"params\":[" );
		base::add_json_string ( out, work.id );

		if ( hex_form ) {

			uint8_t stamp [ 8 ];

			base::store_little64 ( stamp, work.timestamp );
			out.add ( ",\"" ).hex ( work.pre_pow, 32, false ).hex ( stamp, 8, false ).add ( "\"]}" );

			return;

		}

		out.add ( ",[" );

		for ( int32_t i = 0; i < 4; i++ )
			out.add ( i ? "," : "" ).number ( base::load_little64 ( work.pre_pow + i * 8 ) );

		out.add ( "]," ).number ( work.timestamp ).add ( "]}" );

	}

	bool kaspa_nonce_high ( const uint8_t* extranonce, int32_t size, uint64_t index, uint32_t& out ) {

		int32_t bits = 32 - size * 8;
		uint32_t fixed = 0;

		if ( size < 0 || size > 4 )
			return false;

		for ( int32_t i = 0; i < size; i++ )
			fixed = fixed << 8 | extranonce [ i ];

		if ( bits < 32 && index >> bits )
			return false;

		out = bits == 32 ? static_cast < uint32_t > ( index ) : fixed << bits | static_cast < uint32_t > ( index );

		return bits < 32 || index <= 0xFFFFFFFFull;

	}

	void client::take_kaspa_job ( const base::json& message, int32_t params ) {

		kaspa_job* incoming = new kaspa_job;

		if ( !parse_kaspa_notify ( message, params, *incoming ) ) {

			delete incoming;
			note ( EVENT_WARNING, L"The pool sent a job Alloy couldn't read" );

			return;

		}

		wchar_t name [ 64 ];

		base::widen ( name, base::count_of ( name ), incoming->id );

		{

			base::guard hold ( state_lock );

			kaspa_current = *incoming;
			have_job = true;
			difficulty = next_difficulty;
			advance ( );
			clean_serial = serial;
			shown.jobs++;
			shown.has_job = true;
			shown.difficulty = difficulty;
			shown.time = static_cast < uint32_t > ( incoming->timestamp / 1000 );
			base::copy ( shown.previous, incoming->pre_pow, 32 );
			base::copy_text ( shown.job, base::count_of ( shown.job ), name );

		}

		base::text line;

		line.add ( L"New job " ).add ( name );
		note ( EVENT_JOB, line.get ( ) );
		delete incoming;

	}

	void client::take_kaspa_extranonce ( const base::json& message, int32_t params ) {

		uint8_t bytes [ extranonce_room ];
		int32_t node = message.item ( params, 0 );
		int32_t length = message.raw_size ( node );

		if ( message.kind ( node ) != base::JSON_STRING || length % 2 || length / 2 > 4 || !base::read_hex ( message.raw ( node ), length, bytes, static_cast < size_t > ( length / 2 ) ) ) {

			note ( EVENT_WARNING, L"The pool sent an extranonce longer than Alloy can fit in Kaspa's nonce" );

			return;

		}

		base::guard hold ( state_lock );

		base::copy ( nonce.first, bytes, static_cast < size_t > ( length / 2 ) );
		nonce.first_size = length / 2;
		advance ( );

	}

	void client::write_kaspa_submit ( base::line& request, uint32_t request_id, const char* job_id, uint32_t high, uint32_t low ) {

		char worker [ 256 ];
		uint8_t bytes [ 8 ];

		base::store_big ( bytes, high );
		base::store_big ( bytes + 4, low );
		base::narrow ( worker, sizeof ( worker ), config.worker );
		request.add ( "{\"id\":" ).number ( request_id ).add ( ",\"method\":\"mining.submit\",\"params\":[" );
		base::add_json_string ( request, worker );
		request.put ( ',' );
		base::add_json_string ( request, job_id );
		request.add ( ",\"0x" ).hex ( bytes, 8, false ).add ( "\"]}" );

	}

}
