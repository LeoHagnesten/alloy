#include "pool/test_pool.hh"
#include "randomx/machine.hh"
#include "hash/scrypt.hh"
#include "hash/heavyhash.hh"
#include "chain/target.hh"
#include "net/socket.hh"
#include "base/bits.hh"
#include "base/system/clock.hh"
#include "base/system/console.hh"
#include "base/containers/list.hh"
#include "base/memory.hh"
#include "base/strings.hh"
#include "base/text.hh"

namespace pool {

	static constexpr int32_t max_peers = 8;
	static constexpr int32_t kept_jobs = 4;
	static constexpr int32_t seen_room = 4096;

	struct peer {

		net::connection link;
		bool used = false;
		bool authorized = false;
		session_nonce nonce;
		uint64_t seen [ seen_room ] = { };
		int32_t seen_count = 0;
		wchar_t worker [ 64 ] = { };
		char session [ 24 ] = { };

	};

	struct job_book {

		job jobs [ kept_jobs ];
		blob_job blobs [ kept_jobs ];
		kaspa_job kaspas [ kept_jobs ];
		uint32_t made = 0;

		uint32_t newest_slot ( ) const {

			return ( made - 1 ) % kept_jobs;

		}

		int32_t find ( const char* id, uint8_t kind ) const {

			for ( uint32_t i = 0; i < kept_jobs && i < made; i++ ) {

				if ( base::same_text ( kind == chain::ALGORITHM_RANDOMX ? blobs [ i ].id : ( kind == chain::ALGORITHM_KHEAVYHASH ? kaspas [ i ].id : jobs [ i ].id ), id ) )
					return static_cast < int32_t > ( i );

			}

			return -1;

		}

	};

	struct pool_state {

		server_settings config;
		job_book book;
		uint8_t target [ 32 ] = { };
		uint64_t blob_target = 0;
		uint8_t seed [ 32 ] = { };
		uint32_t seeds = 0;
		randomx::cache* cache = nullptr;
		randomx::machine* machine = nullptr;
		hash::heavy_hasher* heavy = nullptr;
		void* scratch = nullptr;
		volatile uint32_t* good = nullptr;
		volatile uint32_t* bad = nullptr;

	};

	static void name_job ( char* out, int32_t room, uint32_t number ) {

		base::writer < char, 16 > name;

		name.number ( number );
		base::copy_text ( out, room, name.get ( ) );

	}

	static void make_stratum_job ( pool_state& state ) {

		job_book& book = state.book;
		uint32_t height = 840000 + book.made;
		job& work = book.jobs [ book.made % kept_jobs ];
		uint8_t seed [ 16 ];
		uint8_t digest [ 32 ];

		work = job { };
		name_job ( work.id, sizeof ( work.id ), book.made + 1 );
		base::store_little ( seed, book.made + 1 );
		base::store_little ( seed + 4, base::unix_time ( ) );
		base::copy ( seed + 8, "alloypoo", 8 );
		hash::sha256_twice ( seed, 16, work.previous );

		static const uint8_t opening [ ] = { 0x01, 0x00, 0x00, 0x00, 0x01 };
		static const uint8_t spent [ 4 ] = { 0xFF, 0xFF, 0xFF, 0xFF };
		static const char tag [ ] = "/alloy test pool/";
		uint8_t zero [ 32 ] = { };
		uint8_t height_push [ 4 ] = { 0x03, static_cast < uint8_t > ( height ), static_cast < uint8_t > ( height >> 8 ), static_cast < uint8_t > ( height >> 16 ) };
		uint8_t reward [ 8 ] = { };
		uint8_t locking [ 25 ] = { 0x76, 0xA9, 0x14 };
		base::list < uint8_t > head;
		base::list < uint8_t > tail;

		base::store_little ( reward, 312500000 );
		hash::sha256_once ( tag, 17, digest );
		base::copy ( locking + 3, digest, 20 );
		locking [ 23 ] = 0x88;
		locking [ 24 ] = 0xAC;

		head.append ( opening, 5 );
		head.append ( zero, 32 );
		head.append ( spent, 4 );
		head.push ( 4 + 8 + 1 + 17 );
		head.append ( height_push, 4 );

		tail.push ( 17 );
		tail.append ( reinterpret_cast < const uint8_t* > ( tag ), 17 );
		tail.append ( spent, 4 );
		tail.push ( 1 );
		tail.append ( reward, 8 );
		tail.push ( 25 );
		tail.append ( locking, 25 );
		tail.append ( zero, 4 );

		base::copy ( work.head, head.data ( ), static_cast < size_t > ( head.size ( ) ) );
		base::copy ( work.tail, tail.data ( ), static_cast < size_t > ( tail.size ( ) ) );
		work.head_size = head.size ( );
		work.tail_size = tail.size ( );
		work.branch_count = 3;

		for ( int32_t i = 0; i < work.branch_count; i++ ) {

			seed [ 0 ] = static_cast < uint8_t > ( i );
			hash::sha256_twice ( seed, 16, work.branches + i * 32 );

		}

		work.version = 0x20000000;
		work.bits = state.config.algorithm == chain::ALGORITHM_SCRYPT ? 0x1A01CD2D : 0x1703A30C;
		work.time = base::unix_time ( );
		work.clean = true;

	}

