#include "pool/test_pool.hh"
#include "randomx/machine.hh"
#include "chain/merkle.hh"
#include "chain/target.hh"
#include "hash/sha256.hh"
#include "net/socket.hh"
#include "base/bits.hh"
#include "base/system/clock.hh"
#include "base/containers/list.hh"
#include "base/memory.hh"
#include "base/strings.hh"
#include "base/text.hh"

namespace pool {

	static constexpr int32_t fake_transactions = 3;
	static const char node_login [ ] = "Basic YWxsb3k6c2VjcmV0";
	static const char payout [ ] = "0014751e76e8199196d454941c45d1b3a323f1433bd6";

	struct node_state {

		uint8_t tip [ 32 ] = { };
		uint64_t height = 840000;
		uint8_t transactions [ fake_transactions ] [ 61 ] = { };
		uint8_t seed [ 32 ] = { };
		uint64_t difficulty = 400;
		randomx::cache* cache = nullptr;
		randomx::machine* machine = nullptr;
		char* message = nullptr;

	};

	static bool read_request ( net::connection& link, base::list < char >& body, bool& authorized ) {

		base::list < char > raw;
		char chunk [ 4096 ];
		double began = base::seconds ( );
		int32_t head = -1;
		int64_t length = 0;

		authorized = false;

		while ( base::seconds ( ) - began < 5.0 ) {

			int32_t got = link.read ( chunk, sizeof ( chunk ), 50 );

			if ( got < 0 )
				return false;

			raw.append ( chunk, got );

			if ( head < 0 ) {

				for ( int32_t i = 0; i + 3 < raw.size ( ); i++ ) {

					if ( raw.data ( ) [ i ] == '\r' && raw.data ( ) [ i + 1 ] == '\n' && raw.data ( ) [ i + 2 ] == '\r' && raw.data ( ) [ i + 3 ] == '\n' ) {

						head = i + 4;

						break;

					}

				}

				if ( head < 0 )
					continue;

				raw.push ( 0 );

				const char* text = raw.data ( );

				for ( int32_t i = 0; i < head; i++ ) {

					if ( i && text [ i - 1 ] != '\n' )
						continue;

					if ( base::same_span ( text + i, 16, "Content-Length: " ) )
						length = static_cast < int64_t > ( base::read_number ( text + i + 16 ) );

					if ( base::same_span ( text + i, 15, "Authorization: " ) && base::same_span ( text + i + 15, base::length ( node_login ), node_login ) )
						authorized = true;

				}

				raw.set_size ( raw.size ( ) - 1 );

			}

			if ( head >= 0 && raw.size ( ) - head >= length ) {

				body.append ( raw.data ( ) + head, static_cast < int32_t > ( length ) );
				body.push ( 0 );

				return true;

			}

		}

		return false;

	}

	static void answer ( net::connection& link, int32_t status, const char* json, int32_t size ) {

		base::writer < char, 256 > head;

		head.add ( "HTTP/1.1 " ).number ( static_cast < uint64_t > ( status ) ).add ( status == 200 ? " OK" : " Unauthorized" ).add ( "\r\nContent-Type: application/json\r\nContent-Length: " ).number ( static_cast < uint64_t > ( size ) ).add ( "\r\nConnection: close\r\n\r\n" );
		link.send ( head.get ( ), head.size ( ) );
		link.send ( json, size );

	}

	static uint64_t read_varint ( const uint8_t* bytes, int32_t size, int32_t& at ) {

		if ( at >= size )
			return 0;

		uint8_t lead = bytes [ at++ ];

		if ( lead < 0xFD )
			return lead;

		int32_t count = lead == 0xFD ? 2 : ( lead == 0xFE ? 4 : 8 );
		uint64_t value = 0;

		for ( int32_t i = 0; i < count && at < size; i++ )
			value |= static_cast < uint64_t > ( bytes [ at++ ] ) << ( i * 8 );

		return value;

	}

