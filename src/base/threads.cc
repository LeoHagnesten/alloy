#include "base/threads.hh"
#include <windows.h>

namespace base {

	void* start_thread ( thread_entry entry, void* context ) {

		return CreateThread ( nullptr, 0, reinterpret_cast < LPTHREAD_START_ROUTINE > ( entry ), context, 0, nullptr );

	}

	void join_thread ( void* thread ) {

		if ( !thread )
			return;

		WaitForSingleObject ( thread, INFINITE );
		CloseHandle ( thread );

	}

	bool thread_finished ( void* thread ) {

		return !thread || WaitForSingleObject ( thread, 0 ) == WAIT_OBJECT_0;

	}

	void set_thread_priority ( thread_priority level ) {

		static constexpr int levels [ ] = { THREAD_PRIORITY_IDLE, THREAD_PRIORITY_BELOW_NORMAL, THREAD_PRIORITY_NORMAL, THREAD_PRIORITY_ABOVE_NORMAL };

		SetThreadPriority ( GetCurrentThread ( ), levels [ level ] );

	}

	void yield ( ) {

		SwitchToThread ( );

	}

	void lock::enter ( ) {

		AcquireSRWLockExclusive ( reinterpret_cast < SRWLOCK* > ( &state ) );

	}

	void lock::leave ( ) {

		ReleaseSRWLockExclusive ( reinterpret_cast < SRWLOCK* > ( &state ) );

	}

}
