#include "pool/client.hh"
#include "pool/sv2.hh"
#include "chain/address.hh"
#include "chain/secp256k1.hh"
#include "chain/target.hh"
#include "net/noise.hh"
#include "net/socket.hh"
#include "base/system/clock.hh"
#include "base/bits.hh"
#include "base/memory.hh"
#include "base/strings.hh"
#include "base/text.hh"

namespace pool {

	struct sv2_job {

		bool used = false;
		uint32_t id = 0;
		uint32_t version = 0;
		uint8_t merkle [ 32 ] = { };
		bool timed = false;
		uint32_t time = 0;

	};

	struct sv2_state {

		uint32_t channel = 0;
		bool channel_open = false;
		sv2_job jobs [ 8 ];
		int32_t next_slot = 0;
		bool have_previous = false;
		uint8_t previous [ 32 ] = { };
		uint32_t previous_time = 0;
		uint32_t bits = 0;
		uint32_t active_job = 0;
		uint32_t active_version = 0;
		uint32_t sequence = 0;
		base::list < uint8_t > payload;

	};

	void drop_sv2 ( sv2_state* value ) {

		delete value;

	}

	void sv2_writer::u8 ( uint8_t value ) {

		bytes.push ( value );

	}

	void sv2_writer::u16 ( uint16_t value ) {

		bytes.push ( static_cast < uint8_t > ( value ) );
		bytes.push ( static_cast < uint8_t > ( value >> 8 ) );

	}

	void sv2_writer::u32 ( uint32_t value ) {

		for ( int32_t i = 0; i < 4; i++ )
			bytes.push ( static_cast < uint8_t > ( value >> ( i * 8 ) ) );

	}

	void sv2_writer::u64 ( uint64_t value ) {

		for ( int32_t i = 0; i < 8; i++ )
			bytes.push ( static_cast < uint8_t > ( value >> ( i * 8 ) ) );

	}

	void sv2_writer::f32 ( float value ) {

		uint32_t raw = 0;

		base::copy ( &raw, &value, 4 );
		u32 ( raw );

	}

	void sv2_writer::text ( const char* value ) {

		int32_t size = base::min ( base::length ( value ), 255 );

		u8 ( static_cast < uint8_t > ( size ) );
		bytes.append ( reinterpret_cast < const uint8_t* > ( value ), size );

	}

	void sv2_writer::raw ( const uint8_t* value, int32_t size ) {

		bytes.append ( value, size );

	}

	bool sv2_reader::u8 ( uint8_t& out ) {

		if ( at + 1 > size )
			return false;

		out = data [ at++ ];

		return true;

	}

	bool sv2_reader::u16 ( uint16_t& out ) {

		if ( at + 2 > size )
			return false;

		out = static_cast < uint16_t > ( data [ at ] | data [ at + 1 ] << 8 );
		at += 2;

		return true;

	}

	bool sv2_reader::u32 ( uint32_t& out ) {

		if ( at + 4 > size )
			return false;

		out = base::load_little ( data + at );
		at += 4;

		return true;

	}

	bool sv2_reader::u64 ( uint64_t& out ) {

		if ( at + 8 > size )
			return false;

		out = base::load_little64 ( data + at );
		at += 8;

		return true;

	}

	bool sv2_reader::raw ( uint8_t* out, int32_t count ) {

		if ( at + count > size )
			return false;

		base::copy ( out, data + at, static_cast < size_t > ( count ) );
		at += count;

		return true;

	}

	bool sv2_reader::text ( char* out, int32_t room ) {

		uint8_t length = 0;

		if ( !u8 ( length ) || at + length > size )
			return false;

		base::copy_span ( out, room, reinterpret_cast < const char* > ( data + at ), length );
		at += length;

		return true;

	}

	bool sv2_reader::skip_bytes ( ) {

		uint8_t length = 0;

		if ( !u8 ( length ) || at + length > size )
			return false;

		at += length;

		return true;

	}

	bool split_sv2_address ( const wchar_t* address, wchar_t* host_port, int32_t room, char* key, int32_t key_room ) {

		static const wchar_t scheme [ ] = L"stratum2+tcp://";

		if ( !base::starts_with ( address, scheme ) )
			return false;

		const wchar_t* rest = address + base::length ( scheme );
		int32_t slash = -1;

		for ( int32_t i = 0; rest [ i ]; i++ ) {

			if ( rest [ i ] == L'/' ) {

				slash = i;

				break;

			}

		}

		if ( slash <= 0 || slash >= room )
			return false;

		base::copy_text ( host_port, slash + 1, rest );
		base::narrow ( key, key_room, rest + slash + 1 );

		return key [ 0 ] != 0;

	}

	bool client::sv2_address ( ) const {

		const wchar_t* address = base::atomic_load ( on_backup ) ? config.backup : config.address;

		return base::starts_with ( address, L"stratum2+tcp://" );

	}

