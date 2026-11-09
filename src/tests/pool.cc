#ifdef ALLOY_DEBUG

#include "tests/harness.hh"
#include "net/http.hh"
#include "randomx/variant.hh"
#include "app/session.hh"
#include "pool/test_pool.hh"
#include "net/socket.hh"
#include "chain/target.hh"
#include "base/system/clock.hh"
#include "base/formats/json.hh"
#include "base/math.hh"
#include "base/memory.hh"
#include "base/strings.hh"
#include "base/system/hardware.hh"
#include "base/text.hh"

namespace tests {

	struct raw_client {

		net::connection link;
		base::json message;
		char line [ net::line_room ];

		bool wait_for ( const char* method, double limit ) {

			double began = base::seconds ( );

			while ( base::seconds ( ) - began < limit ) {

				int32_t size = link.read_line ( line, net::line_room, 50 );

				if ( size < 0 )
					return false;

				if ( size > 0 && message.parse ( line, size ) && message.text_is ( message.member ( 0, "method" ), method ) )
					return true;

			}

			return false;

		}

		bool answer ( uint32_t id, double limit ) {

			double began = base::seconds ( );

			while ( base::seconds ( ) - began < limit ) {

				int32_t size = link.read_line ( line, net::line_room, 50 );

				if ( size < 0 )
					return false;

				if ( size > 0 && message.parse ( line, size ) && message.number ( message.member ( 0, "id" ) ) == id )
					return true;

			}

			return false;

		}

		int32_t error_code ( ) const {

			return static_cast < int32_t > ( message.number ( message.item ( message.member ( 0, "error" ), 0 ) ) );

		}

		bool accepted ( ) const {

			return message.truth ( message.member ( 0, "result" ) );

		}

	};

	static bool submit ( raw_client& client, uint32_t id, const char* job, const char* second, uint32_t time, uint32_t nonce ) {

		base::line request;

		request.add ( "{\"id\":" ).number ( id ).add ( ",\"method\":\"mining.submit\",\"params\":[\"tester\",\"" ).add ( job ).add ( "\",\"" ).add ( second ).add ( "\",\"" ).hex32 ( time ).add ( "\",\"" ).hex32 ( nonce ).add ( "\"]}" );

		return client.link.send_line ( request.get ( ) ) && client.answer ( id, 3.0 );

	}

	template < typename condition >
	static bool wait_until ( app::session& run, const ui::preferences& settings, double limit, condition done ) {

		double began = base::seconds ( );

		while ( base::seconds ( ) - began < limit ) {

			run.tick ( base::seconds ( ), settings );

			if ( done ( run.view ( ) ) )
				return true;

			base::sleep ( 20 );

		}

		return false;

	}

	static int32_t count_logs ( const ui::snapshot& view, const wchar_t* prefix ) {

		int32_t found = 0;

		for ( int32_t i = 0; i < view.log_count; i++ )
			found += base::starts_with ( view.logs [ i ].text, prefix ) ? 1 : 0;

		return found;

	}

