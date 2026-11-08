#pragma once

#include "chain/algorithm.hh"
#include "chain/header.hh"
#include "base/formats/json.hh"

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

	static constexpr int32_t blob_room = 256;
	static constexpr int32_t blob_nonce_at = 39;

	struct blob_job {

		char id [ 64 ] = { };
		uint8_t blob [ blob_room ] = { };
		int32_t blob_size = 0;
		uint64_t target = 0;
		uint8_t seed [ 32 ] = { };
		uint64_t height = 0;
		char algo [ 16 ] = { };
		uint8_t variant = 0;

	};

	struct kaspa_job {

		char id [ 64 ] = { };
		uint8_t pre_pow [ 32 ] = { };
		uint64_t timestamp = 0;

	};

	bool parse_kaspa_notify ( const base::json& message, int32_t params, kaspa_job& out );

	void write_kaspa_notify ( base::line& out, const kaspa_job& work, bool hex_form );

	bool kaspa_nonce_high ( const uint8_t* extranonce, int32_t size, uint64_t index, uint32_t& out );

	bool parse_notify ( const base::json& message, int32_t params, job& out );

	void write_notify ( base::line& out, const job& work );

	bool parse_blob_job ( const base::json& message, int32_t node, blob_job& out );

	void write_blob_job ( base::line& out, const blob_job& work );

	uint64_t read_blob_target ( const char* text, int32_t size );

	uint64_t blob_target_for ( double difficulty );

	double blob_difficulty ( uint64_t target );

	void counter_bytes ( uint64_t value, uint8_t* out, int32_t size );

	void assemble ( const job& work, const uint8_t* merkle_root, uint32_t time, chain::header& out );

	void build_header ( const job& work, const session_nonce& nonce, const uint8_t* second, uint32_t time, chain::header& out );

}
