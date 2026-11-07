#include "chain/target.hh"
#include "base/bits.hh"
#include "base/math.hh"
#include "base/memory.hh"

namespace chain {

	void target_of ( uint32_t bits, uint8_t* out ) {

		uint32_t exponent = bits >> 24;
		uint32_t mantissa = bits & 0x007FFFFF;

		base::zero ( out, 32 );

		for ( uint32_t i = 0; i < 3; i++ ) {

			int32_t place = static_cast < int32_t > ( exponent ) - 3 + static_cast < int32_t > ( i );

			if ( place >= 0 && place < 32 )
				out [ place ] = static_cast < uint8_t > ( mantissa >> ( i * 8 ) );

		}

	}

	void target_for ( double difficulty, uint8_t* out ) {

		if ( !( difficulty > 0.0 ) || difficulty < 1.0 / 65536.0 ) {

			base::fill ( out, 0xFF, 32 );

			return;

		}

		double value = 65535.0 / difficulty;
		double scale = 1.0;

		for ( int32_t i = 0; i < 208; i++ )
			value *= 2.0;

		for ( int32_t i = 0; i < 224; i++ )
			scale *= 2.0;

		for ( int32_t limb = 7; limb >= 0; limb-- ) {

			double whole = base::floor ( value / scale );

			if ( whole > 4294967295.0 )
				whole = 4294967295.0;

			uint32_t part = static_cast < uint32_t > ( whole );

			base::store_little ( out + limb * 4, part );
			value -= static_cast < double > ( part ) * scale;
			scale /= 4294967296.0;

		}

	}

	bool meets ( const uint8_t* digest, const uint8_t* target ) {

		for ( int32_t i = 31; i >= 0; i-- ) {

			if ( digest [ i ] != target [ i ] )
				return digest [ i ] < target [ i ];

		}

		return true;

	}

	bool meets_bits ( const uint8_t* digest, uint32_t bits ) {

		uint8_t target [ 32 ];

		target_of ( bits, target );

		return meets ( digest, target );

	}

	uint32_t leading_zero_bits ( const uint8_t* digest ) {

		uint32_t count = 0;

		for ( int32_t i = 31; i >= 0; i-- ) {

			if ( !digest [ i ] ) {

				count += 8;

				continue;

			}

			count += base::leading_zeros ( digest [ i ] ) - 24;

			break;

		}

		return count;

	}

	uint64_t top_bits ( const uint8_t* digest ) {

		return base::load_little64 ( digest + 24 );

	}

	double difficulty_of ( uint64_t top ) {

		if ( !top )
			return difficulty_one * 18446744073709551616.0;

		return difficulty_one / static_cast < double > ( top );

	}

	double difficulty_of_bits ( uint32_t bits ) {

		uint32_t mantissa = bits & 0x007FFFFF;
		int32_t shift = 0x1D - static_cast < int32_t > ( bits >> 24 );

		if ( !mantissa )
			return 0.0;

		double value = 65535.0 / static_cast < double > ( mantissa );

		for ( ; shift > 0; shift-- )
			value *= 256.0;

		for ( ; shift < 0; shift++ )
			value /= 256.0;

		return value;

	}

}
