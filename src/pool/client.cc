#include "pool/client.hh"
#include "chain/target.hh"
#include "net/socket.hh"
#include "base/bits.hh"
#include "base/memory.hh"
#include "base/strings.hh"
#include "base/text.hh"

namespace pool {

	bool client::start ( const settings& value ) {

		stop ( );

		config = value;
		quit = 0;
		refused = false;
		shown = status { };
		have_job = false;
		authorized = false;
		difficulty = 1.0;
		next_difficulty = 1.0;
		nonce = session_nonce { };
		queued_count = 0;

		for ( pending_submit& slot : submits )
			slot = pending_submit { };

		thread = base::start_thread ( &main, this );

		return thread != nullptr;

	}

	void client::stop ( ) {

		if ( !thread )
			return;

		base::atomic_store ( quit, 1u );
		base::join_thread ( thread );
		thread = nullptr;

		base::guard hold ( state_lock );

		have_job = false;
		authorized = false;
		shown.state = LINK_OFF;
		base::atomic_add ( serial, 1u );

	}

	void client::read ( status& out ) {

		base::guard hold ( state_lock );

		out = shown;

	}

	bool client::next_event ( event& out ) {

		base::guard hold ( event_lock );

		uint64_t pushed = events.pushed ( );
		uint64_t oldest = pushed - static_cast < uint64_t > ( events.size ( ) );

		if ( events_read < oldest )
			events_read = oldest;

		if ( events_read >= pushed )
			return false;

		out = events [ static_cast < int32_t > ( events_read - oldest ) ];
		events_read++;

		return true;

	}

	uint32_t client::job ( ) {

		return base::atomic_load ( serial );

	}

	bool client::fill ( uint64_t index, mine::unit& out ) {

		base::guard hold ( state_lock );

		if ( !have_job || !authorized )
			return false;

		out.algorithm = config.algorithm;
		out.index = index;
		out.job = serial;
		out.first = 0;
		out.end = mine::nonce_space;

		if ( blob_mode ( ) ) {

			if ( index )
				return false;

			base::copy ( out.blob, blob_current.blob, static_cast < size_t > ( blob_current.blob_size ) );
			out.blob_size = static_cast < uint32_t > ( blob_current.blob_size );
			out.nonce_at = blob_nonce_at;
			base::copy ( out.seed, blob_current.seed, 32 );
			chain::target_of_top ( blob_current.target, out.target );

			return true;

		}

		uint8_t second [ extranonce_room ];

		counter_bytes ( index, second, nonce.second_size );
		build_header ( current, nonce, second, current.time, out.header );
		chain::target_for ( difficulty, out.target, config.algorithm );

		return true;

	}

	bool client::found ( const mine::unit& from, uint32_t nonce_value, const uint8_t* digest ) {

		base::guard hold ( share_lock );

		if ( queued_count < base::count_of ( queued ) ) {

			share& item = queued [ queued_count++ ];

			item.serial = from.job;
			item.second = from.index;
			item.time = from.header.time ( );
			item.nonce = nonce_value;
			item.difficulty = chain::difficulty_of ( chain::top_bits ( digest ), config.algorithm );
			base::copy ( item.result, digest, 32 );

		}

		return true;

	}

	unsigned long client::main ( void* context ) {

		static_cast < client* > ( context )->run ( );

		return 0;

	}

	void client::note ( event_kind kind, const wchar_t* text, double share_difficulty, double latency, uint32_t nonce_value ) {

		event item;

		item.kind = kind;
		item.time = base::local_time ( );
		item.difficulty = share_difficulty;
		item.latency = latency;
		item.nonce = nonce_value;
		base::copy_text ( item.text, base::count_of ( item.text ), text );

		base::guard hold ( event_lock );

		events.push ( item );

	}

	void client::advance ( ) {

		base::atomic_store ( serial, serial + 1 );

		if ( !have_job )
			return;

		known_job& slot = recent [ recent_next++ % base::count_of ( recent ) ];

		slot.serial = serial;
		base::copy_text ( slot.id, sizeof ( slot.id ), blob_mode ( ) ? blob_current.id : current.id );
		slot.time = current.time;
		slot.difficulty = difficulty;

	}

	void client::set_state ( link_state value ) {

		base::guard hold ( state_lock );

		shown.state = value;

	}

	void client::pause ( uint32_t seconds ) {

		for ( uint32_t i = 0; i < seconds * 10 && !base::atomic_load ( quit ); i++ )
			base::sleep ( 100 );

	}