	static void make_blob_job ( pool_state& state ) {

		job_book& book = state.book;
		blob_job& work = book.blobs [ book.made % kept_jobs ];
		uint32_t time = base::unix_time ( );
		uint8_t seed [ 16 ];

		if ( !state.seeds || ( state.config.reseed_jobs && book.made && book.made % state.config.reseed_jobs == 0 ) ) {

			uint8_t words [ 8 ];

			base::store_little ( words, state.seeds++ );
			base::copy ( words + 4, "seed", 4 );
			hash::sha256_twice ( words, 8, state.seed );

		}

		work = blob_job { };
		name_job ( work.id, sizeof ( work.id ), book.made + 1 );
		work.blob [ 0 ] = 16;
		work.blob [ 1 ] = 16;

		for ( int32_t i = 0; i < 5; i++ )
			work.blob [ 2 + i ] = static_cast < uint8_t > ( ( time >> ( i * 7 ) ) & 0x7F ) | ( i < 4 ? 0x80 : 0 );

		base::store_little ( seed, book.made + 1 );
		base::store_little ( seed + 4, time );
		base::copy ( seed + 8, "alloyxmr", 8 );
		hash::sha256_twice ( seed, 16, work.blob + 7 );
		seed [ 15 ] = 0x5A;
		hash::sha256_twice ( seed, 16, work.blob + 43 );
		work.blob [ 75 ] = 1;
		work.blob [ blob_nonce_at + 3 ] = state.config.nicehash ? 0xA7 : 0;
		work.blob_size = 76;
		work.target = state.blob_target;
		base::copy ( work.seed, state.seed, 32 );
		work.height = 3200000 + book.made;
		work.variant = state.config.variant;

		if ( state.config.switch_jobs && book.made )
			work.variant = static_cast < uint8_t > ( ( state.config.variant + book.made / state.config.switch_jobs ) % randomx::VARIANT_COUNT );

	}

	static void make_kaspa_job ( pool_state& state ) {

		job_book& book = state.book;
		kaspa_job& work = book.kaspas [ book.made % kept_jobs ];
		uint8_t seed [ 16 ];

		work = kaspa_job { };
		name_job ( work.id, sizeof ( work.id ), book.made + 1 );
		base::store_little ( seed, book.made + 1 );
		base::store_little ( seed + 4, base::unix_time ( ) );
		base::copy ( seed + 8, "alloykas", 8 );
		hash::sha256_twice ( seed, 16, work.pre_pow );
		work.timestamp = static_cast < uint64_t > ( base::unix_time ( ) ) * 1000 + book.made;

	}

	static void make_job ( pool_state& state ) {

		if ( state.config.algorithm == chain::ALGORITHM_RANDOMX )
			make_blob_job ( state );
		else if ( state.config.algorithm == chain::ALGORITHM_KHEAVYHASH )
			make_kaspa_job ( state );
		else
			make_stratum_job ( state );

		state.book.made++;

	}