	static bool check_block ( node_state& state, const uint8_t* block, int32_t size, const char*& reason ) {

		reason = "bad-blk-length";

		if ( size < 81 )
			return false;

		chain::header header;
		uint8_t digest [ 32 ];
		uint8_t target [ 32 ];

		base::copy ( header.bytes, block, chain::header_size );
		hash::sha256_twice ( header.bytes, chain::header_size, digest );
		chain::target_of ( header.bits ( ), target );

		reason = "bad-prevblk";

		if ( !base::same ( header.previous ( ), state.tip, 32 ) )
			return false;

		reason = "high-hash";

		if ( !chain::meets ( digest, target ) )
			return false;

		int32_t at = chain::header_size;
		uint64_t count = read_varint ( block, size, at );

		reason = "bad-txns-count";

		if ( count != fake_transactions + 1 )
			return false;

		bool witness = at + 6 < size && block [ at + 4 ] == 0 && block [ at + 5 ] == 1;
		base::list < uint8_t > plain;

		plain.append ( block + at, 4 );
		at += witness ? 6 : 4;

		int32_t body_start = at;

		reason = "bad-cb-length";

		if ( read_varint ( block, size, at ) != 1 )
			return false;

		at += 36;

		uint64_t script_size = read_varint ( block, size, at );
		int32_t script_at = at;

		at += static_cast < int32_t > ( script_size ) + 4;

		uint64_t outputs = read_varint ( block, size, at );
		int32_t first_output = at;

		for ( uint64_t i = 0; i < outputs && at < size; i++ ) {

			at += 8;
			at += static_cast < int32_t > ( read_varint ( block, size, at ) );

		}

		int32_t body_end = at;

		if ( witness ) {

			uint64_t items = read_varint ( block, size, at );

			for ( uint64_t i = 0; i < items && at < size; i++ )
				at += static_cast < int32_t > ( read_varint ( block, size, at ) );

		}

		if ( at + 4 > size )
			return false;

		plain.append ( block + body_start, body_end - body_start );
		plain.append ( block + at, 4 );
		at += 4;

		uint8_t height_bytes [ 4 ] = { 0x03, static_cast < uint8_t > ( state.height ), static_cast < uint8_t > ( state.height >> 8 ), static_cast < uint8_t > ( state.height >> 16 ) };

		reason = "bad-cb-height";

		if ( script_size < 4 || !base::same ( block + script_at, height_bytes, 4 ) )
			return false;

		uint8_t script [ 32 ];
		int32_t output_at = first_output;
		uint64_t value = base::load_little64 ( block + output_at );

		output_at += 8;

		int32_t paid_size = static_cast < int32_t > ( read_varint ( block, size, output_at ) );

		base::read_hex ( payout, script, 22 );
		reason = "bad-cb-amount";

		if ( value != 312500000 || paid_size != 22 || !base::same ( block + output_at, script, 22 ) )
			return false;

		uint8_t leaves [ ( fake_transactions + 1 ) * 32 ];

		hash::sha256_twice ( plain.data ( ), static_cast < size_t > ( plain.size ( ) ), leaves );

		for ( int32_t i = 0; i < fake_transactions; i++ ) {

			reason = "bad-txns";

			if ( at + 61 > size || !base::same ( block + at, state.transactions [ i ], 61 ) )
				return false;

			hash::sha256_twice ( block + at, 61, leaves + ( i + 1 ) * 32 );
			at += 61;

		}

		int32_t width = fake_transactions + 1;
		uint8_t level [ ( fake_transactions + 2 ) * 32 ];

		base::copy ( level, leaves, sizeof ( leaves ) );

		while ( width > 1 ) {

			if ( width % 2 ) {

				base::copy ( level + width * 32, level + ( width - 1 ) * 32, 32 );
				width++;

			}

			for ( int32_t i = 0; i < width; i += 2 )
				hash::sha256_twice ( level + i * 32, 64, level + ( i / 2 ) * 32 );

			width /= 2;

		}

		reason = "bad-txnmrklroot";

		if ( !base::same ( level, header.merkle_root ( ), 32 ) || at != size )
			return false;

		int32_t said = script_size > 14 ? base::min ( static_cast < int32_t > ( block [ script_at + 13 ] ), static_cast < int32_t > ( script_size ) - 14 ) : 0;

		if ( state.message ) {

			base::copy ( state.message, block + script_at + 14, static_cast < size_t > ( base::min ( said, 47 ) ) );
			state.message [ base::min ( said, 47 ) ] = 0;

		}

		base::copy ( state.tip, digest, 32 );
		state.height++;
		reason = nullptr;

		return true;

	}

