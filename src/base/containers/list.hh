#pragma once

#include "base/memory.hh"

namespace base {

	template < typename item >
	class list {

	public:

		list ( ) = default;

		list ( const list& ) = delete;

		list& operator = ( const list& ) = delete;

		~list ( ) {

			release ( items );

		}

		bool reserve ( int32_t wanted ) {

			if ( wanted <= capacity )
				return true;

			int32_t grown = max ( wanted, max ( capacity * 2, 16 ) );
			item* moved = static_cast < item* > ( resize ( items, static_cast < size_t > ( grown ) * sizeof ( item ) ) );

			if ( !moved )
				return false;

			items = moved;
			capacity = grown;

			return true;

		}

		bool push ( const item& value ) {

			if ( !reserve ( count + 1 ) )
				return false;

			items [ count++ ] = value;

			return true;

		}

		bool append ( const item* values, int32_t size ) {

			if ( size <= 0 )
				return true;

			if ( !reserve ( count + size ) )
				return false;

			copy ( items + count, values, static_cast < size_t > ( size ) * sizeof ( item ) );
			count += size;

			return true;

		}

		bool set_size ( int32_t size ) {

			if ( !reserve ( size ) )
				return false;

			if ( size > count )
				zero ( items + count, static_cast < size_t > ( size - count ) * sizeof ( item ) );

			count = size;

			return true;

		}

		void remove ( int32_t index ) {

			if ( index < 0 || index >= count )
				return;

			move ( items + index, items + index + 1, static_cast < size_t > ( count - index - 1 ) * sizeof ( item ) );
			count--;

		}

		void clear ( ) {

			count = 0;

		}

		int32_t size ( ) const {

			return count;

		}

		item* data ( ) {

			return items;

		}

		const item* data ( ) const {

			return items;

		}

		item& operator [ ] ( int32_t index ) {

			return items [ index ];

		}

		const item& operator [ ] ( int32_t index ) const {

			return items [ index ];

		}

	private:

		item* items = nullptr;
		int32_t count = 0;
		int32_t capacity = 0;

	};

}
