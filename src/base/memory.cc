#include "base/base.hh"
#include <windows.h>

static void store_bytes ( void* target, uint8_t value, size_t size ) {

	__asm__ volatile ( "rep stosb" : "+D" ( target ), "+c" ( size ) : "a" ( value ) : "memory" );

}

static void move_bytes ( void* target, const void* source, size_t size ) {

	__asm__ volatile ( "rep movsb" : "+D" ( target ), "+S" ( source ), "+c" ( size ) : : "memory" );

}

extern "C" {

	int _fltused = 0;

	void* memset ( void* target, int value, size_t size ) {

		store_bytes ( target, static_cast < uint8_t > ( value ), size );

		return target;

	}

	void* memcpy ( void* target, const void* source, size_t size ) {

		move_bytes ( target, source, size );

		return target;

	}

	void* memmove ( void* target, const void* source, size_t size ) {

		uint8_t* out = static_cast < uint8_t* > ( target );
		const uint8_t* in = static_cast < const uint8_t* > ( source );

		if ( out <= in || out >= in + size ) {

			move_bytes ( out, in, size );

			return target;

		}

		volatile uint8_t* back = out + size;
		const volatile uint8_t* from = in + size;

		while ( back != out )
			*--back = *--from;

		return target;

	}

	int memcmp ( const void* a, const void* b, size_t size ) {

		const volatile uint8_t* left = static_cast < const uint8_t* > ( a );
		const volatile uint8_t* right = static_cast < const uint8_t* > ( b );

		for ( size_t i = 0; i < size; i++ ) {

			if ( left [ i ] != right [ i ] )
				return left [ i ] < right [ i ] ? -1 : 1;

		}

		return 0;

	}

}

void* operator new ( size_t size ) {

	return base::allocate ( size );

}

void* operator new [ ] ( size_t size ) {

	return base::allocate ( size );

}

void operator delete ( void* memory ) noexcept {

	base::release ( memory );

}

void operator delete ( void* memory, size_t ) noexcept {

	base::release ( memory );

}

void operator delete [ ] ( void* memory ) noexcept {

	base::release ( memory );

}

void operator delete [ ] ( void* memory, size_t ) noexcept {

	base::release ( memory );

}

namespace base {

	void* allocate ( size_t size ) {

		return HeapAlloc ( GetProcessHeap ( ), HEAP_ZERO_MEMORY, size ? size : 1 );

	}

	void release ( void* memory ) {

		if ( memory )
			HeapFree ( GetProcessHeap ( ), 0, memory );

	}

	void fill ( void* target, uint8_t value, size_t size ) {

		store_bytes ( target, value, size );

	}

	void copy ( void* target, const void* source, size_t size ) {

		move_bytes ( target, source, size );

	}

	bool same ( const void* a, const void* b, size_t size ) {

		return memcmp ( a, b, size ) == 0;

	}

	int32_t length ( const wchar_t* text ) {

		int32_t count = 0;

		while ( text && text [ count ] )
			count++;

		return count;

	}

	bool same_text ( const wchar_t* a, const wchar_t* b ) {

		int32_t i = 0;

		for ( ; a [ i ] && b [ i ]; i++ ) {

			if ( a [ i ] != b [ i ] )
				return false;

		}

		return a [ i ] == b [ i ];

	}

	bool read_hex ( const char* text, uint8_t* out, size_t count ) {

		auto digit = [ ] ( char c ) {

			if ( c >= '0' && c <= '9' )
				return c - '0';

			if ( c >= 'a' && c <= 'f' )
				return c - 'a' + 10;

			if ( c >= 'A' && c <= 'F' )
				return c - 'A' + 10;

			return -1;

		};

		for ( size_t i = 0; i < count; i++ ) {

			int high = digit ( text [ i * 2 ] );
			int low = high < 0 ? -1 : digit ( text [ i * 2 + 1 ] );

			if ( low < 0 )
				return false;

			out [ i ] = static_cast < uint8_t > ( high << 4 | low );

		}

		return text [ count * 2 ] == 0;

	}

	uint64_t read_number ( const wchar_t* text ) {

		uint64_t value = 0;

		for ( int32_t i = 0; text && text [ i ] >= L'0' && text [ i ] <= L'9'; i++ )
			value = value * 10 + static_cast < uint64_t > ( text [ i ] - L'0' );

		return value;

	}

}