#ifdef ALLOY_DEBUG

#include "tests/harness.hh"
#include "app/session.hh"
#include "hash/blake2b.hh"
#include "base/system/clock.hh"
#include "hash/heavyhash.hh"
#include "hash/ethash.hh"
#include "hash/keccak.hh"
#include "chain/rlp.hh"
#include "chain/target.hh"
#include "net/http.hh"
#include "base/formats/json.hh"
#include "base/bits.hh"
#include "base/memory.hh"
#include "base/strings.hh"
#include "base/text.hh"

namespace tests {

	static bool fetch_json ( const char* url, net::http_reply& reply, base::json& parsed ) {

		if ( !net::http_get ( url, reply, 15000 ) || reply.status != 200 )
			return false;

		int32_t size = reply.body.size ( );

		while ( size && !reply.body.data ( ) [ size - 1 ] )
			size--;

		return parsed.parse ( reply.body.data ( ), size );

	}

	static uint64_t number_of ( const base::json& parsed, int32_t node ) {

		return base::read_number ( parsed.raw ( node ) );

	}

	static bool hash_of ( const base::json& parsed, int32_t node, uint8_t* out ) {

		return parsed.raw_size ( node ) == 64 && base::read_hex ( parsed.raw ( node ), 64, out, 32 );

	}

	static bool kaspa_header_hash ( const base::json& parsed, int32_t header, uint64_t timestamp, uint64_t nonce, uint8_t* out ) {

		hash::blake2b work ( 32, "BlockHash", 9 );
		uint8_t bytes [ 32 ];
		int32_t parents = parsed.member ( header, "parents" );
		uint64_t levels = static_cast < uint64_t > ( parsed.count ( parents ) );
		uint16_t version = static_cast < uint16_t > ( number_of ( parsed, parsed.member ( header, "version" ) ) );

		base::store_little64 ( bytes, version );
		work.add ( bytes, 2 );
		base::store_little64 ( bytes, levels );
		work.add ( bytes, 8 );

		for ( uint64_t level = 0; level < levels; level++ ) {

			int32_t list = parsed.member ( parsed.item ( parents, static_cast < int32_t > ( level ) ), "parentHashes" );
			uint64_t count = static_cast < uint64_t > ( parsed.count ( list ) );

			base::store_little64 ( bytes, count );
			work.add ( bytes, 8 );

			for ( uint64_t i = 0; i < count; i++ ) {

				if ( !hash_of ( parsed, parsed.item ( list, static_cast < int32_t > ( i ) ), bytes ) )
					return false;

				work.add ( bytes, 32 );

			}

		}

		static const char* const roots [ ] = { "hashMerkleRoot", "acceptedIdMerkleRoot", "utxoCommitment" };

		for ( const char* name : roots ) {

			if ( !hash_of ( parsed, parsed.member ( header, name ), bytes ) )
				return false;

			work.add ( bytes, 32 );

		}

		base::store_little64 ( bytes, timestamp );
		work.add ( bytes, 8 );
		base::store_little ( bytes, static_cast < uint32_t > ( number_of ( parsed, parsed.member ( header, "bits" ) ) ) );
		work.add ( bytes, 4 );
		base::store_little64 ( bytes, nonce );
		work.add ( bytes, 8 );
		base::store_little64 ( bytes, number_of ( parsed, parsed.member ( header, "daaScore" ) ) );
		work.add ( bytes, 8 );
		base::store_little64 ( bytes, number_of ( parsed, parsed.member ( header, "blueScore" ) ) );
		work.add ( bytes, 8 );

		int32_t blue = parsed.member ( header, "blueWork" );
		char digits [ 80 ] = { };
		uint8_t wide [ 40 ] = { };
		int32_t length = parsed.raw_size ( blue );

		if ( length <= 0 || length > 64 )
			return false;

		digits [ 0 ] = '0';
		base::copy_span ( digits + ( length % 2 ), sizeof ( digits ) - 1, parsed.raw ( blue ), length );
		length += length % 2;

		if ( !base::read_hex ( digits, length, wide, static_cast < size_t > ( length / 2 ) ) )
			return false;

		int32_t skip = 0;

		while ( skip < length / 2 && !wide [ skip ] )
			skip++;

		base::store_little64 ( bytes, static_cast < uint64_t > ( length / 2 - skip ) );
		work.add ( bytes, 8 );
		work.add ( wide + skip, static_cast < size_t > ( length / 2 - skip ) );

		if ( !hash_of ( parsed, parsed.member ( header, "pruningPoint" ), bytes ) )
			return false;

		work.add ( bytes, 32 );
		work.end ( out );

		return true;

	}

