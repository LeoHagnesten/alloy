#include "pool/server.hh"
#include "chain/target.hh"
#include "net/socket.hh"
#include "base/bits.hh"
#include "base/clock.hh"
#include "base/console.hh"
#include "base/list.hh"
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

	};

	struct job_book {

		job jobs [ kept_jobs ];
		uint32_t made = 0;

		const job& newest ( ) const {

			return jobs [ ( made - 1 ) % kept_jobs ];

		}

		const job* find ( const char* id ) const {

			for ( uint32_t i = 0; i < kept_jobs && i < made; i++ ) {

				if ( base::same_text ( jobs [ i ].id, id ) )
					return &jobs [ i ];

			}

			return nullptr;

		}

	};

	static void make_job ( job_book& book ) {

		uint32_t height = 840000 + book.made;
		job& work = book.jobs [ book.made % kept_jobs ];
		base::writer < char, 16 > name;
		uint8_t seed [ 16 ];
		uint8_t digest [ 32 ];

		book.made++;
		work = job { };
		name.number ( book.made );
		base::copy_text ( work.id, sizeof ( work.id ), name.get ( ) );
		base::store_little ( seed, book.made );
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
		work.bits = 0x1703A30C;
		work.time = base::unix_time ( );
		work.clean = true;

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

	static void send_work ( peer& client, const job_book& book, double difficulty ) {

		base::line out;

		out.add ( "{\"id\":null,\"method\":\"mining.set_difficulty\",\"params\":[" ).decimal ( difficulty, 6 ).add ( "]}" );
		client.link.send_line ( out.get ( ) );
		out.clear ( );
		write_notify ( out, book.newest ( ) );
		client.link.send_line ( out.get ( ) );

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

		net::listener& listener = *static_cast < net::listener* > ( socket );
		peer* peers = new peer [ max_peers ];
		job_book* book = new job_book;
		base::json* message = new base::json;
		char* text = new char [ net::line_room ];
		uint32_t sessions = 0;
		double last_job = base::seconds ( );
		uint8_t target [ 32 ];

		make_job ( *book );
		chain::target_for ( config.difficulty, target );

		while ( !base::atomic_load ( quit ) ) {

			bool busy = false;

			for ( int32_t i = 0; i < max_peers; i++ ) {

				peer& client = peers [ i ];

				if ( client.used || !listener.accept ( client.link, 0 ) )
					continue;

				sessions++;
				client.used = true;
				client.authorized = false;
				client.seen_count = 0;
				client.nonce = session_nonce { };
				client.nonce.first_size = 4;
				base::store_big ( client.nonce.first, 0xA1100000 + sessions );
				base::atomic_add ( connected, 1u );
				busy = true;

				break;

			}

			for ( int32_t i = 0; i < max_peers; i++ ) {

				peer& client = peers [ i ];
				int32_t size = 0;

				if ( !client.used )
					continue;

				while ( ( size = client.link.read_line ( text, net::line_room, 0 ) ) > 0 ) {

					busy = true;

					if ( !message->parse ( text, size ) )
						continue;

					int32_t id = message->member ( 0, "id" );
					int32_t method = message->member ( 0, "method" );
					int32_t params = message->member ( 0, "params" );

					if ( message->text_is ( method, "mining.subscribe" ) ) {

						base::line result;

						result.add ( "[[[\"mining.set_difficulty\",\"alloy\"],[\"mining.notify\",\"alloy\"]],\"" ).hex ( client.nonce.first, 4, false ).add ( "\",4]" );
						reply ( client, *message, id, result.get ( ), 0, nullptr );

					} else if ( message->text_is ( method, "mining.authorize" ) ) {

						char worker [ 64 ];

						message->text ( message->item ( params, 0 ), worker, sizeof ( worker ) );
						base::widen ( client.worker, base::count_of ( client.worker ), worker );
						client.authorized = true;
						reply ( client, *message, id, "true", 0, nullptr );
						send_work ( client, *book, config.difficulty );

					} else if ( message->text_is ( method, "mining.extranonce.subscribe" ) )
						reply ( client, *message, id, "true", 0, nullptr );
					else if ( message->text_is ( method, "mining.submit" ) ) {

						char job_id [ 64 ];
						uint8_t second [ 8 ];
						uint32_t time = 0;
						uint32_t nonce = 0;
						int32_t second_node = message->item ( params, 2 );
						int32_t time_node = message->item ( params, 3 );
						int32_t nonce_node = message->item ( params, 4 );

						message->text ( message->item ( params, 1 ), job_id, sizeof ( job_id ) );

						const job* work = book->find ( job_id );

						if ( !client.authorized ) {

							reply ( client, *message, id, nullptr, 24, "Unauthorized worker" );
							base::atomic_add ( bad, 1u );

							continue;

						}

						if ( !work ) {

							reply ( client, *message, id, nullptr, 21, "Job not found" );
							base::atomic_add ( bad, 1u );

							continue;

						}

						if ( !base::read_hex ( message->raw ( second_node ), message->raw_size ( second_node ), second, 4 ) || !base::read_hex32 ( message->raw ( time_node ), message->raw_size ( time_node ), time ) || !base::read_hex32 ( message->raw ( nonce_node ), message->raw_size ( nonce_node ), nonce ) ) {

							reply ( client, *message, id, nullptr, 20, "Malformed share" );
							base::atomic_add ( bad, 1u );

							continue;

						}

						uint64_t key = 1469598103934665603ull;
						uint8_t parts [ 16 ];

						base::copy ( parts, second, 4 );
						base::store_little ( parts + 4, time );
						base::store_little ( parts + 8, nonce );
						base::store_little ( parts + 12, static_cast < uint32_t > ( work - book->jobs ) );

						for ( uint8_t part : parts )
							key = ( key ^ part ) * 1099511628211ull;

						bool repeated = false;

						for ( int32_t k = 0; k < base::min ( client.seen_count, seen_room ); k++ )
							repeated = repeated || client.seen [ k ] == key;

						if ( repeated ) {

							reply ( client, *message, id, nullptr, 22, "Duplicate share" );
							base::atomic_add ( bad, 1u );

							continue;

						}

						client.seen [ client.seen_count++ % seen_room ] = key;

						chain::header header;
						uint8_t digest [ 32 ];

						build_header ( *work, client.nonce, second, time, header );
						header.set_nonce ( nonce );
						hash::sha256_twice ( header.bytes, chain::header_size, digest );

						if ( !chain::meets ( digest, target ) ) {

							reply ( client, *message, id, nullptr, 23, "Low difficulty share" );
							base::atomic_add ( bad, 1u );

							if ( config.verbose )
								base::print_line ( L"  rejected a share below the target" );

							continue;

						}

						reply ( client, *message, id, "true", 0, nullptr );
						base::atomic_add ( good, 1u );

						if ( config.verbose ) {

							base::text note;

							note.add ( L"  share from " ).add ( client.worker ).add ( L", difficulty " ).decimal ( chain::difficulty_of ( chain::top_bits ( digest ) ), 3 ).add ( L", hash " ).hex ( digest, 32, true );
							base::print_line ( note.get ( ) );

						}

					} else
						reply ( client, *message, id, nullptr, 20, "Unknown method" );

				}

				if ( size < 0 || !client.link.is_open ( ) ) {

					client.link.close ( );
					client.used = false;
					client.authorized = false;
					base::atomic_add ( connected, ~0u );

				}

			}

			if ( base::seconds ( ) - last_job >= static_cast < double > ( config.job_seconds ) ) {

				make_job ( *book );
				last_job = base::seconds ( );

				for ( int32_t i = 0; i < max_peers; i++ ) {

					if ( peers [ i ].used && peers [ i ].authorized )
						send_work ( peers [ i ], *book, config.difficulty );

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

		delete [ ] text;
		delete message;
		delete book;
		delete [ ] peers;

	}

}
