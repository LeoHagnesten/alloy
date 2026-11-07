#pragma once

#include "base/base.hh"

namespace base {

	using thread_entry = unsigned long ( * ) ( void* context );

	enum thread_priority : uint8_t {

		PRIORITY_IDLE,
		PRIORITY_LOW,
		PRIORITY_NORMAL,
		PRIORITY_HIGH,

	};

	void* start_thread ( thread_entry entry, void* context );

	void join_thread ( void* thread );

	void set_thread_priority ( thread_priority level );

	void yield ( );

	class lock {

	public:

		void enter ( );

		void leave ( );

	private:

		void* state = nullptr;

	};

	class guard {

	public:

		explicit guard ( lock& held ) : held ( held ) {

			held.enter ( );

		}

		~guard ( ) {

			held.leave ( );

		}

		guard ( const guard& ) = delete;

		guard& operator = ( const guard& ) = delete;

	private:

		lock& held;

	};

	template < typename type >
	inline type atomic_load ( const volatile type& value ) {

		return __atomic_load_n ( &value, __ATOMIC_ACQUIRE );

	}

	template < typename type >
	inline void atomic_store ( volatile type& value, type next ) {

		__atomic_store_n ( &value, next, __ATOMIC_RELEASE );

	}

	template < typename type >
	inline type atomic_add ( volatile type& value, type amount ) {

		return __atomic_add_fetch ( &value, amount, __ATOMIC_SEQ_CST );

	}

	template < typename type >
	inline type atomic_exchange ( volatile type& value, type next ) {

		return __atomic_exchange_n ( &value, next, __ATOMIC_SEQ_CST );

	}

	template < typename type >
	inline bool atomic_replace ( volatile type& value, type expected, type next ) {

		return __atomic_compare_exchange_n ( &value, &expected, next, false, __ATOMIC_SEQ_CST, __ATOMIC_SEQ_CST );

	}

	template < typename type >
	inline void atomic_lower ( volatile type& value, type candidate ) {

		type seen = atomic_load ( value );

		while ( candidate < seen && !__atomic_compare_exchange_n ( &value, &seen, candidate, true, __ATOMIC_SEQ_CST, __ATOMIC_RELAXED ) ) { }

	}

}
