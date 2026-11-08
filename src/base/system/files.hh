#pragma once

#include "base/containers/list.hh"

namespace base {

	static constexpr int32_t path_room = 520;

	bool read_file ( const wchar_t* path, list < uint8_t >& out );

	struct file_entry {

		wchar_t name [ 260 ] = { };
		uint64_t size = 0;
		uint32_t written = 0;

	};

	bool write_file ( const wchar_t* path, const void* data, size_t size );

	bool append_file ( const wchar_t* path, const void* data, size_t size );

	bool delete_file ( const wchar_t* path );

	bool file_exists ( const wchar_t* path );

	int32_t list_files ( const wchar_t* folder, const wchar_t* pattern, file_entry* out, int32_t room );

	bool make_folder ( const wchar_t* path );

	bool join_path ( wchar_t* out, int32_t room, const wchar_t* folder, const wchar_t* name );

	bool data_folder ( wchar_t* out, int32_t room );

	bool roaming_path ( wchar_t* out, int32_t room, const wchar_t* name );

	bool data_subfolder ( wchar_t* out, int32_t room, const wchar_t* name );

	bool temp_folder ( wchar_t* out, int32_t room, const wchar_t* name );

}