	static void kaspa_live_block ( ) {

		net::http_reply* reply = new net::http_reply;
		base::json* parsed = new base::json;
		char tip [ 80 ] = { };
		base::text note;

		bool dag = fetch_json ( "https://api.kaspa.org/info/blockdag", *reply, *parsed ) && parsed->text ( parsed->item ( parsed->member ( 0, "tipHashes" ), 0 ), tip, sizeof ( tip ) ) == 64;
		base::line url;

		url.add ( "https://api.kaspa.org/blocks/" ).add ( tip ).add ( "?includeTransactions=false" );

		bool block = dag && fetch_json ( url.get ( ), *reply, *parsed );
		int32_t header = block ? parsed->member ( 0, "header" ) : -1;
		uint8_t wanted [ 32 ] = { };
		uint8_t id [ 32 ] = { };
		uint8_t pre_pow [ 32 ] = { };
		uint8_t proof [ 32 ] = { };
		uint8_t target [ 32 ] = { };
		bool same_id = false;
		bool under = false;

		if ( block && base::read_hex ( tip, 64, wanted, 32 ) ) {

			uint64_t timestamp = number_of ( *parsed, parsed->member ( header, "timestamp" ) );
			uint64_t nonce = number_of ( *parsed, parsed->member ( header, "nonce" ) );
			uint32_t bits = static_cast < uint32_t > ( number_of ( *parsed, parsed->member ( header, "bits" ) ) );
			hash::heavy_hasher* heavy = new hash::heavy_hasher;

			same_id = kaspa_header_hash ( *parsed, header, timestamp, nonce, id ) && base::same ( id, wanted, 32 );

			if ( kaspa_header_hash ( *parsed, header, 0, 0, pre_pow ) ) {

				heavy->prepare ( pre_pow, timestamp );
				heavy->hash ( nonce, proof );
				chain::target_of ( bits, target );
				under = chain::meets ( proof, target );

			}

			delete heavy;

		}

		note.add ( L"a live Kaspa block from api.kaspa.org hashes to its own id and kHeavyHash puts its proof of work under its target" );

		if ( !dag || !block )
			note.add ( L" (api.kaspa.org answered " ).number ( static_cast < uint64_t > ( reply->status ) ).add ( L" with " ).number ( static_cast < uint64_t > ( reply->body.size ( ) ) ).add ( L" bytes)" );
		else if ( !same_id )
			note.add ( L" (the header hash didn't match the block id)" );
		else if ( !under )
			note.add ( L" (the proof of work missed the target)" );

		check ( same_id && under, note.get ( ) );
		delete parsed;
		delete reply;

	}

