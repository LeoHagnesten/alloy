#include "pool/client.hh"
#include "randomx/variant.hh"
#include "net/socket.hh"
#include "base/memory.hh"
#include "base/strings.hh"
#include "base/text.hh"

namespace pool {

	void client::login ( ) {

		char worker [ 256 ];
		char password [ 256 ];
		base::line request;

		base::narrow ( worker, sizeof ( worker ), config.worker );
		base::narrow ( password, sizeof ( password ), config.password );
		request.add ( "{\"id\":1,\"jsonrpc\":\"2.0\",\"method\":\"login\",\"params\":{\"login\":" );
		base::add_json_string ( request, worker );
		request.add ( ",\"pass\":" );
		base::add_json_string ( request, password );
		request.add ( ",\"agent\":\"" ).add ( agent ).add ( "\",\"algo\":[" );

		const randomx::variant& chosen = randomx::variant_of ( config.variant );

		request.put ( '"' ).add ( chosen.key ).put ( '"' );

		for ( uint8_t i = 0; i < randomx::VARIANT_COUNT; i++ ) {

			if ( i != chosen.id )
				request.add ( ",\"" ).add ( randomx::variant_of ( i ).key ).put ( '"' );

		}

		request.add ( "]}}" );
		link->send_line ( request.get ( ) );

	}

	void client::keep_alive ( ) {

		if ( !authorized || !login_id [ 0 ] || base::seconds ( ) - last_keepalive < 60.0 )
			return;

		base::line request;

		last_keepalive = base::seconds ( );
		request.add ( "{\"id\":" ).number ( next_id++ ).add ( ",\"jsonrpc\":\"2.0\",\"method\":\"keepalived\",\"params\":{\"id\":" );
		base::add_json_string ( request, login_id );
		request.add ( "}}" );
		link->send_line ( request.get ( ) );

	}

	void client::take_blob_job ( const base::json& message, int32_t node ) {

		blob_job* incoming = new blob_job;

		if ( !parse_blob_job ( message, node, *incoming ) ) {

			delete incoming;
			note ( EVENT_WARNING, L"The pool sent a job Alloy couldn't read" );

			return;

		}

		wchar_t name [ 64 ];
		double share_difficulty = blob_difficulty ( incoming->target );
		bool reseeded = false;
		bool changed = false;
		bool switched = false;
		int32_t found = incoming->algo [ 0 ] ? randomx::find_variant ( incoming->algo ) : config.variant;

		base::widen ( name, base::count_of ( name ), incoming->id );

		if ( found < 0 ) {

			if ( !base::same_text ( refused_algo, incoming->algo ) ) {

				wchar_t algo [ 16 ];
				base::text line;

				base::copy_text ( refused_algo, sizeof ( refused_algo ), incoming->algo );
				base::widen ( algo, base::count_of ( algo ), incoming->algo );
				line.add ( L"The pool sent work for " ).add ( algo ).add ( L", which Alloy doesn't mine, so it waits for RandomX work" );
				note ( EVENT_WARNING, line.get ( ) );

			}

			base::guard hold ( state_lock );

			have_job = false;
			advance ( );
			delete incoming;

			return;

		}

		refused_algo [ 0 ] = 0;
		incoming->variant = static_cast < uint8_t > ( found );

		if ( incoming->blob [ blob_nonce_at + 3 ] && !nicehash ) {

			nicehash = true;
			note ( EVENT_INFO, L"The pool fixes the top nonce byte, so Alloy searches the other three" );

		}

		{

			base::guard hold ( state_lock );

			shown.nicehash = nicehash;

			switched = have_job && blob_current.variant != incoming->variant;
			reseeded = have_job && !switched && !base::same ( blob_current.seed, incoming->seed, 32 );
			changed = !have_job || share_difficulty != difficulty;
			blob_current = *incoming;
			have_job = true;
			difficulty = share_difficulty;
			next_difficulty = share_difficulty;
			advance ( );
			clean_serial = serial;
			shown.jobs++;
			shown.has_job = true;
			shown.difficulty = difficulty;
			shown.height = incoming->height;
			shown.blob_size = incoming->blob_size;
			shown.variant = incoming->variant;
			base::copy ( shown.seed, incoming->seed, 32 );
			base::copy_text ( shown.job, base::count_of ( shown.job ), name );

		}

		base::text line;

		line.add ( L"New job " ).add ( name );

		if ( incoming->height )
			line.add ( L" at height " ).number ( incoming->height );

		note ( EVENT_JOB, line.get ( ) );

		if ( changed ) {

			line.clear ( );
			line.add ( L"Share difficulty set to " ).compact ( share_difficulty );
			note ( EVENT_INFO, line.get ( ) );

		}

		if ( reseeded )
			note ( EVENT_INFO, L"The RandomX seed changed, so the dataset is being rebuilt" );

		if ( switched ) {

			const randomx::variant& now = randomx::variant_of ( incoming->variant );
			wchar_t key [ 16 ];

			base::widen ( key, base::count_of ( key ), now.key );
			line.clear ( );
			line.add ( L"The pool switched to " ).add ( now.name ).add ( L" (" ).add ( key ).add ( L") for " ).add ( now.coin ).add ( L", so the dataset is being rebuilt" );
			note ( EVENT_INFO, line.get ( ) );

		}

		delete incoming;

	}

	bool client::handle_blob ( const base::json& message ) {

		int32_t method = message.member ( 0, "method" );

		if ( message.kind ( method ) == base::JSON_STRING ) {

			if ( message.text_is ( method, "job" ) )
				take_blob_job ( message, message.member ( 0, "params" ) );

			return true;

		}

		uint32_t number = static_cast < uint32_t > ( message.number ( message.member ( 0, "id" ) ) );
		int32_t result = message.member ( 0, "result" );
		int32_t error = message.member ( 0, "error" );
		char reason [ 128 ] = { };
		wchar_t wide_reason [ 128 ] = { };
		base::text line;

		if ( message.kind ( error ) == base::JSON_OBJECT ) {

			message.text ( message.member ( error, "message" ), reason, sizeof ( reason ) );
			base::widen ( wide_reason, base::count_of ( wide_reason ), reason );

		}

		bool good = message.kind ( error ) != base::JSON_OBJECT && message.kind ( result ) == base::JSON_OBJECT;

		if ( number == 1 ) {

			if ( !good ) {

				line.add ( L"The pool refused worker " ).add ( config.worker );

				if ( wide_reason [ 0 ] )
					line.add ( L": " ).add ( wide_reason );

				note ( EVENT_ERROR, line.get ( ) );
				refused = true;
				set_state ( LINK_REFUSED );

				return false;

			}

			{

				base::guard hold ( state_lock );

				message.text ( message.member ( result, "id" ), login_id, sizeof ( login_id ) );
				authorized = true;
				shown.state = LINK_READY;

			}

			line.add ( L"Signed in as " ).add ( config.worker );
			note ( EVENT_INFO, line.get ( ) );

			int32_t extensions = message.member ( result, "extensions" );

			for ( int32_t i = 0; i < message.count ( extensions ); i++ ) {

				if ( message.text_is ( message.item ( extensions, i ), "nicehash" ) )
					nicehash = true;

			}

			int32_t job = message.member ( result, "job" );

			if ( message.kind ( job ) == base::JSON_OBJECT )
				take_blob_job ( message, job );

			return true;

		}

		int32_t status = message.member ( result, "status" );

		if ( good && message.kind ( status ) == base::JSON_STRING && message.text_is ( status, "KEEPALIVED" ) )
			return true;

		settle ( number, good, wide_reason );

		return true;

	}

}