	void client::run ( ) {

		uint32_t backoff = 2;

		while ( !base::atomic_load ( quit ) ) {

			double began = base::seconds ( );

			session ( );

			if ( base::atomic_load ( quit ) || refused )
				break;

			if ( base::seconds ( ) - began > 30.0 )
				backoff = 2;

			base::text message;

			message.add ( L"Trying again in " ).number ( backoff ).add ( L" s" );
			note ( EVENT_INFO, message.get ( ) );
			set_state ( LINK_WAITING );
			pause ( backoff );
			backoff = base::min ( backoff * 2, 60u );

		}

		while ( refused && !base::atomic_load ( quit ) )
			base::sleep ( 100 );

	}

	void client::session ( ) {

		char host [ 256 ];
		uint16_t port = 0;
		base::text message;

		if ( !net::split_address ( config.address, host, sizeof ( host ), port ) ) {

			note ( EVENT_ERROR, L"The pool address needs a host and a port, like pool.example.com:3333" );
			refused = true;
			set_state ( LINK_REFUSED );

			return;

		}

		set_state ( LINK_CONNECTING );
		message.add ( L"Connecting to " ).add ( config.address );
		note ( EVENT_INFO, message.get ( ) );

		net::connection* connection = new net::connection;

		if ( !connection->open ( host, port, 5000 ) ) {

			message.clear ( );
			message.add ( L"Couldn't reach " ).add ( config.address );
			note ( EVENT_WARNING, message.get ( ) );
			delete connection;

			return;

		}

		{

			base::guard hold ( state_lock );

			have_job = false;
			authorized = false;
			nonce = session_nonce { };
			login_id [ 0 ] = 0;
			base::atomic_add ( serial, 1u );
			shown.state = blob_mode ( ) ? LINK_AUTHORIZING : LINK_SUBSCRIBING;

		}

		link = connection;
		parser = new base::json;

		if ( blob_mode ( ) )
			login ( );
		else
			subscribe ( );

		char* text = new char [ net::line_room ];
		double heard = base::seconds ( );

		last_keepalive = heard;

		while ( !base::atomic_load ( quit ) && connection->is_open ( ) && !refused ) {

			int32_t size = connection->read_line ( text, net::line_room, 20 );

			if ( size > 0 ) {

				handle ( text, size );
				heard = base::seconds ( );

			}

			submit_shares ( );

			if ( blob_mode ( ) )
				keep_alive ( );

			if ( base::seconds ( ) - heard > 300.0 ) {

				note ( EVENT_WARNING, L"The pool has been silent for five minutes" );

				break;

			}

		}

		if ( !base::atomic_load ( quit ) && !refused )
			note ( EVENT_WARNING, L"Lost the connection to the pool" );

		delete [ ] text;
		delete parser;
		parser = nullptr;
		link = nullptr;
		delete connection;

		base::guard hold ( state_lock );

		have_job = false;
		authorized = false;
		base::atomic_add ( serial, 1u );

	}

	void client::subscribe ( ) {

		base::line request;

		request.add ( "{\"id\":1,\"method\":\"mining.subscribe\",\"params\":[\"" ).add ( agent ).add ( "\"]}" );
		link->send_line ( request.get ( ) );

	}

