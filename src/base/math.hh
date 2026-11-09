#pragma once

#include "base/base.hh"

namespace base {

	static constexpr float pi = 3.14159265358979f;

	float floor ( float value );

	double floor ( double value );

	float ceil ( float value );

	float round ( float value );

	float sin ( float value );

	float cos ( float value );

	inline float sqrt ( float value ) {

		return __builtin_sqrtf ( value );

	}

	inline float abs ( float value ) {

		return value < 0.0f ? -value : value;

	}

	inline double abs ( double value ) {

		return value < 0.0 ? -value : value;

	}

	template < typename type, int32_t count >
	inline type step_through ( const type ( &values ) [ count ], type current, int32_t step ) {

		int32_t at = 0;

		while ( at + 1 < count && values [ at ] < current )
			at++;

		if ( values [ at ] == current )
			at += step;
		else if ( step < 0 && values [ at ] > current )
			at--;

		return values [ clamp ( at, 0, count - 1 ) ];

	}

	inline float mix ( float a, float b, float t ) {

		return a + ( b - a ) * t;

	}

	inline float ease_out ( float t ) {

		float rest = 1.0f - clamp ( t, 0.0f, 1.0f );

		return 1.0f - rest * rest * rest;

	}

	inline float ease_in_out ( float t ) {

		t = clamp ( t, 0.0f, 1.0f );

		return t < 0.5f ? 4.0f * t * t * t : 1.0f - ( -2.0f * t + 2.0f ) * ( -2.0f * t + 2.0f ) * ( -2.0f * t + 2.0f ) * 0.5f;

	}

	float approach ( float value, float target, float rate, float elapsed );

	double nice_ceiling ( double value );

}
