#include "base/system/threads.hh"
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

	struct processor_slot {

		WORD group;
		BYTE number;

	};

	static processor_slot order [ 1024 ];
	static volatile LONG order_count = -1;

	static void learn_order ( ) {

		DWORD size = 0;
		int32_t count = 0;

		GetLogicalProcessorInformationEx ( RelationProcessorCore, nullptr, &size );

		uint8_t* buffer = size ? new uint8_t [ size ] : nullptr;

		if ( buffer && GetLogicalProcessorInformationEx ( RelationProcessorCore, reinterpret_cast < SYSTEM_LOGICAL_PROCESSOR_INFORMATION_EX* > ( buffer ), &size ) ) {

			for ( int32_t pass = 0; pass < 2; pass++ ) {

				for ( DWORD at = 0; at < size; ) {

					SYSTEM_LOGICAL_PROCESSOR_INFORMATION_EX* item = reinterpret_cast < SYSTEM_LOGICAL_PROCESSOR_INFORMATION_EX* > ( buffer + at );
					const GROUP_AFFINITY& mask = item->Processor.GroupMask [ 0 ];
					bool first = true;

					for ( BYTE bit = 0; bit < 64 && count < 1024; bit++ ) {

						if ( !( mask.Mask >> bit & 1 ) )
							continue;

						if ( first == ( pass == 0 ) )
							order [ count++ ] = processor_slot { mask.Group, bit };

						first = false;

					}

					at += item->Size;

				}

			}

		}

		delete [ ] buffer;
		InterlockedExchange ( &order_count, count );

	}

	bool pin_thread ( uint32_t index ) {

		static lock guard_order;

		if ( order_count < 0 ) {

			guard hold ( guard_order );

			if ( order_count < 0 )
				learn_order ( );

		}

		if ( order_count <= 0 )
			return false;

		const processor_slot& slot = order [ index % static_cast < uint32_t > ( order_count ) ];
		GROUP_AFFINITY wanted = { };

		wanted.Group = slot.group;
		wanted.Mask = static_cast < KAFFINITY > ( 1 ) << slot.number;

		return SetThreadGroupAffinity ( GetCurrentThread ( ), &wanted, nullptr ) != 0;

	}

	bool pin_to_processor ( uint32_t number ) {

		WORD groups = GetActiveProcessorGroupCount ( );

		for ( WORD group = 0; group < groups; group++ ) {

			DWORD count = GetActiveProcessorCount ( group );

			if ( number >= count ) {

				number -= count;

				continue;

			}

			GROUP_AFFINITY wanted = { };

			wanted.Group = group;
			wanted.Mask = static_cast < KAFFINITY > ( 1 ) << number;

			return SetThreadGroupAffinity ( GetCurrentThread ( ), &wanted, nullptr ) != 0;

		}

		return false;

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
