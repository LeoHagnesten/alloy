#include "pool/client.hh"
#include "chain/address.hh"
#include "chain/coins.hh"
#include "chain/target.hh"
#include "hash/sha256.hh"
#include "net/http.hh"
#include "base/system/files.hh"
#include "base/bits.hh"
#include "base/memory.hh"
#include "base/strings.hh"
#include "base/text.hh"

namespace pool {

	struct solo_state {

		char url [ 512 ] = { };
		char auth [ 400 ] = { };
		base::list < char > transactions;
		int32_t transaction_count = 0;
		bool witness = false;
		uint64_t height = 0;
		char best [ 65 ] = { };
		base::list < uint8_t > blob;
		uint64_t difficulty = 0;
		double fetched = -100.0;
		double checked = -100.0;
		bool stale = true;
		uint32_t made = 0;
		base::json parsed;
		net::http_reply reply;

	};

	static void add_varint ( base::list < uint8_t >& out, uint64_t value ) {

		if ( value < 0xFD )
			out.push ( static_cast < uint8_t > ( value ) );
		else if ( value <= 0xFFFF ) {

			out.push ( 0xFD );
			out.push ( static_cast < uint8_t > ( value ) );
			out.push ( static_cast < uint8_t > ( value >> 8 ) );

		} else {

			out.push ( 0xFE );

			for ( int32_t i = 0; i < 4; i++ )
				out.push ( static_cast < uint8_t > ( value >> ( i * 8 ) ) );

		}

	}

	static void add_hex ( base::list < char >& out, const uint8_t* bytes, size_t count ) {

		static const char digits [ ] = "0123456789abcdef";

		for ( size_t i = 0; i < count; i++ ) {

			out.push ( digits [ bytes [ i ] >> 4 ] );
			out.push ( digits [ bytes [ i ] & 15 ] );

		}

	}

	static void add_text ( base::list < char >& out, const char* text ) {

		out.append ( text, base::length ( text ) );

	}

	static bool meets_difficulty ( const uint8_t* digest, uint64_t difficulty ) {

		unsigned __int128 carry = 0;

		for ( int32_t i = 0; i < 4; i++ ) {

			carry += static_cast < unsigned __int128 > ( base::load_little64 ( digest + i * 8 ) ) * difficulty;
			carry >>= 64;

		}

		return carry == 0;

	}

	static void height_push ( uint64_t height, base::list < uint8_t >& out ) {

		if ( height == 0 ) {

			out.push ( 0x00 );

			return;

		}

		if ( height <= 16 ) {

			out.push ( static_cast < uint8_t > ( 0x50 + height ) );

			return;

		}

		uint8_t bytes [ 9 ];
		int32_t count = 0;

		for ( uint64_t rest = height; rest; rest >>= 8 )
			bytes [ count++ ] = static_cast < uint8_t > ( rest );

		if ( bytes [ count - 1 ] & 0x80 )
			bytes [ count++ ] = 0;

		out.push ( static_cast < uint8_t > ( count ) );
		out.append ( bytes, count );

	}

	client::~client ( ) {

		stop ( );
		delete solo;
		drop_sv2 ( sv2 );

	}

	bool client::solo_address ( ) const {

		const wchar_t* address = base::atomic_load ( on_backup ) ? config.backup : config.address;

		return base::starts_with ( address, L"http://" ) || base::starts_with ( address, L"https://" );

	}

	bool client::node_call ( const char* body, int32_t size, int32_t& result ) {

		base::text line;
		base::line headers;

		headers.add ( "Content-Type: application/json\r\n" ).add ( solo->auth );
		solo->reply.body.clear ( );

		if ( !net::http_request ( "POST", solo->url, headers.get ( ), body, size, solo->reply, 20000 ) ) {

			wchar_t where [ 512 ];

			base::widen ( where, base::count_of ( where ), solo->url );
			line.add ( L"The node at " ).add ( where ).add ( L" didn't answer" );
			note ( EVENT_WARNING, line.get ( ) );

			return false;

		}

		if ( solo->reply.status == 401 || solo->reply.status == 403 ) {

			note ( EVENT_ERROR, L"The node refused the RPC login. Put user:password in Password, or leave Password empty to use the node's cookie file" );
			refused = true;
			set_state ( LINK_REFUSED );

			return false;

		}

		int32_t length = solo->reply.body.size ( );

		while ( length && !solo->reply.body.data ( ) [ length - 1 ] )
			length--;

		if ( !solo->parsed.parse ( solo->reply.body.data ( ), length ) ) {

			note ( EVENT_WARNING, L"The node sent an answer that isn't JSON" );

			return false;

		}

		int32_t error = solo->parsed.member ( 0, "error" );

		if ( solo->parsed.kind ( error ) == base::JSON_OBJECT ) {

			char said [ 160 ];
			wchar_t wide [ 160 ];

			solo->parsed.text ( solo->parsed.member ( error, "message" ), said, sizeof ( said ) );
			base::widen ( wide, base::count_of ( wide ), said );
			line.add ( L"The node says: " ).add ( wide );
			note ( EVENT_WARNING, line.get ( ) );

			return false;

		}

		result = solo->parsed.member ( 0, "result" );

		return true;

	}