	static void echo_id ( base::line& out, const base::json& message, int32_t id ) {

		if ( message.kind ( id ) == base::JSON_STRING )
			out.put ( '"' ).add ( message.raw ( id ), message.raw_size ( id ) ).put ( '"' );
		else if ( message.kind ( id ) == base::JSON_NUMBER )
			out.add ( message.raw ( id ), message.raw_size ( id ) );
		else
			out.add ( "null" );

	}

	static void reply ( peer& client, const base::json& message, int32_t id, const char* result, int32_t code, const char* reason ) {

		base::line out;

		out.add ( "{\"id\":" );
		echo_id ( out, message, id );

		if ( result )
			out.add ( ",\"result\":" ).add ( result ).add ( ",\"error\":null}" );
		else {

			out.add ( ",\"result\":null,\"error\":[" ).number ( static_cast < uint64_t > ( code ) ).put ( ',' );
			base::add_json_string ( out, reason );
			out.add ( ",null]}" );

		}

		client.link.send_line ( out.get ( ) );

	}

	static void reply_blob ( peer& client, const base::json& message, int32_t id, const char* result, int32_t code, const char* reason ) {

		base::line out;

		out.add ( "{\"id\":" );
		echo_id ( out, message, id );
		out.add ( ",\"jsonrpc\":\"2.0\"" );

		if ( result )
			out.add ( ",\"error\":null,\"result\":" ).add ( result ).put ( '}' );
		else {

			out.add ( ",\"error\":{\"code\":" ).number ( static_cast < uint64_t > ( code ) ).add ( ",\"message\":" );
			base::add_json_string ( out, reason );
			out.add ( "}}" );

		}

		client.link.send_line ( out.get ( ) );

	}

	static void send_work ( peer& client, const pool_state& state ) {

		base::line out;
		const job_book& book = state.book;

		if ( state.config.algorithm == chain::ALGORITHM_RANDOMX ) {

			out.add ( "{\"jsonrpc\":\"2.0\",\"method\":\"job\",\"params\":" );
			write_blob_job ( out, book.blobs [ book.newest_slot ( ) ] );
			out.put ( '}' );
			client.link.send_line ( out.get ( ) );

			return;

		}

		out.add ( "{\"id\":null,\"method\":\"mining.set_difficulty\",\"params\":[" ).decimal ( state.config.difficulty, 6 ).add ( "]}" );
		client.link.send_line ( out.get ( ) );
		out.clear ( );

		if ( state.config.algorithm == chain::ALGORITHM_KHEAVYHASH )
			write_kaspa_notify ( out, book.kaspas [ book.newest_slot ( ) ], book.made % 2 == 0 );
		else
			write_notify ( out, book.jobs [ book.newest_slot ( ) ] );
		client.link.send_line ( out.get ( ) );

	}

	static bool remember ( peer& client, uint64_t key ) {

		for ( int32_t k = 0; k < base::min ( client.seen_count, seen_room ); k++ ) {

			if ( client.seen [ k ] == key )
				return false;

		}

		client.seen [ client.seen_count++ % seen_room ] = key;

		return true;

	}

	static uint64_t share_key ( const uint8_t* parts, size_t size ) {

		uint64_t key = 1469598103934665603ull;

		for ( size_t i = 0; i < size; i++ )
			key = ( key ^ parts [ i ] ) * 1099511628211ull;

		return key;

	}

	static void tally ( pool_state& state, bool accepted ) {

		base::atomic_add ( accepted ? *state.good : *state.bad, 1u );

	}

	static void announce ( const pool_state& state, const peer& client, const uint8_t* digest ) {

		if ( !state.config.verbose )
			return;

		base::text note;

		note.add ( L"  share from " ).add ( client.worker ).add ( L", difficulty " ).decimal ( chain::difficulty_of ( chain::top_bits ( digest ), state.config.algorithm ), 3 ).add ( L", hash " ).hex ( digest, 32, true );
		base::print_line ( note.get ( ) );

	}

