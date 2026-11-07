#pragma once

#include "base/base.hh"

namespace base {

	void* allocate ( size_t size );

	void* resize ( void* memory, size_t size );

	void release ( void* memory );

	void* allocate_pages ( size_t size );

	void release_pages ( void* memory );

	bool make_writable ( void* memory, size_t size );

	bool make_executable ( void* memory, size_t size );

	void* allocate_large ( size_t size, bool& large );

	struct code_view {

		uint8_t* write = nullptr;
		uint8_t* run = nullptr;
		void* section = nullptr;

	};

	bool open_code_view ( code_view& out, size_t size );

	void close_code_view ( code_view& view );

	void fill ( void* target, uint8_t value, size_t size );

	void zero ( void* target, size_t size );

	void copy ( void* target, const void* source, size_t size );

	void move ( void* target, const void* source, size_t size );

	bool same ( const void* a, const void* b, size_t size );

}