	bool client::handle ( const char* text, int32_t size ) {

		base::json& message = *parser;
		base::text line;

		if ( !message.parse ( text, size ) ) {

			note ( EVENT_WARNING, L"The pool sent a line that isn't JSON" );

			return false;

		}

		if ( blob_mode ( ) )
			return handle_blob ( message );

		int32_t method = message.member ( 0, "method" );
		int32_t params = message.member ( 0, "params" );
		int32_t id = message.member ( 0, "id" );

		if ( message.kind ( method ) == base::JSON_STRING ) {

			if ( message.text_is ( method, "mining.notify" ) ) {

				pool::job* incoming = new pool::job;

				if ( !parse_notify ( message, params, *incoming ) ) {

					delete incoming;
					note ( EVENT_WARNING, L"The pool sent a job Alloy couldn't read" );

					return false;

				}

				bool clean = incoming->clean;
				bool replaced = false;
				wchar_t name [ 64 ];

				base::widen ( name, base::count_of ( name ), incoming->id );

				{

					base::guard hold ( state_lock );

					replaced = have_job;
					current = *incoming;
					have_job = true;
					difficulty = next_difficulty;
					advance ( );

					if ( clean )
						clean_serial = serial;

					shown.jobs++;
					shown.has_job = true;
					shown.difficulty = difficulty;
					shown.version = incoming->version;
					shown.bits = incoming->bits;
					shown.time = incoming->time;
					shown.branches = incoming->branch_count;
					shown.coinbase_size = incoming->head_size + incoming->tail_size + nonce.first_size + nonce.second_size;
					base::copy ( shown.previous, incoming->previous, 32 );
					base::copy_text ( shown.job, base::count_of ( shown.job ), name );

				}

				delete incoming;
				line.add ( L"New job " ).add ( name );

				if ( clean && replaced )
					line.add ( L", earlier work dropped" );

				note ( EVENT_JOB, line.get ( ) );

				return true;

			}

			if ( message.text_is ( method, "mining.set_difficulty" ) ) {

				double value = message.number ( message.item ( params, 0 ) );

				if ( value > 0.0 ) {

					bool changed = false;

					{

						base::guard hold ( state_lock );

						changed = value != next_difficulty || !have_job;
						next_difficulty = value;

						if ( !have_job ) {

							difficulty = value;
							shown.difficulty = value;

						}

					}

					if ( changed ) {

						line.add ( L"Share difficulty set to " ).compact ( value );
						note ( EVENT_INFO, line.get ( ) );

					}

				}

				return true;

			}

			if ( message.text_is ( method, "mining.set_extranonce" ) ) {

				uint8_t bytes [ extranonce_room ];
				int32_t node = message.item ( params, 0 );
				int32_t length = message.raw_size ( node );

				if ( length % 2 == 0 && length / 2 <= extranonce_room && base::read_hex ( message.raw ( node ), length, bytes, static_cast < size_t > ( length / 2 ) ) ) {

					base::guard hold ( state_lock );

					base::copy ( nonce.first, bytes, static_cast < size_t > ( length / 2 ) );
					nonce.first_size = length / 2;
					nonce.second_size = base::clamp ( static_cast < int32_t > ( message.number ( message.item ( params, 1 ) ) ), 1, 8 );

				}

				return true;

			}

			if ( message.text_is ( method, "client.reconnect" ) ) {

				note ( EVENT_INFO, L"The pool asked Alloy to reconnect" );
				link->close ( );

				return true;

			}

			if ( message.text_is ( method, "client.show_message" ) ) {

				char said [ 256 ];
				wchar_t wide [ 256 ];

				message.text ( message.item ( params, 0 ), said, sizeof ( said ) );
				base::widen ( wide, base::count_of ( wide ), said );
				line.add ( L"Pool says: " ).add ( wide );
				note ( EVENT_INFO, line.get ( ) );

				return true;

			}

			if ( message.text_is ( method, "client.get_version" ) ) {

				base::line reply;

				reply.add ( "{\"id\":" ).number ( static_cast < uint64_t > ( message.number ( id ) ) ).add ( ",\"result\":\"" ).add ( agent ).add ( "\",\"error\":null}" );
				link->send_line ( reply.get ( ) );

			}

			return true;

		}

		uint32_t number = static_cast < uint32_t > ( message.number ( id ) );
		int32_t result = message.member ( 0, "result" );
		int32_t error = message.member ( 0, "error" );
		char reason [ 128 ] = { };
		wchar_t wide_reason [ 128 ] = { };

		if ( message.kind ( error ) == base::JSON_ARRAY ) {

			message.text ( message.item ( error, 1 ), reason, sizeof ( reason ) );
			base::widen ( wide_reason, base::count_of ( wide_reason ), reason );

		}

		if ( number == 1 ) {

			int32_t first = message.item ( result, 1 );
			int32_t length = message.raw_size ( first );
			uint8_t bytes [ extranonce_room ];

			if ( message.kind ( result ) != base::JSON_ARRAY || length % 2 || length / 2 > extranonce_room || !base::read_hex ( message.raw ( first ), length, bytes, static_cast < size_t > ( length / 2 ) ) ) {

				note ( EVENT_ERROR, L"The pool rejected the subscription" );
				link->close ( );

				return false;

			}

			{

				base::guard hold ( state_lock );

				base::copy ( nonce.first, bytes, static_cast < size_t > ( length / 2 ) );
				nonce.first_size = length / 2;
				nonce.second_size = base::clamp ( static_cast < int32_t > ( message.number ( message.item ( result, 2 ) ) ), 1, 8 );
				shown.state = LINK_AUTHORIZING;

			}

			char worker [ 256 ];
			char password [ 256 ];
			base::line request;

			base::narrow ( worker, sizeof ( worker ), config.worker );
			base::narrow ( password, sizeof ( password ), config.password );
			request.add ( "{\"id\":2,\"method\":\"mining.authorize\",\"params\":[" );
			base::add_json_string ( request, worker );
			request.put ( ',' );
			base::add_json_string ( request, password );
			request.add ( "]}" );
			link->send_line ( request.get ( ) );

			return true;

		}

		if ( number == 2 ) {

			if ( message.truth ( result ) ) {

				{

					base::guard hold ( state_lock );

					authorized = true;
					shown.state = LINK_READY;
					advance ( );

				}

				line.add ( L"Signed in as " ).add ( config.worker );
				note ( EVENT_INFO, line.get ( ) );

				return true;

			}

			line.add ( L"The pool refused worker " ).add ( config.worker );

			if ( wide_reason [ 0 ] )
				line.add ( L": " ).add ( wide_reason );

			note ( EVENT_ERROR, line.get ( ) );
			refused = true;
			set_state ( LINK_REFUSED );

			return false;

		}

		settle ( number, message.truth ( result ), wide_reason );

		return true;

	}

