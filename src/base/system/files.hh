#pragma once

#include "base/containers/list.hh"

namespace base {

	static constexpr int32_t path_room = 520;

	bool read_file ( const wchar_t* path, list < uint8_t >& out );

	bool write_file ( const wchar_t* path, const void* data, size_t size );

	bool make_folder ( const wchar_t* path );

	bool join_path ( wchar_t* out, int32_t room, const wchar_t* folder, const wchar_t* name );

	bool data_folder ( wchar_t* out, int32_t room );

}
