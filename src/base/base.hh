#pragma once

#include <stddef.h>
#include <stdint.h>

namespace base {

	template < typename type >
	constexpr type min ( type a, type b ) {

		return a < b ? a : b;

	}

	template < typename type >
	constexpr type max ( type a, type b ) {

		return a > b ? a : b;

	}

	template < typename type >
	constexpr type clamp ( type value, type low, type high ) {

		return value < low ? low : ( value > high ? high : value );

	}

	template < typename type, size_t count >
	constexpr int32_t count_of ( const type ( & ) [ count ] ) {

		return static_cast < int32_t > ( count );

	}

}
