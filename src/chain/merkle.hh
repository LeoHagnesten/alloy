#pragma once

#include "base/base.hh"

namespace chain {

	void merkle_fold ( const uint8_t* leaf, const uint8_t* branches, int32_t count, uint8_t* out );

	void merkle_tree ( const uint8_t* leaves, int32_t count, uint8_t* out );

	void merkle_branches ( const uint8_t* leaves, int32_t count, uint8_t* out, int32_t& depth );

}
