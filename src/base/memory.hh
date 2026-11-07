#pragma once

#include "base/base.hh"

namespace base {

	void* allocate ( size_t size );

	void* resize ( void* memory, size_t size );

	void release ( void* memory );

	void* allocate_pages ( size_t size );

	void release_pages ( void* memory );

	void fill ( void* target, uint8_t value, size_t size );

	void zero ( void* target, size_t size );

	void copy ( void* target, const void* source, size_t size );

	void move ( void* target, const void* source, size_t size );

	bool same ( const void* a, const void* b, size_t size );

}