	static void bitcoin_reply ( node_state& state, const base::json& message, base::line& out, volatile uint32_t& accepted_count, volatile uint32_t& rejected_count ) {

		int32_t method = message.member ( 0, "method" );
		uint8_t display [ 32 ];

		for ( int32_t i = 0; i < 32; i++ )
			display [ i ] = state.tip [ 31 - i ];

		if ( message.text_is ( method, "getbestblockhash" ) ) {

			out.add ( "{\"result\":\"" ).hex ( display, 32, false ).add ( "\",\"error\":null,\"id\":\"alloy\"}" );

			return;

		}

		if ( message.text_is ( method, "getblocktemplate" ) ) {

			out.add ( "{\"result\":{\"version\":536870912,\"previousblockhash\":\"" ).hex ( display, 32, false ).add ( "\",\"transactions\":[" );

			for ( int32_t i = 0; i < fake_transactions; i++ ) {

				uint8_t id [ 32 ];
				uint8_t shown [ 32 ];

				hash::sha256_twice ( state.transactions [ i ], 61, id );

				for ( int32_t k = 0; k < 32; k++ )
					shown [ k ] = id [ 31 - k ];

				out.add ( i ? ",{\"data\":\"" : "{\"data\":\"" ).hex ( state.transactions [ i ], 61, false ).add ( "\",\"txid\":\"" ).hex ( shown, 32, false ).add ( "\",\"fee\":1000}" );

			}

			out.add ( "],\"coinbasevalue\":312500000,\"bits\":\"1e00ffff\",\"curtime\":" ).number ( base::unix_time ( ) ).add ( ",\"height\":" ).number ( state.height );
			out.add ( ",\"default_witness_commitment\":\"6a24aa21a9ed" );

			uint8_t commitment [ 32 ];

			hash::sha256_twice ( state.tip, 32, commitment );
			out.hex ( commitment, 32, false ).add ( "\"},\"error\":null,\"id\":\"alloy\"}" );

			return;

		}

		if ( message.text_is ( method, "submitblock" ) ) {

			int32_t hex = message.item ( message.member ( 0, "params" ), 0 );
			int32_t size = message.raw_size ( hex ) / 2;
			base::list < uint8_t > block;
			const char* reason = "bad-hex";

			block.set_size ( size );

			bool good = size > 0 && base::read_hex ( message.raw ( hex ), size * 2, block.data ( ), static_cast < size_t > ( size ) ) && check_block ( state, block.data ( ), size, reason );

			base::atomic_add ( good ? accepted_count : rejected_count, 1u );

			if ( good )
				out.add ( "{\"result\":null,\"error\":null,\"id\":\"alloy\"}" );
			else
				out.add ( "{\"result\":\"" ).add ( reason ).add ( "\",\"error\":null,\"id\":\"alloy\"}" );

			return;

		}

		out.add ( "{\"result\":null,\"error\":{\"code\":-32601,\"message\":\"Method not found\"},\"id\":\"alloy\"}" );

	}