	void client::sv2_activate ( int32_t slot ) {

		const sv2_job& work = sv2->jobs [ slot ];
		chain::header header;
		wchar_t name [ 32 ];
		base::writer < char, 24 > id;

		header.set_version ( work.version );
		base::copy ( header.previous ( ), sv2->previous, 32 );
		base::copy ( header.merkle_root ( ), work.merkle, 32 );
		header.set_time ( work.timed ? work.time : sv2->previous_time );
		header.set_bits ( sv2->bits );
		header.set_nonce ( 0 );
		id.number ( work.id );

		{

			base::guard hold ( state_lock );

			sv2_header = header;
			sv2_on = true;
			sv2->active_job = work.id;
			sv2->active_version = work.version;
			base::copy_text ( current.id, sizeof ( current.id ), id.get ( ) );
			current.time = header.time ( );
			current.bits = sv2->bits;
			current.version = work.version;
			have_job = true;
			authorized = true;
			advance ( );
			clean_serial = serial;
			shown.state = LINK_READY;
			shown.jobs++;
			shown.has_job = true;
			shown.version = work.version;
			shown.bits = sv2->bits;
			shown.time = header.time ( );
			base::copy ( shown.previous, sv2->previous, 32 );
			base::widen ( shown.job, base::count_of ( shown.job ), id.get ( ) );

		}

		base::widen ( name, base::count_of ( name ), id.get ( ) );

		base::text line;

		line.add ( L"New job " ).add ( name );
		note ( EVENT_JOB, line.get ( ) );

	}

	bool client::sv2_handle ( uint16_t extension, uint8_t type, const uint8_t* payload, int32_t size, void* wire ) {

		net::noise_link& link = *static_cast < net::noise_link* > ( wire );
		sv2_reader in { payload, size, 0 };
		base::text line;

		if ( extension & 0x7FFF )
			return true;

		if ( type == sv2::SETUP_SUCCESS ) {

			sv2_writer out;
			char worker [ 256 ];
			uint8_t widest [ 32 ];

			base::fill ( widest, 0xFF, 32 );
			base::narrow ( worker, sizeof ( worker ), config.worker );
			out.u32 ( 1 );
			out.text ( worker );
			out.f32 ( 1e9f );
			out.raw ( widest, 32 );
			set_state ( LINK_AUTHORIZING );

			return link.send_frame ( 0, sv2::OPEN_STANDARD_CHANNEL, out.bytes.data ( ), out.bytes.size ( ) );

		}

		if ( type == sv2::SETUP_ERROR || type == sv2::OPEN_CHANNEL_ERROR ) {

			uint32_t skipped = 0;
			char reason [ 128 ] = { };
			wchar_t wide [ 128 ];

			in.u32 ( skipped );
			in.text ( reason, sizeof ( reason ) );
			base::widen ( wide, base::count_of ( wide ), reason );
			line.add ( type == sv2::SETUP_ERROR ? L"The pool refused the connection: " : L"The pool refused worker " ).add ( type == sv2::SETUP_ERROR ? L"" : config.worker ).add ( type == sv2::SETUP_ERROR ? L"" : L": " ).add ( wide );
			note ( EVENT_ERROR, line.get ( ) );
			refused = true;
			set_state ( LINK_REFUSED );

			return false;

		}

		if ( type == sv2::OPEN_STANDARD_SUCCESS ) {

			uint32_t request = 0;
			uint8_t target [ 32 ];

			if ( !in.u32 ( request ) || !in.u32 ( sv2->channel ) || !in.raw ( target, 32 ) )
				return false;

			sv2->channel_open = true;

			{

				base::guard hold ( state_lock );

				base::copy ( sv2_target, target, 32 );
				difficulty = chain::difficulty_of ( chain::top_bits ( target ) );
				next_difficulty = difficulty;
				shown.difficulty = difficulty;

			}

			line.add ( L"Signed in as " ).add ( config.worker ).add ( L" on channel " ).number ( sv2->channel );
			note ( EVENT_INFO, line.get ( ) );

			return true;

		}

		if ( type == sv2::NEW_MINING_JOB ) {

			uint32_t channel = 0;
			uint8_t timed = 0;
			sv2_job work;

			if ( !in.u32 ( channel ) || !in.u32 ( work.id ) || !in.u8 ( timed ) || ( timed && !in.u32 ( work.time ) ) || !in.u32 ( work.version ) || !in.raw ( work.merkle, 32 ) )
				return false;

			work.used = true;
			work.timed = timed != 0;

			int32_t slot = sv2->next_slot++ % static_cast < int32_t > ( base::count_of ( sv2->jobs ) );

			sv2->jobs [ slot ] = work;

			if ( work.timed && sv2->have_previous )
				sv2_activate ( slot );

			return true;

		}

		if ( type == sv2::SET_NEW_PREV_HASH ) {

			uint32_t channel = 0;
			uint32_t job = 0;

			if ( !in.u32 ( channel ) || !in.u32 ( job ) || !in.raw ( sv2->previous, 32 ) || !in.u32 ( sv2->previous_time ) || !in.u32 ( sv2->bits ) )
				return false;

			sv2->have_previous = true;

			for ( int32_t i = 0; i < static_cast < int32_t > ( base::count_of ( sv2->jobs ) ); i++ ) {

				if ( sv2->jobs [ i ].used && sv2->jobs [ i ].id == job ) {

					sv2->jobs [ i ].timed = false;
					sv2_activate ( i );

				}

			}

			return true;

		}

		if ( type == sv2::SET_TARGET ) {

			uint32_t channel = 0;
			uint8_t target [ 32 ];

			if ( !in.u32 ( channel ) || !in.raw ( target, 32 ) )
				return false;

			double value = chain::difficulty_of ( chain::top_bits ( target ) );

			{

				base::guard hold ( state_lock );

				base::copy ( sv2_target, target, 32 );
				difficulty = value;
				next_difficulty = value;
				shown.difficulty = value;
				advance ( );

			}

			line.add ( L"Share difficulty set to " ).compact ( value );
			note ( EVENT_INFO, line.get ( ) );

			return true;

		}

		if ( type == sv2::SUBMIT_SUCCESS ) {

			uint32_t channel = 0;
			uint32_t last = 0;
			uint32_t count = 0;

			if ( !in.u32 ( channel ) || !in.u32 ( last ) || !in.u32 ( count ) )
				return false;

			for ( pending_submit& slot : submits ) {

				if ( slot.id && slot.id - 1 <= last )
					settle ( slot.id, true, L"" );

			}

			return true;

		}

		if ( type == sv2::SUBMIT_ERROR ) {

			uint32_t channel = 0;
			uint32_t sequence = 0;
			char reason [ 128 ] = { };
			wchar_t wide [ 128 ];

			if ( !in.u32 ( channel ) || !in.u32 ( sequence ) )
				return false;

			in.text ( reason, sizeof ( reason ) );
			base::widen ( wide, base::count_of ( wide ), reason );
			settle ( sequence + 1, false, wide );

			return true;

		}

		if ( type == sv2::RECONNECT ) {

			note ( EVENT_INFO, L"The pool asked Alloy to reconnect" );

			return false;

		}

		return true;

	}