	static void handle_stratum ( pool_state& state, peer& client, const base::json& message ) {

		int32_t id = message.member ( 0, "id" );
		int32_t method = message.member ( 0, "method" );
		int32_t params = message.member ( 0, "params" );

		if ( message.text_is ( method, "mining.subscribe" ) ) {

			base::line result;

			result.add ( "[[[\"mining.set_difficulty\",\"alloy\"],[\"mining.notify\",\"alloy\"]],\"" ).hex ( client.nonce.first, 4, false ).add ( "\",4]" );
			reply ( client, message, id, result.get ( ), 0, nullptr );

			return;

		}

		if ( message.text_is ( method, "mining.authorize" ) ) {

			char worker [ 64 ];

			message.text ( message.item ( params, 0 ), worker, sizeof ( worker ) );
			base::widen ( client.worker, base::count_of ( client.worker ), worker );
			client.authorized = true;
			reply ( client, message, id, "true", 0, nullptr );
			send_work ( client, state );

			return;

		}

		if ( message.text_is ( method, "mining.extranonce.subscribe" ) ) {

			reply ( client, message, id, "true", 0, nullptr );

			return;

		}

		if ( !message.text_is ( method, "mining.submit" ) ) {

			reply ( client, message, id, nullptr, 20, "Unknown method" );

			return;

		}

		char job_id [ 64 ];
		uint8_t second [ 8 ];
		uint32_t time = 0;
		uint32_t nonce = 0;
		int32_t second_node = message.item ( params, 2 );
		int32_t time_node = message.item ( params, 3 );
		int32_t nonce_node = message.item ( params, 4 );

		message.text ( message.item ( params, 1 ), job_id, sizeof ( job_id ) );

		int32_t slot = state.book.find ( job_id, chain::ALGORITHM_SHA256D );

		if ( !client.authorized ) {

			reply ( client, message, id, nullptr, 24, "Unauthorized worker" );
			tally ( state, false );

			return;

		}

		if ( slot < 0 ) {

			reply ( client, message, id, nullptr, 21, "Job not found" );
			tally ( state, false );

			return;

		}

		if ( !base::read_hex ( message.raw ( second_node ), message.raw_size ( second_node ), second, 4 ) || !base::read_hex32 ( message.raw ( time_node ), message.raw_size ( time_node ), time ) || !base::read_hex32 ( message.raw ( nonce_node ), message.raw_size ( nonce_node ), nonce ) ) {

			reply ( client, message, id, nullptr, 20, "Malformed share" );
			tally ( state, false );

			return;

		}

		uint8_t parts [ 16 ];

		base::copy ( parts, second, 4 );
		base::store_little ( parts + 4, time );
		base::store_little ( parts + 8, nonce );
		base::store_little ( parts + 12, static_cast < uint32_t > ( slot ) );

		if ( !remember ( client, share_key ( parts, sizeof ( parts ) ) ) ) {

			reply ( client, message, id, nullptr, 22, "Duplicate share" );
			tally ( state, false );

			return;

		}

		chain::header header;
		uint8_t digest [ 32 ];

		build_header ( state.book.jobs [ slot ], client.nonce, second, time, header );
		header.set_nonce ( nonce );

		if ( state.config.algorithm == chain::ALGORITHM_SCRYPT )
			hash::scrypt_header ( header.bytes, digest, state.scratch );
		else
			hash::sha256_twice ( header.bytes, chain::header_size, digest );

		if ( !chain::meets ( digest, state.target ) ) {

			reply ( client, message, id, nullptr, 23, "Low difficulty share" );
			tally ( state, false );

			if ( state.config.verbose )
				base::print_line ( L"  rejected a share below the target" );

			return;

		}

		reply ( client, message, id, "true", 0, nullptr );
		tally ( state, true );
		announce ( state, client, digest );

	}

