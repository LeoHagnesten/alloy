#include "pool/client.hh"
#include "chain/target.hh"
#include "chain/coinbase.hh"
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
		on_backup = 0;
		nudged = 0;
		refused = false;
		nicehash = false;
		refused_algo [ 0 ] = 0;
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

			if ( nicehash ) {

				out.first = static_cast < uint32_t > ( blob_current.blob [ blob_nonce_at + 3 ] ) << 24;
				out.end = static_cast < uint64_t > ( out.first ) + ( 1ull << 24 );

			}
			base::copy ( out.seed, blob_current.seed, 32 );
			out.variant = blob_current.variant;
			chain::target_of_top ( blob_current.target, out.target );

			return true;

		}

		if ( kaspa_mode ( ) ) {

			if ( !kaspa_nonce_high ( nonce.first, nonce.first_size, index, out.nonce_high ) )
				return false;

			base::copy ( out.blob, kaspa_current.pre_pow, 32 );
			out.blob_size = 32;
			out.timestamp = kaspa_current.timestamp;
			chain::target_for ( difficulty, out.target, config.algorithm );

			return true;

		}

		if ( sv2_on ) {

			if ( index > 0xFFFF )
				return false;

			out.header = sv2_header;
			out.header.set_time ( sv2_header.time ( ) + static_cast < uint32_t > ( index ) );
			base::copy ( out.target, sv2_target, 32 );

			return true;

		}

		uint8_t second [ extranonce_room ];

		counter_bytes ( index, second, nonce.second_size );
		build_header ( current, nonce, second, current.time, out.header );

		if ( solo_on )
			chain::target_of ( current.bits, out.target );
		else
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
			item.high = from.nonce_high;
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
		base::copy_text ( slot.id, sizeof ( slot.id ), blob_mode ( ) ? blob_current.id : ( kaspa_mode ( ) ? kaspa_current.id : current.id ) );
		slot.time = current.time;
		slot.difficulty = difficulty;

	}

	void client::set_state ( link_state value ) {

		base::guard hold ( state_lock );

		shown.state = value;

	}

	void client::pause ( uint32_t seconds ) {

		for ( uint32_t i = 0; i < seconds * 10 && !base::atomic_load ( quit ) && !base::atomic_load ( nudged ); i++ )
			base::sleep ( 100 );

	}

	void client::reconnect ( ) {

		base::atomic_store ( nudged, 1u );

	}

	void client::run ( ) {

		uint32_t backoff = 2;
		uint32_t failures = 0;
		double backup_since = 0.0;
		bool has_backup = config.backup [ 0 ] != 0;

		while ( !base::atomic_load ( quit ) ) {

			double began = base::seconds ( );
			bool worked = solo_address ( ) ? solo_session ( ) : ( sv2_address ( ) ? sv2_session ( ) : session ( ) );

			if ( base::atomic_load ( quit ) || ( refused && !has_backup ) )
				break;

			if ( base::atomic_exchange ( nudged, 0u ) ) {

				backoff = 2;
				failures = 0;

				continue;

			}

			bool lasted = base::seconds ( ) - began > 30.0;

			if ( lasted )
				backoff = 2;

			failures = worked && lasted ? 0 : failures + 1;

			base::text message;
			bool backup = base::atomic_load ( on_backup ) != 0;

			if ( has_backup && !backup && ( failures >= config.failover_tries || refused ) ) {

				refused = false;
				failures = 0;
				backoff = 2;
				backup_since = base::seconds ( );
				base::atomic_store ( on_backup, 1u );
				message.add ( L"Switching to the backup pool " ).add ( config.backup );
				note ( EVENT_WARNING, message.get ( ) );

				continue;

			}

			if ( backup && ( ( config.failback_seconds && base::seconds ( ) - backup_since > static_cast < double > ( config.failback_seconds ) ) || failures >= config.failover_tries || refused ) ) {

				refused = false;
				failures = 0;
				backoff = 2;
				base::atomic_store ( on_backup, 0u );
				message.add ( L"Going back to the main pool " ).add ( config.address );
				note ( EVENT_INFO, message.get ( ) );

				continue;

			}

			message.add ( L"Trying again in " ).number ( backoff ).add ( L" s" );
			note ( EVENT_INFO, message.get ( ) );
			set_state ( LINK_WAITING );
			pause ( backoff );
			backoff = base::atomic_exchange ( nudged, 0u ) ? 2u : base::min ( backoff * 2, base::max ( config.retry_limit, 2u ) );

		}

		while ( refused && !base::atomic_load ( quit ) )
			base::sleep ( 100 );

	}

	bool client::session ( ) {

		char host [ 256 ];
		uint16_t port = 0;
		bool secure = false;
		base::text message;
		const wchar_t* address = base::atomic_load ( on_backup ) ? config.backup : config.address;

		{

			base::guard hold ( state_lock );

			shown.on_backup = base::atomic_load ( on_backup ) != 0;

		}

		if ( !net::split_address ( address, host, sizeof ( host ), port, &secure ) ) {

			note ( EVENT_ERROR, L"The pool address needs a host and a port, like pool.example.com:3333" );
			refused = true;
			set_state ( LINK_REFUSED );

			return false;

		}

		set_state ( LINK_CONNECTING );
		message.add ( L"Connecting to " ).add ( address );
		note ( EVENT_INFO, message.get ( ) );

		net::connection* connection = new net::connection;

		if ( !connection->open ( host, port, config.connect_ms ) ) {

			message.clear ( );
			message.add ( L"Couldn't reach " ).add ( address );
			note ( EVENT_WARNING, message.get ( ) );
			delete connection;

			return false;

		}

		if ( secure ) {

			bool backup = base::atomic_load ( on_backup ) != 0;
			char* pin = backup ? config.backup_pin : config.pin;

			if ( !connection->start_tls ( host, 8000 ) ) {

				message.clear ( );
				message.add ( L"No secure connection to " ).add ( address ).add ( L": " ).add ( connection->tls_problem ( ) );
				note ( EVENT_WARNING, message.get ( ) );
				delete connection;

				return false;

			}

			char print [ 65 ];
			wchar_t shown_print [ 16 ];

			base::write_hex ( print, connection->tls_fingerprint ( ), 32 );
			print [ 64 ] = 0;
			base::widen ( shown_print, base::count_of ( shown_print ), print, 12 );
			message.clear ( );

			if ( connection->tls_trusted ( ) )
				message.add ( L"Encrypted with " ).add ( connection->tls_protocol ( ) );
			else if ( !pin [ 0 ] ) {

				base::copy_text ( pin, 65, print );

				{

					base::guard hold ( state_lock );

					base::copy_text ( shown.learned_pin, sizeof ( shown.learned_pin ), print );
					shown.learned_backup = backup;
					shown.pin_serial++;

				}

				message.add ( L"Encrypted with " ).add ( connection->tls_protocol ( ) ).add ( L". The pool's certificate isn't one Windows trusts, so Alloy pinned it (" ).add ( shown_print ).add ( L"...) and will stop if it changes" );

			} else if ( base::same_text ( pin, print ) )
				message.add ( L"Encrypted with " ).add ( connection->tls_protocol ( ) ).add ( L", the pinned certificate matches" );
			else {

				message.add ( L"The pool's certificate changed since Alloy pinned it, so Alloy won't mine there. Change the pool address to start over if you expected this" );
				note ( EVENT_ERROR, message.get ( ) );
				delete connection;

				return false;

			}

			note ( EVENT_INFO, message.get ( ) );

		}

		{

			base::guard hold ( state_lock );

			have_job = false;
			authorized = false;
			solo_on = false;
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

		while ( !base::atomic_load ( quit ) && connection->is_open ( ) && !refused && !base::atomic_load ( nudged ) ) {

			int32_t size = connection->read_line ( text, net::line_room, 20 );

			if ( size > 0 ) {

				handle ( text, size );
				heard = base::seconds ( );

			}

			submit_shares ( );

			if ( blob_mode ( ) )
				keep_alive ( );

			if ( base::seconds ( ) - heard > static_cast < double > ( config.silence_seconds ) ) {

				base::text quiet;

				quiet.add ( L"The pool has been silent for " ).number ( config.silence_seconds / 60 ).add ( config.silence_seconds >= 120 ? L" minutes" : L" minute" );
				note ( EVENT_WARNING, quiet.get ( ) );

				break;

			}

		}

		if ( base::atomic_load ( nudged ) )
			note ( EVENT_INFO, L"Reconnecting to the pool after the network changed" );
		else if ( !base::atomic_load ( quit ) && !refused )
			note ( EVENT_WARNING, L"Lost the connection to the pool" );

		delete [ ] text;
		delete parser;
		parser = nullptr;
		link = nullptr;
		delete connection;

		base::guard hold ( state_lock );

		bool worked = authorized;

		have_job = false;
		authorized = false;
		base::atomic_add ( serial, 1u );

		return worked;

	}

	void client::subscribe ( ) {

		base::line request;

		request.add ( "{\"id\":1,\"method\":\"mining.subscribe\",\"params\":[\"" ).add ( agent ).add ( kaspa_mode ( ) ? "\",\"EthereumStratum/1.0.0\"]}" : "\"]}" );
		link->send_line ( request.get ( ) );

	}

	void client::authorize ( ) {

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

		if ( config.difficulty > 0.0 ) {

			request.clear ( );
			request.add ( "{\"id\":3,\"method\":\"mining.suggest_difficulty\",\"params\":[" ).decimal ( config.difficulty, 6 ).add ( "]}" );
			link->send_line ( request.get ( ) );

		}

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

			if ( kaspa_mode ( ) && message.text_is ( method, "mining.notify" ) ) {

				take_kaspa_job ( message, params );

				return true;

			}

			if ( kaspa_mode ( ) && ( message.text_is ( method, "set_extranonce" ) || message.text_is ( method, "mining.set_extranonce" ) ) ) {

				take_kaspa_extranonce ( message, params );

				return true;

			}

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

					uint8_t* whole = new uint8_t [ coinbase_room * 2 + extranonce_room * 2 ];
					int32_t used = 0;
					uint64_t value = 0;

					base::copy ( whole, incoming->head, static_cast < size_t > ( incoming->head_size ) );
					used += incoming->head_size;
					base::copy ( whole + used, nonce.first, static_cast < size_t > ( nonce.first_size ) );
					used += nonce.first_size;
					base::zero ( whole + used, static_cast < size_t > ( nonce.second_size ) );
					used += nonce.second_size;
					base::copy ( whole + used, incoming->tail, static_cast < size_t > ( incoming->tail_size ) );
					used += incoming->tail_size;
					shown.reward = chain::coinbase_value ( whole, used, value ) ? value : 0;
					delete [ ] whole;
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

		if ( number == 1 && kaspa_mode ( ) ) {

			if ( message.kind ( error ) == base::JSON_ARRAY || message.kind ( error ) == base::JSON_OBJECT || ( message.kind ( result ) != base::JSON_ARRAY && !message.truth ( result ) ) ) {

				note ( EVENT_ERROR, L"The pool rejected the subscription" );
				link->close ( );

				return false;

			}

			set_state ( LINK_AUTHORIZING );
			authorize ( );

			return true;

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

			authorize ( );

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

		if ( number == 3 ) {

			if ( message.kind ( error ) == base::JSON_ARRAY || message.kind ( error ) == base::JSON_OBJECT )
				note ( EVENT_INFO, L"The pool turned down the suggested start difficulty and keeps its own" );

			return true;

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

			} else if ( kaspa_mode ( ) )
				write_kaspa_submit ( request, request_id, id, item.high, item.nonce );
			else {

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