	static void mine_on ( uint8_t algorithm, double difficulty, uint32_t reseed, bool gpu, uint32_t wanted, double limit, bool nicehash = false ) {

		pool::test_server* server = new pool::test_server;
		pool::server_settings config;
		base::text note;

		config.algorithm = algorithm;
		config.port = 0;
		config.difficulty = difficulty;
		config.job_seconds = 3;
		config.reseed_jobs = reseed;
		config.nicehash = nicehash;

		if ( !server->start ( config ) ) {

			check ( false, L"the test pool starts" );
			delete server;

			return;

		}

		double began = base::seconds ( );

		while ( !server->ready ( ) && base::seconds ( ) - began < 10.0 )
			base::sleep ( 10 );

		app::session* run = new app::session;
		ui::preferences settings;
		base::writer < wchar_t, 64 > address;

		address.add ( L"127.0.0.1:" ).number ( server->port ( ) );
		settings.algorithm = algorithm;
		base::copy_text ( settings.pool ( ).address, base::count_of ( settings.pool ( ).address ), address.get ( ) );
		settings.mode = ui::MODE_POOL;
		settings.threads = algorithm == chain::ALGORITHM_RANDOMX ? run->processor ( ).threads : base::min ( run->processor ( ).threads, 4u );
		settings.gpu = gpu;
		settings.dataset_megabytes = ui::dataset_full;
		run->start ( settings );

		bool flowing = wait_until ( *run, settings, limit, [ & ] ( const ui::snapshot& view ) {

			bool reseeded = !reseed || ( count_logs ( view, L"The RandomX seed changed" ) > 0 && view.dataset == ui::DATASET_FULL );

			if ( nicehash && !view.nicehash )
				return false;

			return view.accepted >= wanted && count_logs ( view, L"New job " ) >= ( reseed ? 3 : 2 ) && view.link == ui::PHASE_READY && reseeded;

		} );

		const ui::snapshot& view = run->view ( );
		uint32_t accepted = view.accepted;
		uint32_t rejected = view.rejected;
		uint32_t stale = view.stale;
		double rate = view.rate;
		bool gpu_used = view.gpu_hashes > 0;

		run->stop ( );
		note.add ( chain::algorithm_name ( algorithm ) ).add ( L" mines on its test pool" );

		if ( algorithm == chain::ALGORITHM_RANDOMX )
			note.add ( reseed ? L" through a seed change" : L"" ).add ( nicehash ? L" in NiceHash mode" : L"" ).add ( L" with the full dataset" );
		else if ( gpu )
			note.add ( L" with the CPU and GPU" );

		note.add ( L": " ).number ( accepted ).add ( L" accepted, " ).number ( rejected ).add ( L" rejected, " ).number ( stale ).add ( L" stale at " ).scaled ( rate, L"H/s" );

		if ( !flowing || server->rejected ( ) ) {

			note.add ( L" (pool rejected " ).number ( server->rejected ( ) ).add ( L", jobs " ).number ( static_cast < uint64_t > ( count_logs ( view, L"New job " ) ) );
			note.add ( L", reseeds " ).number ( static_cast < uint64_t > ( count_logs ( view, L"The RandomX seed changed" ) ) ).add ( L", dataset " ).number ( view.dataset ).add ( L", link " ).number ( view.link ).put ( L')' );

		}
		check ( flowing && rejected == 0 && server->accepted ( ) >= accepted && server->rejected ( ) == 0 && ( !gpu || gpu_used ), note.get ( ) );

		delete run;
		server->stop ( );
		delete server;

	}

	static void follow_switches ( ) {

		pool::test_server* server = new pool::test_server;
		pool::server_settings config;

		config.algorithm = chain::ALGORITHM_RANDOMX;
		config.port = 0;
		config.difficulty = 100.0;
		config.job_seconds = 4;
		config.variant = randomx::VARIANT_WOW;
		config.switch_jobs = 2;

		if ( !server->start ( config ) ) {

			check ( false, L"the switching test pool starts" );
			delete server;

			return;

		}

		while ( !server->ready ( ) )
			base::sleep ( 10 );

		app::session* run = new app::session;
		ui::preferences settings;
		base::writer < wchar_t, 64 > address;
		uint32_t seen = 0;
		uint32_t accepted_before = 0;
		uint32_t accepted_after = 0;

		address.add ( L"127.0.0.1:" ).number ( server->port ( ) );
		settings.algorithm = chain::ALGORITHM_RANDOMX;
		base::copy_text ( settings.pool ( ).address, base::count_of ( settings.pool ( ).address ), address.get ( ) );
		settings.threads = run->processor ( ).threads;
		settings.dataset_megabytes = 0;
		settings.variant = randomx::VARIANT_RX0;
		run->start ( settings );

		bool followed = wait_until ( *run, settings, 40.0, [ & ] ( const ui::snapshot& view ) {

			seen |= 1u << view.variant;

			int32_t switches = count_logs ( view, L"The pool switched to " );

			if ( !switches )
				accepted_before = view.accepted;
			else if ( switches >= 1 && view.accepted > accepted_before )
				accepted_after = view.accepted - accepted_before;

			return accepted_before > 0 && accepted_after > 0 && switches >= 1;

		} );

		const ui::snapshot& view = run->view ( );
		base::text note;

		note.add ( L"Alloy follows a pool that switches from RandomWOW to RandomARQ mid-session and keeps getting shares accepted (" ).number ( accepted_before ).add ( L" before, " ).number ( accepted_after ).add ( L" after, pool rejected " ).number ( server->rejected ( ) ).put ( L')' );
		run->stop ( );
		check ( followed && server->rejected ( ) == 0 && ( seen & ( 1u << randomx::VARIANT_WOW ) ) && view.rejected == 0, note.get ( ) );
		delete run;
		server->stop ( );
		delete server;

	}