	static void handle_blob ( pool_state& state, peer& client, const base::json& message ) {

		int32_t id = message.member ( 0, "id" );
		int32_t method = message.member ( 0, "method" );
		int32_t params = message.member ( 0, "params" );

		if ( message.text_is ( method, "login" ) ) {

			char worker [ 64 ];
			base::line result;

			message.text ( message.member ( params, "login" ), worker, sizeof ( worker ) );
			base::widen ( client.worker, base::count_of ( client.worker ), worker );
			client.authorized = true;
			result.add ( "{\"id\":\"" ).add ( client.session ).add ( "\",\"job\":" );
			write_blob_job ( result, state.book.blobs [ state.book.newest_slot ( ) ] );
			result.add ( ",\"extensions\":[\"keepalive\"],\"status\":\"OK\"}" );
			reply_blob ( client, message, id, result.get ( ), 0, nullptr );

			return;

		}

		if ( message.text_is ( method, "keepalived" ) ) {

			reply_blob ( client, message, id, "{\"status\":\"KEEPALIVED\"}", 0, nullptr );

			return;

		}

		if ( !message.text_is ( method, "submit" ) ) {

			reply_blob ( client, message, id, nullptr, -1, "Unknown method" );

			return;

		}

		char job_id [ 64 ];
		uint8_t nonce [ 4 ];
		uint8_t claimed [ 32 ];
		int32_t nonce_node = message.member ( params, "nonce" );
		int32_t result_node = message.member ( params, "result" );

		message.text ( message.member ( params, "job_id" ), job_id, sizeof ( job_id ) );

		int32_t slot = state.book.find ( job_id, chain::ALGORITHM_RANDOMX );

		if ( !client.authorized || !message.text_is ( message.member ( params, "id" ), client.session ) ) {

			reply_blob ( client, message, id, nullptr, -1, "Unauthenticated" );
			tally ( state, false );

			return;

		}

		if ( slot < 0 ) {

			reply_blob ( client, message, id, nullptr, -1, "Block expired" );
			tally ( state, false );

			return;

		}

		if ( message.raw_size ( nonce_node ) != 8 || message.raw_size ( result_node ) != 64 || !base::read_hex ( message.raw ( nonce_node ), 8, nonce, 4 ) || !base::read_hex ( message.raw ( result_node ), 64, claimed, 32 ) ) {

			reply_blob ( client, message, id, nullptr, -1, "Malformed share" );
			tally ( state, false );

			return;

		}

		if ( state.config.nicehash && nonce [ 3 ] != 0xA7 ) {

			reply_blob ( client, message, id, nullptr, -1, "Nonce outside your range" );
			tally ( state, false );

			return;

		}

		uint8_t parts [ 8 ];

		base::copy ( parts, nonce, 4 );
		base::store_little ( parts + 4, static_cast < uint32_t > ( slot ) );

		if ( !remember ( client, share_key ( parts, sizeof ( parts ) ) ) ) {

			reply_blob ( client, message, id, nullptr, -1, "Duplicate share" );
			tally ( state, false );

			return;

		}

		blob_job& work = state.book.blobs [ slot ];
		uint8_t blob [ blob_room ];
		uint8_t digest [ 32 ];

		if ( !state.cache->matches ( work.seed, 32, work.variant ) )
			state.cache->build ( work.seed, 32, work.variant );

		base::copy ( blob, work.blob, static_cast < size_t > ( work.blob_size ) );
		base::copy ( blob + blob_nonce_at, nonce, 4 );
		state.machine->open ( state.cache, nullptr, work.variant );
		state.machine->hash ( blob, static_cast < size_t > ( work.blob_size ), digest );

		if ( !base::same ( digest, claimed, 32 ) ) {

			reply_blob ( client, message, id, nullptr, -1, "Invalid result" );
			tally ( state, false );

			if ( state.config.verbose )
				base::print_line ( L"  rejected a share whose hash didn't check out" );

			return;

		}

		if ( chain::top_bits ( digest ) >= work.target ) {

			reply_blob ( client, message, id, nullptr, -1, "Low difficulty share" );
			tally ( state, false );

			return;

		}

		reply_blob ( client, message, id, "{\"status\":\"OK\"}", 0, nullptr );
		tally ( state, true );
		announce ( state, client, digest );

	}

