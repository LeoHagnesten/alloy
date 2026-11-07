#pragma once

#include "base/base.hh"

namespace base {

	template < typename type >
	inline void drop ( type*& item ) {

		if ( item )
			item->Release ( );

		item = nullptr;

	}

}
