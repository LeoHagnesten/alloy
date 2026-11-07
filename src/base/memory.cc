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

	bool make_writable ( void* memory, size_t size ) {

		DWORD previous = 0;

		return memory && VirtualProtect ( memory, size, PAGE_READWRITE, &previous );

	}

	bool make_executable ( void* memory, size_t size ) {

		DWORD previous = 0;

		if ( !memory || !VirtualProtect ( memory, size, PAGE_EXECUTE_READ, &previous ) )
			return false;

		FlushInstructionCache ( GetCurrentProcess ( ), memory, size );

		return true;

	}

	static bool lock_privilege ( ) {

		static volatile LONG state = 0;

		if ( state )
			return state == 1;

		HANDLE token = nullptr;
		TOKEN_PRIVILEGES wanted = { };
		bool granted = false;

		if ( OpenProcessToken ( GetCurrentProcess ( ), TOKEN_ADJUST_PRIVILEGES | TOKEN_QUERY, &token ) ) {

			wanted.PrivilegeCount = 1;
			wanted.Privileges [ 0 ].Attributes = SE_PRIVILEGE_ENABLED;
			granted = LookupPrivilegeValueW ( nullptr, L"SeLockMemoryPrivilege", &wanted.Privileges [ 0 ].Luid ) && AdjustTokenPrivileges ( token, FALSE, &wanted, 0, nullptr, nullptr ) && GetLastError ( ) == ERROR_SUCCESS;
			CloseHandle ( token );

		}

		state = granted ? 1 : 2;

		return granted;

	}

	void* allocate_large ( size_t size, bool& large ) {

		size_t page = GetLargePageMinimum ( );

		large = false;

		if ( page && lock_privilege ( ) ) {

			size_t rounded = ( size + page - 1 ) / page * page;
			void* memory = VirtualAlloc ( nullptr, rounded, MEM_RESERVE | MEM_COMMIT | MEM_LARGE_PAGES, PAGE_READWRITE );

			if ( memory ) {

				large = true;

				return memory;

			}

		}

		return allocate_pages ( size );

	}

	bool open_code_view ( code_view& out, size_t size ) {

		close_code_view ( out );
		out.section = CreateFileMappingW ( INVALID_HANDLE_VALUE, nullptr, PAGE_EXECUTE_READWRITE, 0, static_cast < DWORD > ( size ), nullptr );

		if ( !out.section )
			return false;

		out.write = static_cast < uint8_t* > ( MapViewOfFile ( out.section, FILE_MAP_WRITE, 0, 0, size ) );
		out.run = static_cast < uint8_t* > ( MapViewOfFile ( out.section, FILE_MAP_READ | FILE_MAP_EXECUTE, 0, 0, size ) );

		if ( out.write && out.run )
			return true;

		close_code_view ( out );

		return false;

	}

	void close_code_view ( code_view& view ) {

		if ( view.write )
			UnmapViewOfFile ( view.write );

		if ( view.run )
			UnmapViewOfFile ( view.run );

		if ( view.section )
			CloseHandle ( view.section );

		view = code_view { };

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
