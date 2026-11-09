#pragma once

#include "base/base.hh"

namespace chain {

	bool read_varint ( const uint8_t* data, int32_t size, int32_t& at, uint64_t& out );

	void write_varint ( uint8_t* out, int32_t& at, uint64_t value );

	bool coinbase_value ( const uint8_t* tx, int32_t size, uint64_t& total );

	double share_hashes ( uint8_t algorithm, double difficulty );

	double block_hashes ( uint8_t algorithm, double difficulty );


}
