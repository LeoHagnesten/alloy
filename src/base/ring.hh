#pragma once

#include "base/base.hh"

namespace base {

	template < typename item, int32_t room >
	class ring {

	public:

		item& push ( const item& value ) {

			item& slot = items [ ( first + count ) % room ];

			slot = value;

			if ( count < room )
				count++;
			else
				first = ( first + 1 ) % room;

			total++;

			return slot;

		}

		void clear ( ) {

			first = 0;
			count = 0;

		}

		int32_t size ( ) const {

			return count;

		}

		uint64_t pushed ( ) const {

			return total;

		}

		item& operator [ ] ( int32_t index ) {

			return items [ ( first + index ) % room ];

		}

		const item& operator [ ] ( int32_t index ) const {

			return items [ ( first + index ) % room ];

		}

	private:

		item items [ room ] = { };
		int32_t first = 0;
		int32_t count = 0;
		uint64_t total = 0;

	};

}