	bool client::fetch_template ( ) {

		base::json& message = solo->parsed;
		int32_t result = -1;
		double now = base::seconds ( );

		if ( !solo->stale && now - solo->fetched < 30.0 ) {

			if ( now - solo->checked < 2.0 )
				return true;

			static const char ask [ ] = "{\"jsonrpc\":\"1.0\",\"id\":\"alloy\",\"method\":\"getbestblockhash\",\"params\":[]}";

			solo->checked = now;

			if ( !node_call ( ask, base::length ( ask ), result ) )
				return false;

			if ( message.raw_size ( result ) == 64 && base::same_span ( message.raw ( result ), 64, solo->best ) )
				return true;

		}

		const chain::coin_info& mined = chain::coin_of ( config.coin );
		const char* ask = config.coin == chain::COIN_LITECOIN ? "{\"jsonrpc\":\"1.0\",\"id\":\"alloy\",\"method\":\"getblocktemplate\",\"params\":[{\"rules\":[\"mweb\",\"segwit\"]}]}" : ( mined.segwit ? "{\"jsonrpc\":\"1.0\",\"id\":\"alloy\",\"method\":\"getblocktemplate\",\"params\":[{\"rules\":[\"segwit\"]}]}" : "{\"jsonrpc\":\"1.0\",\"id\":\"alloy\",\"method\":\"getblocktemplate\",\"params\":[{}]}" );

		solo->fetched = now;
		solo->checked = now;

		if ( !node_call ( ask, base::length ( ask ), result ) )
			return false;

		if ( message.kind ( result ) != base::JSON_OBJECT ) {

			note ( EVENT_WARNING, L"The node sent a block template Alloy couldn't read" );

			return false;

		}

		if ( message.kind ( message.member ( result, "mweb" ) ) == base::JSON_STRING ) {

			note ( EVENT_ERROR, L"This Litecoin node wants MWEB blocks, which Alloy can't build yet. Mine Litecoin through a pool instead" );
			refused = true;
			set_state ( LINK_REFUSED );

			return false;

		}

		uint8_t script [ 64 ];
		int32_t script_size = 0;

		if ( !chain::payout_script ( config.coin, config.worker, script, sizeof ( script ), script_size ) ) {

			base::text problem;

			problem.add ( L"Solo mining pays the block reward to the address in Worker, and that isn't a valid " ).add ( mined.name ).add ( L" address" );
			note ( EVENT_ERROR, problem.get ( ) );
			refused = true;
			set_state ( LINK_REFUSED );

			return false;

		}

		int32_t previous = message.member ( result, "previousblockhash" );
		int32_t list = message.member ( result, "transactions" );
		int32_t commitment = message.member ( result, "default_witness_commitment" );
		uint64_t height = base::read_number ( message.raw ( message.member ( result, "height" ) ) );
		uint64_t value = base::read_number ( message.raw ( message.member ( result, "coinbasevalue" ) ) );
		uint64_t version = base::read_number ( message.raw ( message.member ( result, "version" ) ) );
		uint64_t time = base::read_number ( message.raw ( message.member ( result, "curtime" ) ) );
		int32_t bits_node = message.member ( result, "bits" );
		uint32_t bits = 0;
		uint8_t display [ 32 ];

		if ( message.raw_size ( previous ) != 64 || !base::read_hex ( message.raw ( previous ), 64, display, 32 ) || !base::read_hex32 ( message.raw ( bits_node ), message.raw_size ( bits_node ), bits ) || message.kind ( list ) != base::JSON_ARRAY ) {

			note ( EVENT_WARNING, L"The node sent a block template Alloy couldn't read" );

			return false;

		}

		pool::job* work = new pool::job;
		base::list < uint8_t > head;
		base::list < uint8_t > tail;
		base::list < uint8_t > pushed;
		static const uint8_t opening [ ] = { 0x01, 0x00, 0x00, 0x00, 0x01 };
		static const uint8_t spent [ 4 ] = { 0xFF, 0xFF, 0xFF, 0xFF };
		int32_t tag_size = base::min ( static_cast < int32_t > ( base::length ( config.tag ) ), 40 );
		uint8_t zero [ 32 ] = { };
		uint8_t amount [ 8 ];
		bool witness = message.kind ( commitment ) == base::JSON_STRING && message.raw_size ( commitment ) > 0 && message.raw_size ( commitment ) <= 160;

		height_push ( height, pushed );
		head.append ( opening, 5 );
		head.append ( zero, 32 );
		head.append ( spent, 4 );
		head.push ( static_cast < uint8_t > ( pushed.size ( ) + 1 + 8 + ( tag_size ? 1 + tag_size : 0 ) ) );
		head.append ( pushed.data ( ), pushed.size ( ) );
		head.push ( 0x08 );

		if ( tag_size ) {

			tail.push ( static_cast < uint8_t > ( tag_size ) );
			tail.append ( reinterpret_cast < const uint8_t* > ( config.tag ), static_cast < size_t > ( tag_size ) );

		}

		tail.append ( spent, 4 );
		tail.push ( witness ? 2 : 1 );
		base::store_little64 ( amount, value );
		tail.append ( amount, 8 );
		tail.push ( static_cast < uint8_t > ( script_size ) );
		tail.append ( script, script_size );

		if ( witness ) {

			uint8_t committed [ 80 ];
			int32_t committed_size = message.raw_size ( commitment ) / 2;

			base::zero ( amount, 8 );
			base::read_hex ( message.raw ( commitment ), message.raw_size ( commitment ), committed, static_cast < size_t > ( committed_size ) );
			tail.append ( amount, 8 );
			tail.push ( static_cast < uint8_t > ( committed_size ) );
			tail.append ( committed, committed_size );

		}

		tail.append ( zero, 4 );

		base::copy ( work->head, head.data ( ), static_cast < size_t > ( head.size ( ) ) );
		base::copy ( work->tail, tail.data ( ), static_cast < size_t > ( tail.size ( ) ) );
		work->head_size = head.size ( );
		work->tail_size = tail.size ( );

		for ( int32_t i = 0; i < 32; i++ )
			work->previous [ i ] = display [ 31 - i ];

		base::reverse_words ( work->previous, 32 );
		work->version = static_cast < uint32_t > ( version );
		work->bits = bits;
		work->time = static_cast < uint32_t > ( time );
		work->clean = true;

		int32_t count = message.count ( list );
		base::list < uint8_t > level;

		level.set_size ( ( count + 2 ) * 32 );
		solo->transactions.clear ( );

		for ( int32_t i = 0; i < count; i++ ) {

			int32_t item = message.item ( list, i );
			int32_t txid = message.member ( item, "txid" );
			int32_t data = message.member ( item, "data" );
			uint8_t id [ 32 ];

			if ( message.raw_size ( txid ) != 64 || !base::read_hex ( message.raw ( txid ), 64, id, 32 ) || message.kind ( data ) != base::JSON_STRING ) {

				delete work;
				note ( EVENT_WARNING, L"The node sent a transaction Alloy couldn't read" );

				return false;

			}

			for ( int32_t k = 0; k < 32; k++ )
				level.data ( ) [ ( i + 1 ) * 32 + k ] = id [ 31 - k ];

			solo->transactions.append ( message.raw ( data ), message.raw_size ( data ) );

		}

		int32_t width = count + 1;

		while ( width > 1 && work->branch_count < branch_room ) {

			base::copy ( work->branches + work->branch_count * 32, level.data ( ) + 32, 32 );
			work->branch_count++;

			if ( width % 2 ) {

				base::copy ( level.data ( ) + width * 32, level.data ( ) + ( width - 1 ) * 32, 32 );
				width++;

			}

			for ( int32_t i = 2; i < width; i += 2 )
				hash::sha256_twice ( level.data ( ) + i * 32, 64, level.data ( ) + ( i / 2 ) * 32 );

			width /= 2;

		}

		base::writer < char, 64 > name;

		name.add ( "solo-" ).number ( height ).put ( '-' ).number ( ++solo->made );
		base::copy_text ( work->id, sizeof ( work->id ), name.get ( ) );
		base::copy_span ( solo->best, sizeof ( solo->best ), message.raw ( previous ), 64 );

		bool fresh = height != solo->height;

		{

			base::guard hold ( state_lock );

			current = *work;
			nonce = session_nonce { };
			nonce.first_size = 4;
			nonce.second_size = 4;
			solo->transaction_count = count;
			solo->witness = witness;
			solo->height = height;
			solo->stale = false;
			solo_on = true;
			difficulty = chain::difficulty_of_bits ( bits );
			next_difficulty = difficulty;
			have_job = true;
			authorized = true;
			advance ( );
			clean_serial = serial;
			shown.state = LINK_READY;
			shown.jobs++;
			shown.has_job = true;
			shown.difficulty = difficulty;
			shown.version = work->version;
			shown.bits = bits;
			shown.time = work->time;
			shown.branches = work->branch_count;
			shown.coinbase_size = work->head_size + work->tail_size + 8;
			shown.reward = value;
			shown.height = height;
			base::copy ( shown.previous, work->previous, 32 );
			base::widen ( shown.job, base::count_of ( shown.job ), work->id );

		}

		delete work;

		if ( fresh ) {

			base::text line;

			line.add ( L"New block template at height " ).number ( height ).add ( L" with " ).number ( static_cast < uint64_t > ( count ) ).add ( count == 1 ? L" transaction" : L" transactions" );
			note ( EVENT_JOB, line.get ( ) );

		}

		return true;

	}