	static bool ask_api ( uint16_t port, const char* host, const char* path, char* status, char* body, int32_t room ) {

		net::connection link;
		base::line request;
		bool past_headers = false;
		bool first = true;

		status [ 0 ] = 0;
		body [ 0 ] = 0;

		if ( !link.open ( "127.0.0.1", port, 2000 ) )
			return false;

		request.add ( "GET " ).add ( path ).add ( " HTTP/1.1\r\nHost: " ).add ( host ).add ( "\r\n\r\n" );
		link.send ( request.get ( ), request.size ( ) );

		double began = base::seconds ( );
		char line [ 4096 ];

		while ( base::seconds ( ) - began < 3.0 && link.is_open ( ) ) {

			line [ 0 ] = 1;

			int32_t size = link.read_line ( line, sizeof ( line ), 100 );

			if ( size < 0 )
				break;

			if ( size == 0 && line [ 0 ] == 0 )
				past_headers = true;
			else if ( size > 0 && first ) {

				base::copy_text ( status, room, line );
				first = false;

			} else if ( size > 0 && past_headers )
				base::copy_text ( body, room, line );

		}

		return status [ 0 ] != 0;

	}

	static void solo_on ( uint8_t algorithm, const wchar_t* wallet, const wchar_t* secret, uint32_t wanted, double limit ) {

		pool::test_server* server = new pool::test_server;
		pool::server_settings config;
		base::text note;

		config.algorithm = algorithm;
		config.port = 0;
		config.node = true;

		if ( !server->start ( config ) ) {

			check ( false, L"the test node starts" );
			delete server;

			return;

		}

		double began = base::seconds ( );

		while ( !server->ready ( ) && base::seconds ( ) - began < 10.0 )
			base::sleep ( 10 );

		app::session* run = new app::session;
		ui::preferences settings;
		base::writer < wchar_t, 64 > address;

		address.add ( L"http://127.0.0.1:" ).number ( server->port ( ) );
		settings.algorithm = algorithm;
		base::copy_text ( settings.pool ( ).address, base::count_of ( settings.pool ( ).address ), address.get ( ) );
		base::copy_text ( settings.pool ( ).worker, base::count_of ( settings.pool ( ).worker ), wallet );
		base::copy_text ( settings.pool ( ).password, base::count_of ( settings.pool ( ).password ), secret );
		settings.mode = ui::MODE_POOL;
		settings.threads = algorithm == chain::ALGORITHM_RANDOMX ? run->processor ( ).threads : 2u;
		settings.gpu = false;
		settings.dataset_megabytes = 0;
		base::copy_text ( settings.coinbase_tag, base::count_of ( settings.coinbase_tag ), L"/mined with alloy, solo/" );
		run->start ( settings );

		bool known_seen = false;
		bool found = wait_until ( *run, settings, limit, [ & ] ( const ui::snapshot& view ) {

			known_seen = known_seen || view.network_known;

			return view.accepted >= wanted && view.link == ui::PHASE_READY && ( known_seen || algorithm == chain::ALGORITHM_RANDOMX );

		} );

		const ui::snapshot& view = run->view ( );
		uint32_t accepted = view.accepted;
		uint32_t rejected = view.rejected;
		bool known = known_seen || view.network_known || algorithm == chain::ALGORITHM_RANDOMX;

		run->stop ( );
		note.add ( chain::algorithm_name ( algorithm ) ).add ( L" mines solo against a local node that checks every block in full: " ).number ( accepted ).add ( L" blocks accepted, " ).number ( rejected ).add ( L" turned down" );

		if ( !found || server->rejected ( ) )
			note.add ( L" (node rejected " ).number ( server->rejected ( ) ).add ( L", accepted " ).number ( server->accepted ( ) ).add ( L", link " ).number ( view.link ).put ( L')' );

		bool signed_off = algorithm == chain::ALGORITHM_RANDOMX || base::same_text ( server->coinbase_message ( ), "/mined with alloy, solo/" );

		if ( algorithm != chain::ALGORITHM_RANDOMX )
			note.add ( L", each carrying the chosen coinbase message" );

		if ( !known || !signed_off || server->accepted ( ) < accepted ) {

			wchar_t said [ 48 ];

			base::widen ( said, base::count_of ( said ), server->coinbase_message ( ) );
			note.add ( L" (network known " ).number ( known ? 1u : 0u ).add ( L", node accepted " ).number ( server->accepted ( ) ).add ( L", message " ).add ( said ).put ( L')' );

		}

		check ( found && rejected == 0 && server->rejected ( ) == 0 && server->accepted ( ) >= accepted && known && signed_off, note.get ( ) );
		delete run;
		server->stop ( );
		delete server;

	}

