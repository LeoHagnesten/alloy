#pragma once

#include "base/list.hh"

namespace base {

	uint32_t crc32 ( const uint8_t* data, size_t size, uint32_t running = 0 );

	bool deflate ( const uint8_t* data, size_t size, list < uint8_t >& out );

	bool encode_png ( const uint8_t* rgba, int32_t width, int32_t height, list < uint8_t >& out );

}
