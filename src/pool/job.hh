#pragma once

#include "chain/header.hh"
#include "base/json.hh"

namespace pool {

	static constexpr int32_t coinbase_room = 1024;
	static constexpr int32_t branch_room = 32;
	static constexpr int32_t extranonce_room = 16;

	struct job {

		char id [ 64 ] = { };
		uint8_t previous [ 32 ] = { };
		uint8_t head [ coinbase_room ] = { };
		int32_t head_size = 0;
		uint8_t tail [ coinbase_room ] = { };
		int32_t tail_size = 0;
		uint8_t branches [ branch_room * 32 ] = { };
		int32_t branch_count = 0;
		uint32_t version = 0;
		uint32_t bits = 0;
		uint32_t time = 0;
		bool clean = false;

	};

	struct session_nonce {

		uint8_t first [ extranonce_room ] = { };
		int32_t first_size = 0;
		int32_t second_size = 4;

	};

	bool parse_notify ( const base::json& message, int32_t params, job& out );

	void write_notify ( base::line& out, const job& work );

	void counter_bytes ( uint64_t value, uint8_t* out, int32_t size );

	void assemble ( const job& work, const uint8_t* merkle_root, uint32_t time, chain::header& out );

	void build_header ( const job& work, const session_nonce& nonce, const uint8_t* second, uint32_t time, chain::header& out );

}