	void client::sv2_submit ( void* wire ) {

		net::noise_link& link = *static_cast < net::noise_link* > ( wire );
		share batch [ 64 ];
		int32_t count = 0;

		{

			base::guard hold ( share_lock );

			count = queued_count;
			base::copy ( batch, queued, sizeof ( share ) * static_cast < size_t > ( count ) );
			queued_count = 0;

		}

		for ( int32_t i = 0; i < count && sv2->channel_open; i++ ) {

			const share& item = batch [ i ];
			char id [ 64 ] = { };
			bool known = false;

			{

				base::guard hold ( state_lock );

				for ( const known_job& slot : recent ) {

					if ( slot.serial == item.serial && slot.id [ 0 ] ) {

						base::copy_text ( id, sizeof ( id ), slot.id );
						known = item.serial >= clean_serial;

					}

				}

				if ( !known )
					shown.stale++;

			}

			if ( !known )
				continue;

			uint32_t job = static_cast < uint32_t > ( base::read_number ( id ) );
			uint32_t version = sv2->active_version;

			for ( const sv2_job& slot : sv2->jobs ) {

				if ( slot.used && slot.id == job )
					version = slot.version;

			}

			sv2_writer out;
			uint32_t sequence = sv2->sequence++;

			out.u32 ( sv2->channel );
			out.u32 ( sequence );
			out.u32 ( job );
			out.u32 ( item.nonce );
			out.u32 ( item.time );
			out.u32 ( version );

			pending_submit* slot = &submits [ ( sequence + 1 ) % base::count_of ( submits ) ];

			slot->id = sequence + 1;
			slot->sent = base::seconds ( );
			slot->difficulty = item.difficulty;
			slot->nonce = item.nonce;
			link.send_frame ( sv2::channel_message, sv2::SUBMIT_SHARES_STANDARD, out.bytes.data ( ), out.bytes.size ( ) );

		}

	}

