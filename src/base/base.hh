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

	void* allocate ( size_t size );

	void release ( void* memory );

	void fill ( void* target, uint8_t value, size_t size );

	void copy ( void* target, const void* source, size_t size );

	bool same ( const void* a, const void* b, size_t size );

	int32_t length ( const wchar_t* text );

	bool same_text ( const wchar_t* a, const wchar_t* b );

	bool read_hex ( const char* text, uint8_t* out, size_t count );

	uint64_t read_number ( const wchar_t* text );

}