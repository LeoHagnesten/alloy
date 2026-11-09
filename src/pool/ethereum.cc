#include "pool/client.hh"
#include "hash/ethash.hh"
#include "chain/coins.hh"
#include "net/socket.hh"
#include "base/bits.hh"
#include "base/memory.hh"
#include "base/strings.hh"
#include "base/text.hh"

namespace pool {

	static bool read_hash ( const base::json& message, int32_t node, uint8_t* out ) {

		const char* text = message.raw ( node );
		int32_t size = message.raw_size ( node );

		if ( message.kind ( node ) != base::JSON_STRING )
			return false;

		if ( size == 66 && text [ 0 ] == '0' && ( text [ 1 ] == 'x' || text [ 1 ] == 'X' ) ) {

			text += 2;
			size -= 2;

		}

		return size == 64 && base::read_hex ( text, 64, out, 32 );

	}

	bool parse_ethash_notify ( const base::json& message, int32_t params, ethash_job& out ) {

		int32_t id = message.item ( params, 0 );
		int32_t clean = message.item ( params, 3 );

		out = ethash_job { };

		if ( id < 0 || message.raw_size ( id ) <= 0 || message.raw_size ( id ) >= static_cast < int32_t > ( sizeof ( out.id ) ) )
			return false;

		base::copy_span ( out.id, sizeof ( out.id ), message.raw ( id ), message.raw_size ( id ) );
		out.clean = clean < 0 || message.truth ( clean );
		message.text ( message.member ( 0, "algo" ), out.algo, sizeof ( out.algo ) );

		int32_t height = message.member ( 0, "height" );

		if ( message.kind ( height ) == base::JSON_NUMBER )
			out.height = base::read_number ( message.raw ( height ) );

		return read_hash ( message, message.item ( params, 1 ), out.seed ) && read_hash ( message, message.item ( params, 2 ), out.header );

	}

	void write_ethash_notify ( base::line& out, const ethash_job& work ) {

		out.add ( "{\"id\":null,\"method\":\"mining.notify\",\"params\":[" );
		base::add_json_string ( out, work.id );
		out.add ( ",\"" ).hex ( work.seed, 32, false ).add ( "\",\"" ).hex ( work.header, 32, false ).add ( work.clean ? "\",true]" : "\",false]" );

		if ( work.algo [ 0 ] ) {

			out.add ( ",\"algo\":" );
			base::add_json_string ( out, work.algo );

		}

		out.put ( '}' );

	}

	int32_t ethash_dataset_epoch ( const uint8_t* seed, bool etchash ) {

		int32_t found = hash::ethash_seed_epoch ( seed );

		if ( found < 0 )
			return -1;

		return etchash ? found / 2 : found;

	}

	int32_t etchash_by_height ( int32_t seed_epoch, uint64_t height ) {

		uint64_t seen = static_cast < uint64_t > ( seed_epoch );
		bool plain = seen == height / 30000;
		bool classic = seen == height / 60000 * 2;

		if ( plain == classic )
			return -1;

		return classic ? 1 : 0;

	}

	void client::take_ethash_job ( const base::json& message, int32_t params ) {

		ethash_job* incoming = new ethash_job;

		if ( !parse_ethash_notify ( message, params, *incoming ) ) {

			delete incoming;
			note ( EVENT_WARNING, L"The pool sent a job Alloy couldn't read" );

			return;

		}

		if ( !base::same ( incoming->seed, ethash_seed_seen, 32 ) || ethash_seed_epoch < 0 ) {

			ethash_seed_epoch = hash::ethash_seed_epoch ( incoming->seed );
			base::copy ( ethash_seed_seen, incoming->seed, 32 );

		}

		bool etchash = config.coin == chain::COIN_ETHEREUM_CLASSIC;

		if ( incoming->algo [ 0 ] )
			etchash = base::same_folded ( incoming->algo, "etchash" );
		else if ( incoming->height && ethash_seed_epoch >= 0 && etchash_by_height ( ethash_seed_epoch, incoming->height ) >= 0 )
			etchash = etchash_by_height ( ethash_seed_epoch, incoming->height ) == 1;

		int32_t epoch = ethash_seed_epoch < 0 ? -1 : ( etchash ? ethash_seed_epoch / 2 : ethash_seed_epoch );

		if ( epoch < 0 ) {

			delete incoming;
			note ( EVENT_WARNING, L"The pool sent a seed hash from an epoch Alloy doesn't know" );

			return;

		}

		wchar_t name [ 64 ];
		bool replaced = false;

		base::widen ( name, base::count_of ( name ), incoming->id );

		{

			base::guard hold ( state_lock );

			replaced = have_job;
			ethash_current = *incoming;
			ethash_epoch = static_cast < uint32_t > ( epoch );
			have_job = true;
			difficulty = next_difficulty;
			advance ( );

			if ( incoming->clean )
				clean_serial = serial;

			shown.jobs++;
			shown.has_job = true;
			shown.difficulty = difficulty;
			base::copy ( shown.previous, incoming->header, 32 );
			base::copy ( shown.seed, incoming->seed, 32 );
			base::copy_text ( shown.job, base::count_of ( shown.job ), name );

		}

		base::text line;

		line.add ( L"New job " ).add ( name );

		if ( incoming->clean && replaced )
			line.add ( L", earlier work dropped" );

		note ( EVENT_JOB, line.get ( ) );
		delete incoming;

	}

	bool client::take_ethash_subscription ( const base::json& message, int32_t result ) {

		int32_t node = message.item ( result, 1 );
		int32_t length = message.raw_size ( node );
		uint8_t bytes [ extranonce_room ];

		if ( message.kind ( result ) != base::JSON_ARRAY || message.kind ( node ) != base::JSON_STRING || length % 2 || length / 2 > 4 || !base::read_hex ( message.raw ( node ), length, bytes, static_cast < size_t > ( length / 2 ) ) )
			return false;

		base::guard hold ( state_lock );

		base::copy ( nonce.first, bytes, static_cast < size_t > ( length / 2 ) );
		nonce.first_size = length / 2;
		shown.state = LINK_AUTHORIZING;

		return true;

	}

	void client::write_ethash_submit ( base::line& request, uint32_t request_id, const char* job_id, uint32_t high, uint32_t low, int32_t fixed ) {

		char worker [ 256 ];
		uint8_t bytes [ 8 ];

		base::store_big ( bytes, high );
		base::store_big ( bytes + 4, low );
		base::narrow ( worker, sizeof ( worker ), config.worker );
		request.add ( "{\"id\":" ).number ( request_id ).add ( ",\"method\":\"mining.submit\",\"params\":[" );
		base::add_json_string ( request, worker );
		request.put ( ',' );
		base::add_json_string ( request, job_id );
		request.add ( ",\"" ).hex ( bytes + fixed, static_cast < size_t > ( 8 - fixed ), false ).add ( "\"]}" );

	}

}
