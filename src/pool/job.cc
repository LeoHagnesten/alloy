#include "pool/job.hh"
#include "chain/merkle.hh"
#include "randomx/variant.hh"
#include "base/bits.hh"
#include "base/memory.hh"
#include "base/strings.hh"

namespace pool {

	static bool read_bytes ( const base::json& message, int32_t node, uint8_t* out, int32_t room, int32_t& size ) {

		int32_t length = message.raw_size ( node );

		if ( message.kind ( node ) != base::JSON_STRING || length % 2 || length / 2 > room )
			return false;

		size = length / 2;

		return base::read_hex ( message.raw ( node ), length, out, static_cast < size_t > ( size ) );

	}

	static bool read_word ( const base::json& message, int32_t node, uint32_t& out ) {

		return message.kind ( node ) == base::JSON_STRING && base::read_hex32 ( message.raw ( node ), message.raw_size ( node ), out );

	}

	bool parse_notify ( const base::json& message, int32_t params, job& out ) {

		int32_t size = 0;
		int32_t list = message.item ( params, 4 );

		if ( message.count ( params ) < 9 || message.kind ( list ) != base::JSON_ARRAY || message.count ( list ) > branch_room )
			return false;

		message.text ( message.item ( params, 0 ), out.id, sizeof ( out.id ) );

		if ( !read_bytes ( message, message.item ( params, 1 ), out.previous, 32, size ) || size != 32 )
			return false;

		if ( !read_bytes ( message, message.item ( params, 2 ), out.head, coinbase_room, out.head_size ) )
			return false;

		if ( !read_bytes ( message, message.item ( params, 3 ), out.tail, coinbase_room, out.tail_size ) )
			return false;

		out.branch_count = message.count ( list );

		for ( int32_t i = 0; i < out.branch_count; i++ ) {

			if ( !read_bytes ( message, message.item ( list, i ), out.branches + i * 32, 32, size ) || size != 32 )
				return false;

		}

		if ( !read_word ( message, message.item ( params, 5 ), out.version ) || !read_word ( message, message.item ( params, 6 ), out.bits ) || !read_word ( message, message.item ( params, 7 ), out.time ) )
			return false;

		out.clean = message.truth ( message.item ( params, 8 ) );

		return out.id [ 0 ] != 0;

	}

	void write_notify ( base::line& out, const job& work ) {

		out.add ( "{\"id\":null,\"method\":\"mining.notify\",\"params\":[" );
		base::add_json_string ( out, work.id );
		out.add ( ",\"" ).hex ( work.previous, 32, false ).add ( "\",\"" ).hex ( work.head, static_cast < size_t > ( work.head_size ), false );
		out.add ( "\",\"" ).hex ( work.tail, static_cast < size_t > ( work.tail_size ), false ).add ( "\",[" );

		for ( int32_t i = 0; i < work.branch_count; i++ )
			out.add ( i ? ",\"" : "\"" ).hex ( work.branches + i * 32, 32, false ).put ( '"' );

		out.add ( "],\"" ).hex32 ( work.version ).add ( "\",\"" ).hex32 ( work.bits ).add ( "\",\"" ).hex32 ( work.time ).add ( "\"," );
		out.add ( work.clean ? "true" : "false" ).add ( "]}" );

	}

	uint64_t read_blob_target ( const char* text, int32_t size ) {

		uint8_t bytes [ 8 ] = { };

		if ( ( size != 8 && size != 16 ) || !base::read_hex ( text, size, bytes, static_cast < size_t > ( size / 2 ) ) )
			return 0;

		if ( size == 16 )
			return base::load_little64 ( bytes );

		uint64_t narrow = base::load_little ( bytes );

		return narrow ? ~0ull / ( 0xFFFFFFFFull / narrow ) : 0;

	}