	static void handle_kaspa ( pool_state& state, peer& client, const base::json& message ) {

		int32_t id = message.member ( 0, "id" );
		int32_t method = message.member ( 0, "method" );
		int32_t params = message.member ( 0, "params" );

		if ( message.text_is ( method, "mining.subscribe" ) ) {

			base::line out;

			reply ( client, message, id, "[true,\"EthereumStratum/1.0.0\"]", 0, nullptr );
			out.add ( "{\"id\":null,\"method\":\"set_extranonce\",\"params\":[\"" ).hex ( client.nonce.first, 2, false ).add ( "\",6]}" );
			client.link.send_line ( out.get ( ) );

			return;

		}

		if ( message.text_is ( method, "mining.authorize" ) ) {

			char worker [ 64 ];

			message.text ( message.item ( params, 0 ), worker, sizeof ( worker ) );
			base::widen ( client.worker, base::count_of ( client.worker ), worker );
			client.authorized = true;
			reply ( client, message, id, "true", 0, nullptr );
			send_work ( client, state );

			return;

		}

		if ( !message.text_is ( method, "mining.submit" ) ) {

			reply ( client, message, id, nullptr, 20, "Unknown method" );

			return;

		}

		char job_id [ 64 ];
		int32_t nonce_node = message.item ( params, 2 );
		const char* text = message.raw ( nonce_node );
		int32_t size = message.raw_size ( nonce_node );
		uint8_t bytes [ 8 ];

		message.text ( message.item ( params, 1 ), job_id, sizeof ( job_id ) );

		int32_t slot = state.book.find ( job_id, chain::ALGORITHM_KHEAVYHASH );

		if ( size == 18 && text [ 0 ] == '0' && ( text [ 1 ] == 'x' || text [ 1 ] == 'X' ) ) {

			text += 2;
			size -= 2;

		}

		if ( !client.authorized ) {

			reply ( client, message, id, nullptr, 24, "Unauthorized worker" );
			tally ( state, false );

			return;

		}

		if ( slot < 0 ) {

			reply ( client, message, id, nullptr, 21, "Job not found" );
			tally ( state, false );

			return;

		}

		if ( size != 16 || !base::read_hex ( text, 16, bytes, 8 ) ) {

			reply ( client, message, id, nullptr, 20, "Malformed share" );
			tally ( state, false );

			return;

		}

		if ( bytes [ 0 ] != client.nonce.first [ 0 ] || bytes [ 1 ] != client.nonce.first [ 1 ] ) {

			reply ( client, message, id, nullptr, 20, "Nonce outside your range" );
			tally ( state, false );

			return;

		}

		uint8_t parts [ 12 ];

		base::copy ( parts, bytes, 8 );
		base::store_little ( parts + 8, static_cast < uint32_t > ( slot ) );

		if ( !remember ( client, share_key ( parts, sizeof ( parts ) ) ) ) {

			reply ( client, message, id, nullptr, 22, "Duplicate share" );
			tally ( state, false );

			return;

		}

		const kaspa_job& work = state.book.kaspas [ slot ];
		uint8_t digest [ 32 ];
		uint64_t nonce = static_cast < uint64_t > ( base::load_big ( bytes ) ) << 32 | base::load_big ( bytes + 4 );

		state.heavy->prepare ( work.pre_pow, work.timestamp );
		state.heavy->hash ( nonce, digest );

		if ( !chain::meets ( digest, state.target ) ) {

			reply ( client, message, id, nullptr, 23, "Low difficulty share" );
			tally ( state, false );

			if ( state.config.verbose )
				base::print_line ( L"  rejected a share below the target" );

			return;

		}

		reply ( client, message, id, "true", 0, nullptr );
		tally ( state, true );
		announce ( state, client, digest );

	}

	bool test_server::start ( const server_settings& value ) {

		stop ( );

		net::listener* listener = new net::listener;

		if ( !listener->open ( value.port ) ) {

			delete listener;

			return false;

		}

		config = value;
		socket = listener;
		bound = listener->port ( );
		quit = 0;
		good = 0;
		bad = 0;
		connected = 0;
		made = 0;
		listening = 0;
		thread = base::start_thread ( &main, this );

		return true;

	}

	void test_server::stop ( ) {

		if ( !socket )
			return;

		base::atomic_store ( quit, 1u );
		base::join_thread ( thread );
		thread = nullptr;
		delete static_cast < net::listener* > ( socket );
		socket = nullptr;
		bound = 0;

	}

	unsigned long test_server::main ( void* context ) {

		static_cast < test_server* > ( context )->run ( );

		return 0;

	}

