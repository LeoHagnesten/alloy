#include "base/memory.hh"

namespace std {

	enum class align_val_t : size_t { };

}

static void* allocate_aligned ( size_t size, std::align_val_t alignment ) {

	size_t align = static_cast < size_t > ( alignment );
	uint8_t* block = static_cast < uint8_t* > ( base::allocate ( size + align + sizeof ( void* ) ) );

	if ( !block )
		return nullptr;

	uintptr_t start = ( reinterpret_cast < uintptr_t > ( block ) + sizeof ( void* ) + align - 1 ) & ~( align - 1 );

	reinterpret_cast < void** > ( start ) [ -1 ] = block;

	return reinterpret_cast < void* > ( start );

}

static void release_aligned ( void* memory ) {

	if ( memory )
		base::release ( static_cast < void** > ( memory ) [ -1 ] );

}

extern "C" {

	int _fltused = 0;

	int _purecall ( ) {

		return 0;

	}

	void* memset ( void* target, int value, size_t size ) {

		base::fill ( target, static_cast < uint8_t > ( value ), size );

		return target;

	}

	void* memcpy ( void* target, const void* source, size_t size ) {

		base::copy ( target, source, size );

		return target;

	}

	void* memmove ( void* target, const void* source, size_t size ) {

		base::move ( target, source, size );

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

void* operator new ( size_t size, std::align_val_t alignment ) {

	return allocate_aligned ( size, alignment );

}

void* operator new [ ] ( size_t size, std::align_val_t alignment ) {

	return allocate_aligned ( size, alignment );

}

void operator delete ( void* memory, std::align_val_t ) noexcept {

	release_aligned ( memory );

}

void operator delete ( void* memory, size_t, std::align_val_t ) noexcept {

	release_aligned ( memory );

}

void operator delete [ ] ( void* memory, std::align_val_t ) noexcept {

	release_aligned ( memory );

}

void operator delete [ ] ( void* memory, size_t, std::align_val_t ) noexcept {

	release_aligned ( memory );

}