	uint64_t blob_target_for ( double difficulty ) {

		if ( !( difficulty > 1.0 ) )
			return ~0ull;

		double value = 18446744073709551615.0 / difficulty;

		return value >= 18446744073709551615.0 ? ~0ull : static_cast < uint64_t > ( value );

	}

	double blob_difficulty ( uint64_t target ) {

		return target ? 18446744073709551615.0 / static_cast < double > ( target ) : 0.0;

	}

	bool parse_blob_job ( const base::json& message, int32_t node, blob_job& out ) {

		int32_t size = 0;
		int32_t blob = message.member ( node, "blob" );
		int32_t target = message.member ( node, "target" );
		int32_t seed = message.member ( node, "seed_hash" );

		out = blob_job { };
		message.text ( message.member ( node, "job_id" ), out.id, sizeof ( out.id ) );

		if ( !read_bytes ( message, blob, out.blob, blob_room, out.blob_size ) || out.blob_size < blob_nonce_at + 4 )
			return false;

		if ( message.kind ( target ) == base::JSON_STRING )
			out.target = read_blob_target ( message.raw ( target ), message.raw_size ( target ) );

		if ( !out.target )
			return false;

		if ( !read_bytes ( message, seed, out.seed, 32, size ) || size != 32 )
			return false;

		out.height = static_cast < uint64_t > ( message.number ( message.member ( node, "height" ) ) );
		message.text ( message.member ( node, "algo" ), out.algo, sizeof ( out.algo ) );

		return out.id [ 0 ] != 0;

	}

	void write_blob_job ( base::line& out, const blob_job& work ) {

		uint8_t target [ 8 ];

		base::store_little64 ( target, work.target );
		out.add ( "{\"blob\":\"" ).hex ( work.blob, static_cast < size_t > ( work.blob_size ), false ).add ( "\",\"job_id\":" );
		base::add_json_string ( out, work.id );
		out.add ( ",\"target\":\"" ).hex ( target, 8, false ).add ( "\",\"height\":" ).number ( work.height );
		out.add ( ",\"seed_hash\":\"" ).hex ( work.seed, 32, false ).add ( "\",\"algo\":\"" ).add ( randomx::variant_of ( work.variant ).key ).add ( "\"}" );

	}

	void counter_bytes ( uint64_t value, uint8_t* out, int32_t size ) {

		for ( int32_t i = 0; i < size; i++ )
			out [ i ] = static_cast < uint8_t > ( i < 8 ? value >> ( i * 8 ) : 0 );

	}

	void assemble ( const job& work, const uint8_t* merkle_root, uint32_t time, chain::header& out ) {

		out.set_version ( work.version );
		base::copy ( out.previous ( ), work.previous, 32 );
		base::reverse_words ( out.previous ( ), 32 );
		base::copy ( out.merkle_root ( ), merkle_root, 32 );
		out.set_time ( time );
		out.set_bits ( work.bits );
		out.set_nonce ( 0 );

	}

	void build_header ( const job& work, const session_nonce& nonce, const uint8_t* second, uint32_t time, chain::header& out ) {

		uint8_t coinbase [ coinbase_room * 2 + extranonce_room * 2 ];
		int32_t size = 0;
		uint8_t leaf [ 32 ];
		uint8_t root [ 32 ];

		base::copy ( coinbase, work.head, static_cast < size_t > ( work.head_size ) );
		size += work.head_size;
		base::copy ( coinbase + size, nonce.first, static_cast < size_t > ( nonce.first_size ) );
		size += nonce.first_size;
		base::copy ( coinbase + size, second, static_cast < size_t > ( nonce.second_size ) );
		size += nonce.second_size;
		base::copy ( coinbase + size, work.tail, static_cast < size_t > ( work.tail_size ) );
		size += work.tail_size;

		hash::sha256_twice ( coinbase, static_cast < size_t > ( size ), leaf );
		chain::merkle_fold ( leaf, work.branches, work.branch_count, root );
		assemble ( work, root, time, out );

	}

}