	static void solo_refused ( ) {

		pool::test_server* server = new pool::test_server;
		pool::server_settings config;

		config.algorithm = chain::ALGORITHM_SHA256D;
		config.port = 0;
		config.node = true;
		server->start ( config );

		double began = base::seconds ( );

		while ( !server->ready ( ) && base::seconds ( ) - began < 10.0 )
			base::sleep ( 10 );

		app::session* run = new app::session;
		ui::preferences settings;
		base::writer < wchar_t, 64 > address;

		address.add ( L"http://127.0.0.1:" ).number ( server->port ( ) );
		base::copy_text ( settings.pool ( ).address, base::count_of ( settings.pool ( ).address ), address.get ( ) );
		base::copy_text ( settings.pool ( ).worker, base::count_of ( settings.pool ( ).worker ), L"bc1qw508d6qejxtdg4y5r3zarvary0c5xw7kv8f3t4" );
		base::copy_text ( settings.pool ( ).password, base::count_of ( settings.pool ( ).password ), L"alloy:wrong" );
		settings.mode = ui::MODE_POOL;
		settings.threads = 1;
		settings.gpu = false;
		run->start ( settings );

		bool refused = wait_until ( *run, settings, 10.0, [ & ] ( const ui::snapshot& view ) {

			return view.link == ui::PHASE_REFUSED && count_logs ( view, L"The node refused the RPC login" ) > 0;

		} );

		run->stop ( );
		check ( refused, L"a node that refuses the RPC password stops solo mining with an explanation" );
		delete run;
		server->stop ( );
		delete server;

	}

