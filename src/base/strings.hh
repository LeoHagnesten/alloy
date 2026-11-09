#pragma once

#include "base/base.hh"

namespace base {

	int32_t length ( const wchar_t* text );

	int32_t length ( const char* text );

	bool same_text ( const wchar_t* a, const wchar_t* b );

	bool same_text ( const char* a, const char* b );

	bool same_folded ( const wchar_t* a, const wchar_t* b );

	bool same_folded ( const char* a, const char* b );

	bool same_secret ( const char* a, const char* b );

	bool same_span ( const char* span, int32_t size, const char* text );

	bool starts_with ( const wchar_t* text, const wchar_t* prefix );

	bool starts_with ( const char* text, const char* prefix );

	void copy_text ( wchar_t* target, int32_t room, const wchar_t* source );

	void copy_text ( char* target, int32_t room, const char* source );

	void copy_span ( char* target, int32_t room, const char* source, int32_t size );

	int32_t widen ( wchar_t* target, int32_t room, const char* source, int32_t size = -1 );

	int32_t narrow ( char* target, int32_t room, const wchar_t* source );

	int32_t hex_digit ( char value );

	bool read_hex ( const char* text, uint8_t* out, size_t count );

	bool read_hex ( const char* text, int32_t size, uint8_t* out, size_t count );

	void write_hex ( char* out, const uint8_t* bytes, size_t count );

	bool read_hex32 ( const char* text, int32_t size, uint32_t& out );

	uint64_t read_hex_number ( const char* text );

	int32_t write_base64 ( char* out, int32_t room, const uint8_t* bytes, size_t count );

	uint64_t read_number ( const wchar_t* text );

	uint64_t read_number ( const char* text );

	double read_decimal ( const wchar_t* text );

	double read_decimal ( const char* text, int32_t size );

}
