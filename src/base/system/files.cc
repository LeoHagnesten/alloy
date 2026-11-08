#include "base/system/files.hh"
#include "base/strings.hh"
#include "base/text.hh"
#include <windows.h>

namespace base {

	bool read_file ( const wchar_t* path, list < uint8_t >& out ) {

		HANDLE file = CreateFileW ( path, GENERIC_READ, FILE_SHARE_READ | FILE_SHARE_WRITE, nullptr, OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, nullptr );

		if ( file == INVALID_HANDLE_VALUE )
			return false;

		LARGE_INTEGER size = { };
		bool ok = GetFileSizeEx ( file, &size ) && size.QuadPart < 0x40000000 && out.set_size ( static_cast < int32_t > ( size.QuadPart ) );
		DWORD done = 0;

		if ( ok && size.QuadPart )
			ok = ReadFile ( file, out.data ( ), static_cast < DWORD > ( size.QuadPart ), &done, nullptr ) && done == size.QuadPart;

		CloseHandle ( file );

		return ok;

	}

	bool write_file ( const wchar_t* path, const void* data, size_t size ) {

		writer < wchar_t, path_room > temporary;

		temporary.add ( path ).add ( L".new" );

		if ( temporary.full ( ) )
			return false;

		HANDLE file = CreateFileW ( temporary.get ( ), GENERIC_WRITE, 0, nullptr, CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL, nullptr );

		if ( file == INVALID_HANDLE_VALUE )
			return false;

		DWORD done = 0;
		bool ok = !size || ( WriteFile ( file, data, static_cast < DWORD > ( size ), &done, nullptr ) && done == size );

		CloseHandle ( file );

		if ( ok )
			ok = MoveFileExW ( temporary.get ( ), path, MOVEFILE_REPLACE_EXISTING );

		if ( !ok )
			DeleteFileW ( temporary.get ( ) );

		return ok;

	}

	bool make_folder ( const wchar_t* path ) {

		return CreateDirectoryW ( path, nullptr ) || GetLastError ( ) == ERROR_ALREADY_EXISTS;

	}

	bool join_path ( wchar_t* out, int32_t room, const wchar_t* folder, const wchar_t* name ) {

		int32_t used = length ( folder );
		int32_t extra = length ( name );

		if ( used + extra + 2 > room )
			return false;

		copy_text ( out, room, folder );

		if ( used && out [ used - 1 ] != L'\\' && out [ used - 1 ] != L'/' )
			out [ used++ ] = L'\\';

		copy_text ( out + used, room - used, name );

		return true;

	}

	bool data_folder ( wchar_t* out, int32_t room ) {

		wchar_t roaming [ path_room ];
		DWORD size = GetEnvironmentVariableW ( L"APPDATA", roaming, path_room );

		if ( !size || size >= path_room )
			return false;

		return join_path ( out, room, roaming, L"Alloy" ) && make_folder ( out );

	}

}