	void test_server::run ( ) {

		if ( config.node ) {

			run_node ( );

			return;

		}

		if ( config.sv2 ) {

			run_sv2 ( );

			return;

		}

		net::listener& listener = *static_cast < net::listener* > ( socket );
		peer* peers = new peer [ max_peers ];
		pool_state* state = new pool_state;
		base::json* message = new base::json;
		char* text = new char [ net::line_room ];
		uint32_t sessions = 0;
		bool blob = config.algorithm == chain::ALGORITHM_RANDOMX;

		state->config = config;
		state->good = &good;
		state->bad = &bad;
		state->blob_target = blob_target_for ( config.difficulty );
		chain::target_for ( config.difficulty, state->target, config.algorithm );

		if ( config.algorithm == chain::ALGORITHM_SCRYPT )
			state->scratch = base::allocate ( hash::scrypt_scratch_bytes );

		if ( config.algorithm == chain::ALGORITHM_KHEAVYHASH )
			state->heavy = new hash::heavy_hasher;

		make_job ( *state );

		if ( blob ) {

			state->cache = new randomx::cache;
			state->machine = new randomx::machine;
			state->cache->build ( state->seed, 32, config.variant );

		}

		base::atomic_store ( made, state->book.made );
		base::atomic_store ( listening, 1u );

		double last_job = base::seconds ( );

		while ( !base::atomic_load ( quit ) ) {

			bool busy = false;

			for ( int32_t i = 0; i < max_peers; i++ ) {

				peer& client = peers [ i ];

				if ( client.used || !listener.accept ( client.link, 0 ) )
					continue;

				base::writer < char, 24 > name;

				sessions++;
				client.used = true;
				client.authorized = false;
				client.seen_count = 0;
				client.nonce = session_nonce { };
				client.nonce.first_size = 4;
				base::store_big ( client.nonce.first, ( config.algorithm == chain::ALGORITHM_KHEAVYHASH ? 0xA7000000 : 0xA1100000 ) + ( sessions << ( config.algorithm == chain::ALGORITHM_KHEAVYHASH ? 16 : 0 ) ) );
				name.add ( "alloy-" ).number ( sessions );
				base::copy_text ( client.session, sizeof ( client.session ), name.get ( ) );
				base::atomic_add ( connected, 1u );
				busy = true;

				break;

			}

			for ( int32_t i = 0; i < max_peers; i++ ) {

				peer& client = peers [ i ];
				int32_t size = 0;
				int32_t handled = 0;

				if ( !client.used )
					continue;

				while ( handled++ < 16 && ( size = client.link.read_line ( text, net::line_room, 0 ) ) > 0 ) {

					busy = true;

					if ( !message->parse ( text, size ) )
						continue;

					if ( blob )
						handle_blob ( *state, client, *message );
					else if ( config.algorithm == chain::ALGORITHM_KHEAVYHASH )
						handle_kaspa ( *state, client, *message );
					else
						handle_stratum ( *state, client, *message );

				}

				if ( size < 0 || !client.link.is_open ( ) ) {

					client.link.close ( );
					client.used = false;
					client.authorized = false;
					base::atomic_add ( connected, ~0u );

				}

			}

			if ( base::seconds ( ) - last_job >= static_cast < double > ( config.job_seconds ) ) {

				make_job ( *state );
				base::atomic_store ( made, state->book.made );
				last_job = base::seconds ( );

				for ( int32_t i = 0; i < max_peers; i++ ) {

					if ( peers [ i ].used && peers [ i ].authorized )
						send_work ( peers [ i ], *state );

				}

			}

			if ( !busy ) {

				uintptr_t handles [ max_peers + 1 ] = { listener.native ( ) };
				int32_t count = 1;

				for ( int32_t i = 0; i < max_peers; i++ ) {

					if ( peers [ i ].used )
						handles [ count++ ] = peers [ i ].link.native ( );

				}

				net::wait_any ( handles, count, 20 );

			}

		}

		base::release ( state->scratch );
		delete state->heavy;
		delete state->machine;
		delete state->cache;
		delete [ ] text;
		delete message;
		delete state;
		delete [ ] peers;

	}

}
