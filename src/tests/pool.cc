#ifdef ALLOY_DEBUG

#include "tests/harness.hh"
#include "app/session.hh"
#include "pool/server.hh"
#include "net/socket.hh"
#include "chain/target.hh"
#include "base/clock.hh"
#include "base/json.hh"
#include "base/memory.hh"
#include "base/strings.hh"
#include "base/system.hh"
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

	static void mine_on ( uint8_t algorithm, double difficulty, uint32_t reseed, bool gpu, uint32_t wanted, double limit ) {

		pool::test_server* server = new pool::test_server;
		pool::server_settings config;
		base::text note;

		config.algorithm = algorithm;
		config.port = 0;
		config.difficulty = difficulty;
		config.job_seconds = 3;
		config.reseed_jobs = reseed;

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
		settings.full_dataset = true;
		run->start ( settings );

		bool flowing = wait_until ( *run, settings, limit, [ & ] ( const ui::snapshot& view ) {

			bool reseeded = !reseed || ( count_logs ( view, L"The RandomX seed changed" ) > 0 && view.dataset == ui::DATASET_FULL );

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
			note.add ( reseed ? L" through a seed change" : L"" ).add ( L" with the full dataset" );
		else if ( gpu )
			note.add ( L" with the CPU and GPU" );

		note.add ( L": " ).number ( accepted ).add ( L" accepted, " ).number ( rejected ).add ( L" rejected, " ).number ( stale ).add ( L" stale at " ).scaled ( rate, L"H/s" );
		check ( flowing && rejected == 0 && server->accepted ( ) >= accepted && server->rejected ( ) == 0 && ( !gpu || gpu_used ), note.get ( ) );

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

		mine_on ( chain::ALGORITHM_SCRYPT, 2.0, 0, true, 8, 25.0 );
		mine_on ( chain::ALGORITHM_RANDOMX, 100.0, 2, false, 8, 45.0 );

	}

}

#endif