	bool client::fetch_monero ( ) {

		base::json& message = solo->parsed;
		int32_t result = -1;
		double now = base::seconds ( );

		if ( !solo->stale && now - solo->fetched < 10.0 )
			return true;

		char wallet [ 160 ];
		int32_t used = 0;

		for ( int32_t i = 0; config.worker [ i ] && config.worker [ i ] != L'.' && used < 159; i++ )
			wallet [ used++ ] = static_cast < char > ( config.worker [ i ] );

		wallet [ used ] = 0;

		base::line ask;

		ask.add ( "{\"jsonrpc\":\"2.0\",\"id\":\"0\",\"method\":\"get_block_template\",\"params\":{\"wallet_address\":" );
		base::add_json_string ( ask, wallet );
		ask.add ( ",\"reserve_size\":8}}" );
		solo->fetched = now;

		if ( !node_call ( ask.get ( ), ask.size ( ), result ) )
			return false;

		int32_t whole = message.member ( result, "blocktemplate_blob" );
		int32_t hashing = message.member ( result, "blockhashing_blob" );
		int32_t seed = message.member ( result, "seed_hash" );
		uint64_t height = base::read_number ( message.raw ( message.member ( result, "height" ) ) );
		uint64_t needed = base::read_number ( message.raw ( message.member ( result, "difficulty" ) ) );
		blob_job* incoming = new blob_job;
		int32_t whole_size = message.raw_size ( whole ) / 2;

		incoming->blob_size = message.raw_size ( hashing ) / 2;

		if ( message.kind ( whole ) != base::JSON_STRING || whole_size < blob_nonce_at + 4 || incoming->blob_size < blob_nonce_at + 4 || incoming->blob_size > blob_room || !needed || message.raw_size ( seed ) != 64 ) {

			delete incoming;
			note ( EVENT_WARNING, L"The node sent a block template Alloy couldn't read" );

			return false;

		}

		solo->blob.set_size ( whole_size );
		base::read_hex ( message.raw ( whole ), whole_size * 2, solo->blob.data ( ), static_cast < size_t > ( whole_size ) );
		base::read_hex ( message.raw ( hashing ), incoming->blob_size * 2, incoming->blob, static_cast < size_t > ( incoming->blob_size ) );
		base::read_hex ( message.raw ( seed ), 64, incoming->seed, 32 );
		incoming->target = blob_target_for ( static_cast < double > ( needed ) );
		incoming->height = height;
		incoming->variant = config.variant;

		base::writer < char, 64 > name;

		name.add ( "solo-" ).number ( height ).put ( '-' ).number ( ++solo->made );
		base::copy_text ( incoming->id, sizeof ( incoming->id ), name.get ( ) );

		bool fresh = height != solo->height;

		{

			base::guard hold ( state_lock );

			if ( fresh || !have_job || !base::same ( blob_current.blob, incoming->blob, static_cast < size_t > ( incoming->blob_size ) ) ) {

				blob_current = *incoming;
				have_job = true;
				authorized = true;
				advance ( );
				clean_serial = serial;
				shown.jobs++;

			}

			solo->height = height;
			solo->difficulty = needed;
			solo->stale = false;
			difficulty = static_cast < double > ( needed );
			next_difficulty = difficulty;
			shown.state = LINK_READY;
			shown.has_job = true;
			shown.difficulty = difficulty;
			shown.height = height;
			shown.blob_size = incoming->blob_size;
			shown.variant = incoming->variant;
			base::copy ( shown.seed, incoming->seed, 32 );
			base::widen ( shown.job, base::count_of ( shown.job ), incoming->id );

		}

		delete incoming;

		if ( fresh ) {

			base::text line;

			line.add ( L"New block template at height " ).number ( height ).add ( L", difficulty " ).compact ( static_cast < double > ( needed ) );
			note ( EVENT_JOB, line.get ( ) );

		}

		return true;

	}

