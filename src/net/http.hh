#pragma once

#include "base/containers/list.hh"

namespace net {

	struct http_target {

		char host [ 256 ] = { };
		uint16_t port = 80;
		bool secure = false;
		char path [ 1024 ] = { };

	};

	struct http_reply {

		int32_t status = 0;
		base::list < char > body;

	};

	bool parse_url ( const char* url, http_target& out );

	bool http_request ( const char* method, const char* url, const char* headers, const char* body, int32_t body_size, http_reply& out, uint32_t timeout = 8000 );

	bool http_get ( const char* url, http_reply& out, uint32_t timeout = 8000 );

}
