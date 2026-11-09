#pragma once

#include "base/containers/list.hh"

namespace chain {

	class rlp_list {

	public:

		bool bytes ( const uint8_t* data, int32_t size );

		bool hex ( const char* text, int32_t size, bool quantity );

		bool finish ( base::list < uint8_t >& out ) const;

	private:

		bool header ( uint8_t base, uint64_t size, base::list < uint8_t >& out ) const;

		base::list < uint8_t > body;

	};

}
