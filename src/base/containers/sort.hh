#pragma once

#include "base/base.hh"

namespace base {

	template < typename type, typename order >
	void sort ( type* items, int32_t count, order before ) {

		auto sift = [ & ] ( int32_t root, int32_t end ) {

			for ( ;; ) {

				int32_t child = root * 2 + 1;

				if ( child >= end )
					return;

				if ( child + 1 < end && before ( items [ child ], items [ child + 1 ] ) )
					child++;

				if ( !before ( items [ root ], items [ child ] ) )
					return;

				type kept = items [ root ];

				items [ root ] = items [ child ];
				items [ child ] = kept;
				root = child;

			}

		};

		for ( int32_t i = count / 2 - 1; i >= 0; i-- )
			sift ( i, count );

		for ( int32_t end = count - 1; end > 0; end-- ) {

			type kept = items [ 0 ];

			items [ 0 ] = items [ end ];
			items [ end ] = kept;
			sift ( 0, end );

		}

	}

}