	void pool_tests ( ) {

		section ( L"stratum pool" );

		pool::test_server* server = new pool::test_server;
		pool::server_settings config;

		config.port = 0;
		config.difficulty = 0.002;
		config.job_seconds = 2;

		bool listening = server->start ( config );

		check ( listening && server->port ( ) != 0, L"the test pool listens on a free loopback port" );

		if ( !listening ) {

			delete server;

			return;

		}

		raw_client* client = new raw_client;
		bool linked = client->link.open ( "127.0.0.1", server->port ( ), 2000 );

		client->link.send_line ( "{\"id\":1,\"method\":\"mining.subscribe\",\"params\":[\"tester\"]}" );

		bool subscribed = linked && client->answer ( 1, 3.0 );
		pool::session_nonce nonce;
		int32_t first = client->message.item ( client->message.member ( 0, "result" ), 1 );

		nonce.first_size = client->message.raw_size ( first ) / 2;
		nonce.second_size = static_cast < int32_t > ( client->message.number ( client->message.item ( client->message.member ( 0, "result" ), 2 ) ) );
		subscribed = subscribed && nonce.first_size == 4 && nonce.second_size == 4 && base::read_hex ( client->message.raw ( first ), 8, nonce.first, 4 );
		client->link.send_line ( "{\"id\":2,\"method\":\"mining.authorize\",\"params\":[\"tester\",\"x\"]}" );

		bool authorized = subscribed && client->answer ( 2, 3.0 ) && client->accepted ( );
		pool::job work;
		bool notified = authorized && client->wait_for ( "mining.notify", 3.0 ) && pool::parse_notify ( client->message, client->message.member ( 0, "params" ), work );

		check ( notified && work.branch_count == 3 && work.clean, L"the pool answers subscribe and authorize, then sends a job with three merkle branches" );

		uint8_t target [ 32 ];
		uint8_t second [ 4 ] = { 1, 2, 3, 4 };
		chain::header header;
		chain::header_hasher hasher;
		uint8_t digest [ 32 ];
		uint32_t winner = 0;
		bool mined = false;

		base::processor cpu;

		base::detect ( cpu );
		chain::target_for ( config.difficulty, target );
		pool::build_header ( work, nonce, second, work.time, header );
		hasher.prepare ( header, cpu.sha && cpu.sse41 && cpu.ssse3 ? hash::sha256_block_extensions : hash::sha256_block );

		for ( uint32_t candidate = 0; candidate < 200000000 && !mined; candidate++ ) {

			hasher.hash ( candidate, digest );

			if ( chain::meets ( digest, target ) ) {

				winner = candidate;
				mined = true;

			}

		}

		uint32_t loser = winner + 1;

		hasher.hash ( loser, digest );

		while ( chain::meets ( digest, target ) )
			hasher.hash ( ++loser, digest );

		bool low = notified && submit ( *client, 10, work.id, "01020304", work.time, loser ) && client->error_code ( ) == 23;
		bool missing = notified && submit ( *client, 11, "nope", "01020304", work.time, winner ) && client->error_code ( ) == 21;
		bool good = mined && submit ( *client, 12, work.id, "01020304", work.time, winner ) && client->accepted ( );
		bool repeated = good && submit ( *client, 13, work.id, "01020304", work.time, winner ) && client->error_code ( ) == 22;

		check ( low, L"a share above the target is rejected as low difficulty" );
		check ( missing, L"a share for an unknown job is rejected" );
		check ( good, L"a share mined against the job is accepted after the pool rebuilds the header" );
		check ( repeated, L"the same share sent twice is rejected as a duplicate" );
		delete client;

		app::session* run = new app::session;
		ui::preferences settings;
		base::writer < wchar_t, 64 > address;

		address.add ( L"127.0.0.1:" ).number ( server->port ( ) );
		base::copy_text ( settings.pool ( ).address, base::count_of ( settings.pool ( ).address ), address.get ( ) );
		settings.mode = ui::MODE_POOL;
		settings.threads = base::min ( run->processor ( ).threads, 4u );
		settings.gpu = false;
		run->start ( settings );

		bool flowing = wait_until ( *run, settings, 20.0, [ ] ( const ui::snapshot& view ) {

			int32_t jobs = 0;

			for ( int32_t i = 0; i < view.log_count; i++ )
				jobs += base::starts_with ( view.logs [ i ].text, L"New job " ) ? 1 : 0;

			return view.accepted >= 6 && jobs >= 2 && view.link == ui::PHASE_READY;

		} );

		const ui::snapshot& view = run->view ( );
		uint32_t accepted = view.accepted;
		uint32_t rejected = view.rejected;

		run->stop ( );

		base::text note;

		note.add ( L"the session mines on the test pool across job changes: " ).number ( accepted ).add ( L" accepted, " ).number ( rejected ).add ( L" rejected, " ).number ( view.stale ).add ( L" stale" );
		check ( flowing && rejected == 0, note.get ( ) );
		check ( server->accepted ( ) >= accepted + 1, L"the pool's own count covers every share the miner saw accepted" );

		run->clear_log ( );
		base::copy_text ( settings.pool ( ).address, base::count_of ( settings.pool ( ).address ), L"127.0.0.1:1" );
		run->start ( settings );

		bool retrying = wait_until ( *run, settings, 8.0, [ ] ( const ui::snapshot& view ) {

			return view.link == ui::PHASE_RETRYING;

		} );

		run->stop ( );
		check ( retrying, L"an unreachable pool backs off and retries" );

		run->clear_log ( );
		base::copy_text ( settings.pool ( ).backup, base::count_of ( settings.pool ( ).backup ), address.get ( ) );
		run->start ( settings );

		bool failed_over = wait_until ( *run, settings, 30.0, [ ] ( const ui::snapshot& view ) {

			return view.on_backup && view.accepted >= 2 && view.link == ui::PHASE_READY;

		} );

		run->stop ( );
		check ( failed_over, L"after three failed tries on the main pool the miner moves to the backup pool and gets shares accepted" );

		run->clear_log ( );
		settings.failover_tries = 1;
		run->start ( settings );

		bool quick = wait_until ( *run, settings, 15.0, [ ] ( const ui::snapshot& view ) {

			return view.on_backup && view.accepted >= 1 && view.link == ui::PHASE_READY;

		} );

		int32_t misses = count_logs ( run->view ( ), L"Couldn't reach" );

		run->stop ( );
		check ( quick && misses == 1, L"with tries before the backup set to one, a single failed connection moves the miner to the backup pool" );
		settings.failover_tries = 3;
		settings.pool ( ).backup [ 0 ] = 0;
		base::copy_text ( settings.pool ( ).address, base::count_of ( settings.pool ( ).address ), address.get ( ) );
		base::copy_text ( settings.pool ( ).difficulty, base::count_of ( settings.pool ( ).difficulty ), L"0.0005" );
		run->clear_log ( );
		run->start ( settings );

		bool suggested = wait_until ( *run, settings, 20.0, [ ] ( const ui::snapshot& view ) {

			return view.accepted >= 2 && base::abs ( view.difficulty - 0.0005 ) < 1e-9 && view.link == ui::PHASE_READY;

		} );

		uint32_t suggested_rejected = run->view ( ).rejected;
		base::text granted;

		granted.add ( L"a start difficulty goes to the pool as mining.suggest_difficulty and shares follow the difficulty it grants" );

		if ( !suggested ) {

			granted.add ( L" (difficulty " ).decimal ( run->view ( ).difficulty, 6 ).add ( L", accepted " ).number ( run->view ( ).accepted ).add ( L", link " ).number ( run->view ( ).link );

			for ( int32_t i = base::max ( 0, run->view ( ).log_count - 4 ); i < run->view ( ).log_count; i++ )
				granted.add ( L"; " ).add ( run->view ( ).logs [ i ].text );

			granted.put ( L')' );

		}

		run->stop ( );
		check ( suggested && suggested_rejected == 0, granted.get ( ) );
		settings.pool ( ).difficulty [ 0 ] = 0;

		base::copy_text ( settings.pool ( ).address, base::count_of ( settings.pool ( ).address ), L"no port here" );
		run->start ( settings );

		bool refused = wait_until ( *run, settings, 3.0, [ ] ( const ui::snapshot& view ) {

			return view.link == ui::PHASE_REFUSED;

		} );

		run->stop ( );
		check ( refused, L"an address without a port is refused with an explanation" );

		delete run;
		server->stop ( );
		delete server;

		app::status_api* api = new app::status_api;
		char status [ 256 ];
		char body [ 4096 ];
		static const char json [ ] = "{\"hashrate\":{\"total\":1234.5}}\n";
		app::api_options plain;
		bool served = api->start ( plain );
		base::writer < char, 32 > host;

		host.add ( "127.0.0.1:" ).number ( api->port ( ) );
		api->publish ( json, base::length ( json ) );
		served = served && ask_api ( api->port ( ), host.get ( ), "/api.json", status, body, sizeof ( body ) ) && base::starts_with ( status, "HTTP/1.1 200" ) && base::same_text ( body, "{\"hashrate\":{\"total\":1234.5}}" );

		bool guarded = ask_api ( api->port ( ), "attacker.example", "/api.json", status, body, sizeof ( body ) ) && base::starts_with ( status, "HTTP/1.1 403" ) && !body [ 0 ];
		bool unknown_path = ask_api ( api->port ( ), host.get ( ), "/settings", status, body, sizeof ( body ) ) && base::starts_with ( status, "HTTP/1.1 404" );

		check ( served, L"the stats API serves the latest JSON on 127.0.0.1" );
		check ( guarded && unknown_path, L"it refuses requests whose Host isn't this machine, so web pages can't read it, and knows only its own path" );

		app::api_options open;
		net::http_reply reply;
		base::writer < char, 96 > url;
		base::writer < char, 64 > control;
		base::writer < char, 64 > wrong;

		open.everyone = true;
		open.control = true;
		base::copy_text ( open.token, sizeof ( open.token ), "0123456789abcdef0123456789abcdef" );
		api->stop ( );
		api->start ( open );
		api->publish ( json, base::length ( json ) );
		url.add ( "http://127.0.0.1:" ).number ( api->port ( ) );
		control.add ( "Authorization: Bearer " ).add ( open.token ).add ( "\r\n" );
		wrong.add ( "Authorization: Bearer 0123456789abcdef0123456789abcdee\r\n" );

		base::writer < char, 128 > summary;
		base::writer < char, 128 > pause;
		base::writer < char, 128 > page;

		summary.add ( "http://127.0.0.2:" ).number ( api->port ( ) ).add ( "/api.json" );
		pause.add ( url.get ( ) ).add ( "/1/pause" );
		page.add ( url.get ( ) ).add ( "/" );

		bool by_token = net::http_request ( "GET", summary.get ( ), control.get ( ), nullptr, 0, reply ) && reply.status == 200 && base::same_text ( reply.body.data ( ), json );
		bool bad_token = net::http_request ( "GET", summary.get ( ), wrong.get ( ), nullptr, 0, reply ) && reply.status == 401;
		bool tokenless = net::http_request ( "POST", pause.get ( ), nullptr, "", 0, reply ) && reply.status == 401 && api->take_command ( ) == app::API_NONE;
		bool ordered = net::http_request ( "POST", pause.get ( ), control.get ( ), "", 0, reply ) && reply.status == 200 && api->take_command ( ) == app::API_PAUSE;
		bool dashboard = net::http_get ( page.get ( ), reply ) && reply.status == 200 && base::starts_with ( reply.body.data ( ), "<!doctype html>" );

		check ( by_token && bad_token, L"with a token the API answers any host, which is how other rigs and the web dashboard read it, and a wrong token gets 401" );
		check ( tokenless && ordered, L"pause, resume, start and stop need the token, then reach the session" );
		check ( dashboard, L"the API serves its own web dashboard, read back through Alloy's HTTP client" );

		open.control = false;
		api->stop ( );
		api->start ( open );

		pause.clear ( );
		pause.add ( "http://127.0.0.1:" ).number ( api->port ( ) ).add ( "/1/pause" );

		bool closed = net::http_request ( "POST", pause.get ( ), control.get ( ), "", 0, reply ) && reply.status == 403;

		check ( closed, L"with control off even the right token can't pause mining" );
		delete api;

		mine_on ( chain::ALGORITHM_SCRYPT, 2.0, 0, true, 8, 25.0 );
		mine_on ( chain::ALGORITHM_RANDOMX, 1000.0, 2, false, 8, 45.0, true );
		mine_on ( chain::ALGORITHM_KHEAVYHASH, 0.05, 0, true, 8, 25.0 );
		mine_on ( chain::ALGORITHM_ETHASH, 0.005, 0, true, 8, 40.0 );
		solo_on ( chain::ALGORITHM_SHA256D, L"bc1qw508d6qejxtdg4y5r3zarvary0c5xw7kv8f3t4", L"alloy:secret", 3, 30.0 );
		solo_on ( chain::ALGORITHM_RANDOMX, L"44AFFq5kSiGBoZ4NMDwYtN18obc8AemS33DBLWs3H7otXft3XjrpDtQGv7SqSsaBYBb98uNbr2VBBEt7f2wfn3RVGQBEP3A", L"", 3, 40.0 );
		solo_refused ( );
		follow_switches ( );

	}

}

#endif