	bool client::sv2_session ( ) {

		const wchar_t* address = base::atomic_load ( on_backup ) ? config.backup : config.address;
		wchar_t host_port [ 256 ];
		char key_text [ 128 ];
		uint8_t authority [ 32 ];
		char host [ 256 ];
		uint16_t port = 0;
		base::text line;

		if ( !split_sv2_address ( address, host_port, base::count_of ( host_port ), key_text, sizeof ( key_text ) ) || !chain::read_authority_key ( key_text, authority ) || !net::split_address ( host_port, host, sizeof ( host ), port ) ) {

			note ( EVENT_ERROR, L"A Stratum V2 address looks like stratum2+tcp://host:port/ followed by the pool's authority key" );
			refused = true;
			set_state ( LINK_REFUSED );

			return false;

		}

		delete sv2;
		sv2 = new sv2_state;
		set_state ( LINK_CONNECTING );
		line.add ( L"Connecting to " ).add ( host_port ).add ( L" with Stratum V2" );
		note ( EVENT_INFO, line.get ( ) );

		net::connection* connection = new net::connection;

		if ( !connection->open ( host, port, config.connect_ms ) ) {

			line.clear ( );
			line.add ( L"Couldn't reach " ).add ( host_port );
			note ( EVENT_WARNING, line.get ( ) );
			delete connection;

			return false;

		}

		net::noise_link* link = new net::noise_link ( *connection );
		net::noise_handshake* handshake = new net::noise_handshake;
		uint8_t first [ net::noise_first_size ];
		uint8_t second [ net::noise_second_size ];
		uint8_t server_x [ 32 ];
		uint8_t signed_message [ 32 ];
		net::certificate signed_by;
		uint32_t now = base::unix_time ( );
		bool shaken = handshake->first ( first ) && connection->send ( reinterpret_cast < const char* > ( first ), sizeof ( first ) ) && link->read_exact ( second, sizeof ( second ), 10000 ) && handshake->second ( second, server_x, signed_by, link->send, link->receive );

		delete handshake;

		if ( shaken ) {

			net::certificate_message ( signed_by, server_x, signed_message );

			if ( !chain::schnorr_verify ( authority, signed_message, signed_by.signature ) || now < signed_by.valid_from || now > signed_by.valid_until ) {

				note ( EVENT_ERROR, L"The pool's certificate isn't signed by the authority key in the address, or has expired, so Alloy won't mine there" );
				refused = true;
				set_state ( LINK_REFUSED );
				delete link;
				delete connection;

				return false;

			}

			note ( EVENT_INFO, L"Encrypted with Noise, and the pool's certificate is signed by its authority key" );

		} else {

			note ( EVENT_WARNING, L"The Stratum V2 handshake with the pool didn't complete" );
			delete link;
			delete connection;

			return false;

		}

		{

			base::guard hold ( state_lock );

			have_job = false;
			authorized = false;
			sv2_on = false;
			base::atomic_add ( serial, 1u );
			shown.state = LINK_SUBSCRIBING;

		}

		sv2_writer setup;
		char narrow_host [ 256 ];

		base::copy_text ( narrow_host, sizeof ( narrow_host ), host );
		setup.u8 ( 0 );
		setup.u16 ( 2 );
		setup.u16 ( 2 );
		setup.u32 ( 1 );
		setup.text ( narrow_host );
		setup.u16 ( port );
		setup.text ( "Alloy" );
		setup.text ( "PC" );
		setup.text ( ALLOY_VERSION_TEXT );
		setup.text ( "" );

		bool worked = false;
		double heard = base::seconds ( );

		if ( link->send_frame ( 0, sv2::SETUP_CONNECTION, setup.bytes.data ( ), setup.bytes.size ( ) ) ) {

			while ( !base::atomic_load ( quit ) && connection->is_open ( ) && !refused && !base::atomic_load ( nudged ) ) {

				uint16_t extension = 0;
				uint8_t type = 0;
				int32_t got = link->read_frame ( extension, type, sv2->payload, 20 );

				if ( got < 0 )
					break;

				if ( got > 0 ) {

					heard = base::seconds ( );

					if ( !sv2_handle ( extension, type, sv2->payload.data ( ), sv2->payload.size ( ), link ) )
						break;

					worked = worked || sv2->channel_open;

				}

				sv2_submit ( link );

				if ( base::seconds ( ) - heard > static_cast < double > ( config.silence_seconds ) ) {

					base::text quiet;

					quiet.add ( L"The pool has been silent for " ).number ( config.silence_seconds / 60 ).add ( config.silence_seconds >= 120 ? L" minutes" : L" minute" );
					note ( EVENT_WARNING, quiet.get ( ) );

					break;

				}

			}

		}

		if ( base::atomic_load ( nudged ) )
			note ( EVENT_INFO, L"Reconnecting to the pool after the network changed" );
		else if ( !base::atomic_load ( quit ) && !refused )
			note ( EVENT_WARNING, L"Lost the connection to the pool" );

		delete link;
		delete connection;

		base::guard hold ( state_lock );

		have_job = false;
		authorized = false;
		sv2_on = false;
		base::atomic_add ( serial, 1u );

		return worked;

	}

}
