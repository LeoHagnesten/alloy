#include "pool/job.hh"
#include "chain/merkle.hh"
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
