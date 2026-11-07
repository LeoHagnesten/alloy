#pragma once

#include "base/base.hh"

namespace chain {

	static constexpr double difficulty_one = 4294901760.0;

	void target_of ( uint32_t bits, uint8_t* out );

	void target_for ( double difficulty, uint8_t* out );

	bool meets ( const uint8_t* digest, const uint8_t* target );

	bool meets_bits ( const uint8_t* digest, uint32_t bits );

	uint32_t leading_zero_bits ( const uint8_t* digest );

	uint64_t top_bits ( const uint8_t* digest );

	double difficulty_of ( uint64_t top );

	double difficulty_of_bits ( uint32_t bits );

}