	static void etc_live_block ( ) {

		static const char request [ ] = "{\"jsonrpc\":\"2.0\",\"id\":1,\"method\":\"eth_getBlockByNumber\",\"params\":[\"latest\",false]}";
		static const char* const sealed [ ] = { "parentHash", "sha3Uncles", "miner", "stateRoot", "transactionsRoot", "receiptsRoot", "logsBloom", "difficulty", "number", "gasLimit", "gasUsed", "timestamp", "extraData" };
		static const bool quantities [ ] = { false, false, false, false, false, false, false, true, true, true, true, true, false };
		net::http_reply* reply = new net::http_reply;
		base::json* parsed = new base::json;
		hash::ethash_cache* cache = new hash::ethash_cache;
		base::text note;
		bool fetched = net::http_request ( "POST", "https://etc.etcdesktop.com", "Content-Type: application/json\r\n", request, sizeof ( request ) - 1, *reply, 15000 ) && reply->status == 200;
		int32_t size = reply->body.size ( );

		while ( size && !reply->body.data ( ) [ size - 1 ] )
			size--;

		fetched = fetched && parsed->parse ( reply->body.data ( ), size );

		int32_t block = fetched ? parsed->member ( 0, "result" ) : -1;
		chain::rlp_list fields;
		base::list < uint8_t > encoded;
		bool packed = block >= 0;

		for ( int32_t i = 0; i < 13 && packed; i++ ) {

			int32_t node = parsed->member ( block, sealed [ i ] );

			packed = node >= 0 && fields.hex ( parsed->raw ( node ), parsed->raw_size ( node ), quantities [ i ] );

		}

		uint8_t seal [ 32 ] = { };
		uint8_t wanted_mix [ 32 ] = { };
		uint8_t nonce_bytes [ 8 ] = { };
		uint8_t mix [ 32 ] = { };
		uint8_t result [ 32 ] = { };
		uint8_t flipped [ 32 ];
		uint8_t target [ 32 ];
		uint64_t number = 0;
		double difficulty = 0.0;
		bool same_mix = false;
		bool under = false;
		double built = 0.0;

		if ( packed && fields.finish ( encoded ) ) {

			int32_t mix_node = parsed->member ( block, "mixHash" );
			int32_t nonce_node = parsed->member ( block, "nonce" );
			char digits [ 40 ] = { };

			hash::keccak_256 ( encoded.data ( ), static_cast < size_t > ( encoded.size ( ) ), seal );
			parsed->text ( parsed->member ( block, "number" ), digits, sizeof ( digits ) );
			number = base::read_hex_number ( digits );
			parsed->text ( parsed->member ( block, "difficulty" ), digits, sizeof ( digits ) );
			difficulty = static_cast < double > ( base::read_hex_number ( digits ) );

			bool read = parsed->raw_size ( mix_node ) == 66 && base::read_hex ( parsed->raw ( mix_node ) + 2, 64, wanted_mix, 32 ) && parsed->raw_size ( nonce_node ) == 18 && base::read_hex ( parsed->raw ( nonce_node ) + 2, 16, nonce_bytes, 8 );
			uint32_t epoch = static_cast < uint32_t > ( number / 60000 );
			uint8_t seed [ 32 ];
			uint64_t nonce = 0;

			for ( int32_t i = 0; i < 8; i++ )
				nonce = nonce << 8 | nonce_bytes [ i ];

			hash::ethash_seed ( epoch * 2, seed );

			double began = base::seconds ( );

			if ( read && cache->build ( epoch, seed ) ) {

				built = base::seconds ( ) - began;
				cache->light ( seal, nonce, mix, result );
				same_mix = base::same ( mix, wanted_mix, 32 );

				for ( int32_t i = 0; i < 32; i++ )
					flipped [ i ] = result [ 31 - i ];

				chain::target_for ( difficulty, target, chain::ALGORITHM_RANDOMX );
				under = chain::meets ( flipped, target );

			}

		}

		note.add ( L"a live Ethereum Classic block from etc.etcdesktop.com has the mix digest Etchash computes from its header, and the result is under its difficulty" );

		if ( !packed )
			note.add ( L" (the node answered " ).number ( static_cast < uint64_t > ( reply->status ) ).add ( L" with " ).number ( static_cast < uint64_t > ( reply->body.size ( ) ) ).add ( L" bytes)" );
		else if ( !same_mix )
			note.add ( L" (block " ).number ( number ).add ( L": the mix digest didn't match)" );
		else if ( !under )
			note.add ( L" (block " ).number ( number ).add ( L": the result missed the difficulty)" );
		else
			note.add ( L", block " ).number ( number ).add ( L", epoch " ).number ( number / 60000 ).add ( L", cache built in " ).decimal ( built, 1 ).add ( L" s" );

		check ( same_mix && under, note.get ( ) );
		delete cache;
		delete parsed;
		delete reply;

	}

	static int32_t lines_starting ( const ui::snapshot& view, const wchar_t* prefix ) {

		int32_t found = 0;

		for ( int32_t i = 0; i < view.log_count; i++ )
			found += base::starts_with ( view.logs [ i ].text, prefix ) ? 1 : 0;

		return found;

	}