	void client::submit_blocks ( ) {

		share batch [ 64 ];
		int32_t count = 0;

		{

			base::guard hold ( share_lock );

			count = queued_count;
			base::copy ( batch, queued, sizeof ( share ) * static_cast < size_t > ( count ) );
			queued_count = 0;

		}

		for ( int32_t i = 0; i < count; i++ ) {

			const share& item = batch [ i ];
			bool current_work = false;

			{

				base::guard hold ( state_lock );

				current_work = item.serial >= clean_serial && have_job && !solo->stale;

			}

			if ( !current_work )
				continue;

			if ( blob_mode ( ) && !meets_difficulty ( item.result, solo->difficulty ) )
				continue;

			base::list < char >* body = new base::list < char >;
			int32_t result = -1;
			double sent = base::seconds ( );

			if ( blob_mode ( ) ) {

				uint8_t* blob = solo->blob.data ( );

				base::store_little ( blob + blob_nonce_at, item.nonce );
				add_text ( *body, "{\"jsonrpc\":\"2.0\",\"id\":\"0\",\"method\":\"submit_block\",\"params\":[\"" );
				add_hex ( *body, blob, static_cast < size_t > ( solo->blob.size ( ) ) );
				add_text ( *body, "\"]}" );

			} else {

				chain::header header;
				uint8_t second [ extranonce_room ];
				base::list < uint8_t > block;
				uint8_t* coinbase = new uint8_t [ coinbase_room * 2 + 16 ];
				int32_t size = 0;

				counter_bytes ( item.second, second, 4 );
				build_header ( current, nonce, second, item.time, header );
				header.set_nonce ( item.nonce );
				base::copy ( coinbase, current.head, static_cast < size_t > ( current.head_size ) );
				size += current.head_size;
				base::copy ( coinbase + size, nonce.first, 4 );
				base::copy ( coinbase + size + 4, second, 4 );
				size += 8;
				base::copy ( coinbase + size, current.tail, static_cast < size_t > ( current.tail_size ) );
				size += current.tail_size;

				block.append ( header.bytes, chain::header_size );
				add_varint ( block, static_cast < uint64_t > ( solo->transaction_count + 1 ) );

				if ( solo->witness ) {

					static const uint8_t marker [ 2 ] = { 0x00, 0x01 };
					static const uint8_t reserved [ 2 ] = { 0x01, 0x20 };
					uint8_t zero [ 32 ] = { };

					block.append ( coinbase, 4 );
					block.append ( marker, 2 );
					block.append ( coinbase + 4, size - 8 );
					block.append ( reserved, 2 );
					block.append ( zero, 32 );
					block.append ( coinbase + size - 4, 4 );

				} else
					block.append ( coinbase, size );

				delete [ ] coinbase;
				add_text ( *body, "{\"jsonrpc\":\"1.0\",\"id\":\"alloy\",\"method\":\"submitblock\",\"params\":[\"" );
				add_hex ( *body, block.data ( ), static_cast < size_t > ( block.size ( ) ) );
				body->append ( solo->transactions.data ( ), solo->transactions.size ( ) );
				add_text ( *body, "\"]}" );

			}

			bool answered = node_call ( body->data ( ), body->size ( ), result );
			bool accepted = false;
			wchar_t reason [ 128 ] = { };

			delete body;

			if ( answered ) {

				base::json& message = solo->parsed;

				if ( blob_mode ( ) )
					accepted = message.kind ( result ) == base::JSON_OBJECT && message.text_is ( message.member ( result, "status" ), "OK" );
				else
					accepted = message.is_null ( result ) || message.kind ( result ) == base::JSON_NULL;

				if ( !accepted && message.kind ( result ) == base::JSON_STRING ) {

					char said [ 128 ];

					message.text ( result, said, sizeof ( said ) );
					base::widen ( reason, base::count_of ( reason ), said );

				}

			}

			double latency = ( base::seconds ( ) - sent ) * 1000.0;
			base::text line;

			{

				base::guard hold ( state_lock );

				if ( accepted )
					shown.accepted++;
				else
					shown.rejected++;

				shown.latency = latency;
				solo->stale = true;

			}

			if ( accepted )
				line.add ( L"The node accepted a block at height " ).number ( solo->height );
			else
				line.add ( L"The node turned the block down" ).add ( reason [ 0 ] ? L": " : L"" ).add ( reason );

			note ( accepted ? EVENT_ACCEPTED : EVENT_REJECTED, line.get ( ), item.difficulty, latency, item.nonce );

		}

	}