	static void monero_reply ( node_state& state, const base::json& message, base::line& out, volatile uint32_t& accepted_count, volatile uint32_t& rejected_count ) {

		int32_t method = message.member ( 0, "method" );
		uint8_t blob [ 96 ];

		blob [ 0 ] = 16;
		blob [ 1 ] = 16;

		for ( int32_t i = 0; i < 5; i++ )
			blob [ 2 + i ] = static_cast < uint8_t > ( ( ( 1700000000u + state.height ) >> ( i * 7 ) ) & 0x7F ) | ( i < 4 ? 0x80 : 0 );

		base::copy ( blob + 7, state.tip, 32 );
		base::zero ( blob + 39, 4 );
		hash::sha256_twice ( state.tip, 32, blob + 43 );
		blob [ 75 ] = 1;

		if ( message.text_is ( method, "get_block_template" ) ) {

			out.add ( "{\"id\":\"0\",\"jsonrpc\":\"2.0\",\"result\":{\"blockhashing_blob\":\"" ).hex ( blob, 76, false ).add ( "\",\"blocktemplate_blob\":\"" ).hex ( blob, 76, false ).add ( "0000a1b2c3d4e5f60708\",\"difficulty\":" ).number ( state.difficulty );
			out.add ( ",\"height\":" ).number ( state.height ).add ( ",\"reserved_offset\":130,\"seed_hash\":\"" ).hex ( state.seed, 32, false ).add ( "\",\"status\":\"OK\"}}" );

			return;

		}

		if ( message.text_is ( method, "submit_block" ) ) {

			int32_t hex = message.item ( message.member ( 0, "params" ), 0 );
			uint8_t sent [ 96 ];
			uint8_t digest [ 32 ];
			bool good = message.raw_size ( hex ) == 172 && base::read_hex ( message.raw ( hex ), 172, sent, 86 );

			if ( good ) {

				base::copy ( blob + 39, sent + 39, 4 );
				good = base::same ( sent, blob, 76 );
				state.machine->hash ( sent, 76, digest );

				unsigned __int128 carry = 0;

				for ( int32_t i = 0; i < 4; i++ ) {

					carry += static_cast < unsigned __int128 > ( base::load_little64 ( digest + i * 8 ) ) * state.difficulty;
					carry >>= 64;

				}

				good = good && carry == 0;

			}

			base::atomic_add ( good ? accepted_count : rejected_count, 1u );

			if ( good ) {

				base::copy ( state.tip, digest, 32 );
				state.height++;
				out.add ( "{\"id\":\"0\",\"jsonrpc\":\"2.0\",\"result\":{\"status\":\"OK\"}}" );

			} else
				out.add ( "{\"id\":\"0\",\"jsonrpc\":\"2.0\",\"error\":{\"code\":-7,\"message\":\"Block not accepted\"}}" );

			return;

		}

		out.add ( "{\"id\":\"0\",\"jsonrpc\":\"2.0\",\"error\":{\"code\":-32601,\"message\":\"Method not found\"}}" );

	}

	void test_server::run_node ( ) {

		net::listener& listener = *static_cast < net::listener* > ( socket );
		node_state* state = new node_state;
		base::json* message = new base::json;
		base::line* out = new base::line;
		bool monero = config.algorithm == chain::ALGORITHM_RANDOMX;

		state->message = coinbase;
		hash::sha256_twice ( "alloy test node", 15, state->tip );
		hash::sha256_twice ( "alloy test seed", 15, state->seed );

		for ( int32_t i = 0; i < fake_transactions; i++ ) {

			for ( int32_t k = 0; k < 61; k++ )
				state->transactions [ i ] [ k ] = static_cast < uint8_t > ( i * 31 + k * 7 + 1 );

		}

		if ( monero ) {

			state->cache = new randomx::cache;
			state->machine = new randomx::machine;
			state->cache->build ( state->seed, 32, config.variant );
			state->machine->open ( state->cache, nullptr, config.variant );

		}

		base::atomic_store ( listening, 1u );

		while ( !base::atomic_load ( quit ) ) {

			net::connection link;

			if ( !listener.accept ( link, 50 ) )
				continue;

			base::list < char > body;
			bool authorized = false;

			if ( !read_request ( link, body, authorized ) ) {

				link.close ( );

				continue;

			}

			base::atomic_add ( connected, 1u );

			if ( !monero && !authorized ) {

				static const char refusal [ ] = "{\"result\":null,\"error\":{\"code\":-1,\"message\":\"Unauthorized\"},\"id\":null}";

				answer ( link, 401, refusal, base::length ( refusal ) );
				link.close ( );

				continue;

			}

			out->clear ( );

			if ( message->parse ( body.data ( ), body.size ( ) - 1 ) ) {

				if ( monero )
					monero_reply ( *state, *message, *out, good, bad );
				else
					bitcoin_reply ( *state, *message, *out, good, bad );

			} else
				out->add ( "{\"result\":null,\"error\":{\"code\":-32700,\"message\":\"Parse error\"},\"id\":null}" );

			base::atomic_store ( made, static_cast < uint32_t > ( state->height ) );
			answer ( link, 200, out->get ( ), out->size ( ) );
			link.close ( );

		}

		delete state->machine;
		delete state->cache;
		delete out;
		delete message;
		delete state;

	}

}