	static void live_pool ( uint8_t algorithm, const wchar_t* address, const wchar_t* worker, const wchar_t* what, double limit, bool gpu ) {

		app::session* run = new app::session;
		ui::preferences settings;
		double began = base::seconds ( );
		bool jobs = false;

		settings.algorithm = algorithm;
		settings.mode = ui::MODE_POOL;
		settings.threads = 2;
		settings.gpu = gpu;
		settings.dataset_megabytes = 0;
		base::copy_text ( settings.pool ( ).address, base::count_of ( settings.pool ( ).address ), address );
		base::copy_text ( settings.pool ( ).backup, base::count_of ( settings.pool ( ).backup ), L"" );
		base::copy_text ( settings.pool ( ).worker, base::count_of ( settings.pool ( ).worker ), worker );
		base::copy_text ( settings.pool ( ).password, base::count_of ( settings.pool ( ).password ), L"x" );
		run->start ( settings );

		while ( base::seconds ( ) - began < limit ) {

			run->tick ( base::seconds ( ), settings );

			const ui::snapshot& view = run->view ( );

			jobs = view.link == ui::PHASE_READY && lines_starting ( view, L"New job" ) + lines_starting ( view, L"New block template" ) >= 1;

			if ( jobs && view.accepted )
				break;

			base::sleep ( 100 );

		}

		const ui::snapshot& view = run->view ( );
		base::text note;

		note.add ( what ).add ( L": signed in and working on real jobs, " ).number ( view.accepted ).add ( L" accepted, " ).number ( view.rejected ).add ( L" rejected at difficulty " ).compact ( view.difficulty ).add ( L" and " ).scaled ( view.rate, L"H/s" );

		if ( !jobs ) {

			note.add ( L" (link " ).number ( view.link );

			for ( int32_t i = base::max ( 0, view.log_count - 3 ); i < view.log_count; i++ )
				note.add ( L"; " ).add ( view.logs [ i ].text );

			note.put ( L')' );

		}

		run->stop ( );
		check ( jobs && view.rejected == 0, note.get ( ) );
		delete run;

	}

	void etc_live_tests ( ) {

		section ( L"etc" );
		etc_live_block ( );

	}

	void online_tests ( ) {

		section ( L"online" );
		etc_live_block ( );
		kaspa_live_block ( );
		live_pool ( chain::ALGORITHM_KHEAVYHASH, L"kas.2miners.com:2020", L"kaspa:precqv0krj3r6uyyfa36ga7s0u9jct0v4wg8ctsfde2gkrsgwgw8jgxfzfc98.alloy", L"2Miners' Kaspa pool", 30.0, true );
		live_pool ( chain::ALGORITHM_KHEAVYHASH, L"kaspa.herominers.com:1206", L"kaspa:precqv0krj3r6uyyfa36ga7s0u9jct0v4wg8ctsfde2gkrsgwgw8jgxfzfc98.alloy", L"HeroMiners' Kaspa pool", 120.0, true );
		live_pool ( chain::ALGORITHM_SHA256D, L"stratum.ckpool.org:3333", L"1A1zP1eP5QGefi2DMPTfTL5SLmv7DivfNa.alloy", L"CKPool solo over Stratum", 45.0, false );
		live_pool ( chain::ALGORITHM_RANDOMX, L"http://xmr-node.cakewallet.com:18081", L"44AFFq5kSiGBoZ4NMDwYtN18obc8AemS33DBLWs3H7otXft3XjrpDtQGv7SqSsaBYBb98uNbr2VBBEt7f2wfn3RVGQBEP3A", L"solo mining against a public monerod", 30.0, false );
		live_pool ( chain::ALGORITHM_RANDOMX, L"stratum+ssl://pool.supportxmr.com:443", L"44AFFq5kSiGBoZ4NMDwYtN18obc8AemS33DBLWs3H7otXft3XjrpDtQGv7SqSsaBYBb98uNbr2VBBEt7f2wfn3RVGQBEP3A.alloy", L"SupportXMR over TLS", 45.0, false );
		live_pool ( chain::ALGORITHM_SHA256D, L"stratum2+tcp://75.119.150.111:3333/9auqWEzQDVyd2oe1JVGFLMLHZtCo2FFqZwtKA5gd9xbuEu7PH72", L"alloy-test", L"the Stratum V2 reference project's hosted testnet4 pool, over Noise with its signed certificate", 150.0, true );

	}

}

#endif
