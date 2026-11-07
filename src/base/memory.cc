#include "base/memory.hh"
#include <windows.h>

namespace base {

	void* allocate ( size_t size ) {

		return HeapAlloc ( GetProcessHeap ( ), HEAP_ZERO_MEMORY, size ? size : 1 );

	}

	void* resize ( void* memory, size_t size ) {

		if ( !memory )
			return allocate ( size );

		return HeapReAlloc ( GetProcessHeap ( ), HEAP_ZERO_MEMORY, memory, size ? size : 1 );

	}

	void release ( void* memory ) {

		if ( memory )
			HeapFree ( GetProcessHeap ( ), 0, memory );

	}

	void* allocate_pages ( size_t size ) {

		return VirtualAlloc ( nullptr, size, MEM_RESERVE | MEM_COMMIT, PAGE_READWRITE );

	}

	void release_pages ( void* memory ) {

		if ( memory )
			VirtualFree ( memory, 0, MEM_RELEASE );

	}

	void fill ( void* target, uint8_t value, size_t size ) {

		__asm__ volatile ( "rep stosb" : "+D" ( target ), "+c" ( size ) : "a" ( value ) : "memory" );

	}

	void zero ( void* target, size_t size ) {

		fill ( target, 0, size );

	}

	void copy ( void* target, const void* source, size_t size ) {

		__asm__ volatile ( "rep movsb" : "+D" ( target ), "+S" ( source ), "+c" ( size ) : : "memory" );

	}

	void move ( void* target, const void* source, size_t size ) {

		uint8_t* out = static_cast < uint8_t* > ( target );
		const uint8_t* in = static_cast < const uint8_t* > ( source );

		if ( out <= in || out >= in + size ) {

			copy ( out, in, size );

			return;

		}

		volatile uint8_t* back = out + size;
		const volatile uint8_t* from = in + size;

		while ( back != out )
			*--back = *--from;

	}

	bool same ( const void* a, const void* b, size_t size ) {

		const volatile uint8_t* left = static_cast < const uint8_t* > ( a );
		const volatile uint8_t* right = static_cast < const uint8_t* > ( b );

		for ( size_t i = 0; i < size; i++ ) {

			if ( left [ i ] != right [ i ] )
				return false;

		}

		return true;

	}

}