	void client::settle ( uint32_t number, bool accepted, const wchar_t* reason ) {

		for ( pending_submit& slot : submits ) {

			if ( slot.id != number || !number )
				continue;

			double latency = ( base::seconds ( ) - slot.sent ) * 1000.0;
			base::text line;

			{

				base::guard hold ( state_lock );

				if ( accepted )
					shown.accepted++;
				else
					shown.rejected++;

				shown.latency = latency;

			}

			if ( accepted )
				line.add ( L"Share accepted" );
			else
				line.add ( L"Share rejected" ).add ( reason [ 0 ] ? L": " : L"" ).add ( reason );

			note ( accepted ? EVENT_ACCEPTED : EVENT_REJECTED, line.get ( ), slot.difficulty, latency, slot.nonce );
			slot = pending_submit { };

			return;

		}

	}

	void client::submit_shares ( ) {

		share batch [ 64 ];
		int32_t count = 0;

		{

			base::guard hold ( share_lock );

			count = queued_count;
			base::copy ( batch, queued, sizeof ( share ) * static_cast < size_t > ( count ) );
			queued_count = 0;

		}

		for ( int32_t i = 0; i < count && link && link->is_open ( ); i++ ) {

			const share& item = batch [ i ];
			char id [ 64 ] = { };
			char second_hex [ extranonce_room * 2 + 1 ];
			uint8_t second [ extranonce_room ];
			int32_t second_size = 0;
			bool known = false;

			{

				base::guard hold ( state_lock );

				for ( const known_job& slot : recent ) {

					if ( slot.serial == item.serial && slot.id [ 0 ] ) {

						base::copy_text ( id, sizeof ( id ), slot.id );
						known = item.serial >= clean_serial;

					}

				}

				second_size = nonce.second_size;

				if ( !known )
					shown.stale++;

			}

			if ( !known )
				continue;

			base::line request;
			uint32_t request_id = next_id++;

			if ( blob_mode ( ) ) {

				uint8_t nonce_bytes [ 4 ];

				base::store_little ( nonce_bytes, item.nonce );
				request.add ( "{\"id\":" ).number ( request_id ).add ( ",\"jsonrpc\":\"2.0\",\"method\":\"submit\",\"params\":{\"id\":" );
				base::add_json_string ( request, login_id );
				request.add ( ",\"job_id\":" );
				base::add_json_string ( request, id );
				request.add ( ",\"nonce\":\"" ).hex ( nonce_bytes, 4, false ).add ( "\",\"result\":\"" ).hex ( item.result, 32, false ).add ( "\"}}" );

			} else {

				char worker [ 256 ];

				counter_bytes ( item.second, second, second_size );
				base::write_hex ( second_hex, second, static_cast < size_t > ( second_size ) );
				base::narrow ( worker, sizeof ( worker ), config.worker );
				request.add ( "{\"id\":" ).number ( request_id ).add ( ",\"method\":\"mining.submit\",\"params\":[" );
				base::add_json_string ( request, worker );
				request.put ( ',' );
				base::add_json_string ( request, id );
				request.add ( ",\"" ).add ( second_hex ).add ( "\",\"" ).hex32 ( item.time ).add ( "\",\"" ).hex32 ( item.nonce ).add ( "\"]}" );

			}

			pending_submit* slot = &submits [ request_id % base::count_of ( submits ) ];

			slot->id = request_id;
			slot->sent = base::seconds ( );
			slot->difficulty = item.difficulty;
			slot->nonce = item.nonce;
			link->send_line ( request.get ( ) );

		}

	}

}
