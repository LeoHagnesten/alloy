#include "base/math.hh"

namespace base {

	float floor ( float value ) {

		if ( !( value > -8388608.0f && value < 8388608.0f ) )
			return value;

		float whole = static_cast < float > ( static_cast < int32_t > ( value ) );

		return whole > value ? whole - 1.0f : whole;

	}

	double floor ( double value ) {

		if ( !( value > -4503599627370496.0 && value < 4503599627370496.0 ) )
			return value;

		double whole = static_cast < double > ( static_cast < int64_t > ( value ) );

		return whole > value ? whole - 1.0 : whole;

	}

	float ceil ( float value ) {

		return -floor ( -value );

	}

	float round ( float value ) {

		return floor ( value + 0.5f );

	}

	float sin ( float value ) {

		float turns = floor ( value / ( 2.0f * pi ) + 0.5f );
		float x = value - turns * 2.0f * pi;

		if ( x > pi * 0.5f )
			x = pi - x;
		else if ( x < -pi * 0.5f )
			x = -pi - x;

		float square = x * x;

		return x * ( 1.0f - square / 6.0f * ( 1.0f - square / 20.0f * ( 1.0f - square / 42.0f * ( 1.0f - square / 72.0f * ( 1.0f - square / 110.0f ) ) ) ) );

	}

	float cos ( float value ) {

		return sin ( value + pi * 0.5f );

	}

	float approach ( float value, float target, float rate, float elapsed ) {

		float step = rate * elapsed;

		if ( value < target )
			return min ( value + step, target );

		return max ( value - step, target );

	}

	double nice_ceiling ( double value ) {

		if ( !( value > 0.0 ) )
			return 1.0;

		double scale = 1.0;

		while ( scale * 10.0 <= value )
			scale *= 10.0;

		while ( scale > value )
			scale /= 10.0;

		static constexpr double steps [ ] = { 1.0, 1.2, 1.5, 2.0, 2.5, 3.0, 4.0, 5.0, 6.0, 8.0, 10.0 };

		for ( double step : steps ) {

			if ( step * scale >= value )
				return step * scale;

		}

		return 10.0 * scale;

	}

}