	bool client::solo_session ( ) {

		if ( !solo )
			solo = new solo_state;

		const wchar_t* address = base::atomic_load ( on_backup ) ? config.backup : config.address;
		base::text line;

		base::narrow ( solo->url, sizeof ( solo->url ), address );

		if ( blob_mode ( ) ) {

			int32_t slashes = 0;

			for ( int32_t i = 0; solo->url [ i ]; i++ )
				slashes += solo->url [ i ] == '/' ? 1 : 0;

			if ( slashes < 3 )
				base::copy_text ( solo->url + base::length ( solo->url ), static_cast < int32_t > ( sizeof ( solo->url ) ) - base::length ( solo->url ), "/json_rpc" );

		}

		char secret [ 256 ] = { };

		base::narrow ( secret, sizeof ( secret ), config.password );
		solo->auth [ 0 ] = 0;

		bool given = false;

		for ( int32_t i = 0; secret [ i ]; i++ )
			given = given || secret [ i ] == ':';

		if ( !given && !blob_mode ( ) ) {

			wchar_t path [ base::path_room ];
			base::list < uint8_t > bytes;

			secret [ 0 ] = 0;

			wchar_t cookie [ 64 ];

			base::copy_text ( cookie, base::count_of ( cookie ), chain::coin_of ( config.coin ).data_folder );
			base::copy_text ( cookie + base::length ( cookie ), base::count_of ( cookie ) - base::length ( cookie ), L"\\.cookie" );

			if ( base::roaming_path ( path, base::path_room, cookie ) && base::read_file ( path, bytes ) && bytes.size ( ) < 200 ) {

				int32_t size = bytes.size ( );

				while ( size && ( bytes.data ( ) [ size - 1 ] == '\n' || bytes.data ( ) [ size - 1 ] == '\r' ) )
					size--;

				base::copy_span ( secret, sizeof ( secret ), reinterpret_cast < const char* > ( bytes.data ( ) ), size );
				given = secret [ 0 ] != 0;

			}

		}

		if ( given ) {

			char encoded [ 360 ];

			if ( base::write_base64 ( encoded, sizeof ( encoded ), reinterpret_cast < const uint8_t* > ( secret ), static_cast < size_t > ( base::length ( secret ) ) ) > 0 ) {

				base::copy_text ( solo->auth, sizeof ( solo->auth ), "Authorization: Basic " );
				base::copy_text ( solo->auth + base::length ( solo->auth ), static_cast < int32_t > ( sizeof ( solo->auth ) ) - base::length ( solo->auth ), encoded );
				base::copy_text ( solo->auth + base::length ( solo->auth ), static_cast < int32_t > ( sizeof ( solo->auth ) ) - base::length ( solo->auth ), "\r\n" );

			}

		}

		base::zero ( secret, sizeof ( secret ) );

		{

			base::guard hold ( state_lock );

			have_job = false;
			authorized = false;
			base::atomic_add ( serial, 1u );
			shown.state = LINK_CONNECTING;

		}

		solo->stale = true;
		solo->height = 0;
		line.add ( L"Mining solo: asking the node at " ).add ( address ).add ( L" for block templates" );
		note ( EVENT_INFO, line.get ( ) );

		bool worked = false;

		while ( !base::atomic_load ( quit ) && !refused && !base::atomic_load ( nudged ) ) {

			if ( !( blob_mode ( ) ? fetch_monero ( ) : fetch_template ( ) ) )
				break;

			worked = true;
			submit_blocks ( );
			base::sleep ( 50 );

		}

		base::guard hold ( state_lock );

		have_job = false;
		authorized = false;
		base::atomic_add ( serial, 1u );

		return worked;

	}

}
