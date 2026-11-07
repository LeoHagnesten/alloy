#include "base/system.hh"
#include "base/strings.hh"
#include <windows.h>

namespace base {

	static void cpuid ( uint32_t leaf, uint32_t sub, uint32_t* out ) {

		__asm__ volatile ( "cpuid" : "=a" ( out [ 0 ] ), "=b" ( out [ 1 ] ), "=c" ( out [ 2 ] ), "=d" ( out [ 3 ] ) : "a" ( leaf ), "c" ( sub ) );

	}

	static uint32_t count_cores ( ) {

		DWORD size = 0;

		GetLogicalProcessorInformationEx ( RelationProcessorCore, nullptr, &size );

		if ( !size )
			return 0;

		uint8_t* buffer = new uint8_t [ size ];
		uint32_t cores = 0;

		if ( GetLogicalProcessorInformationEx ( RelationProcessorCore, reinterpret_cast < SYSTEM_LOGICAL_PROCESSOR_INFORMATION_EX* > ( buffer ), &size ) ) {

			for ( DWORD at = 0; at < size; ) {

				SYSTEM_LOGICAL_PROCESSOR_INFORMATION_EX* item = reinterpret_cast < SYSTEM_LOGICAL_PROCESSOR_INFORMATION_EX* > ( buffer + at );

				if ( item->Relationship == RelationProcessorCore )
					cores++;

				at += item->Size;

			}

		}

		delete [ ] buffer;

		return cores;

	}

	static void shorten ( const wchar_t* name, wchar_t* out, int32_t room ) {

		static const wchar_t* const noise [ ] = { L"(R)", L"(TM)", L"(tm)", L" CPU", L" Processor", L" processor" };
		int32_t used = 0;

		for ( int32_t i = 0; name [ i ] && used < room - 1; ) {

			bool skipped = false;

			if ( name [ i ] == L' ' && name [ i + 1 ] == L'@' )
				break;

			for ( const wchar_t* word : noise ) {

				if ( starts_with ( name + i, word ) ) {

					i += length ( word );
					skipped = true;

					break;

				}

			}

			if ( !skipped && name [ i ] == L' ' && name [ i + 1 ] >= L'0' && name [ i + 1 ] <= L'9' ) {

				int32_t end = i + 1;

				while ( name [ end ] >= L'0' && name [ end ] <= L'9' )
					end++;

				if ( starts_with ( name + end, L"-Core" ) ) {

					i = end + 5;
					skipped = true;

				}

			}

			if ( skipped )
				continue;

			if ( name [ i ] == L' ' && ( !used || out [ used - 1 ] == L' ' ) ) {

				i++;

				continue;

			}

			out [ used++ ] = name [ i++ ];

		}

		while ( used && out [ used - 1 ] == L' ' )
			used--;

		out [ used ] = 0;

	}

	void detect ( processor& out ) {

		uint32_t regs [ 4 ];
		char brand [ 49 ] = { };

		cpuid ( 0, 0, regs );

		uint32_t highest = regs [ 0 ];

		if ( highest >= 1 ) {

			cpuid ( 1, 0, regs );
			out.ssse3 = regs [ 2 ] >> 9 & 1;
			out.sse41 = regs [ 2 ] >> 19 & 1;

		}

		if ( highest >= 7 ) {

			cpuid ( 7, 0, regs );
			out.avx2 = regs [ 1 ] >> 5 & 1;
			out.sha = regs [ 1 ] >> 29 & 1;

		}

		cpuid ( 0x80000000, 0, regs );

		if ( regs [ 0 ] >= 0x80000004 ) {

			for ( uint32_t i = 0; i < 3; i++ ) {

				cpuid ( 0x80000002 + i, 0, regs );

				for ( uint32_t k = 0; k < 16; k++ )
					brand [ i * 16 + k ] = static_cast < char > ( regs [ k / 4 ] >> ( k % 4 * 8 ) );

			}

		}

		int32_t start = 0;
		int32_t end = length ( brand );

		while ( brand [ start ] == ' ' )
			start++;

		while ( end > start && brand [ end - 1 ] == ' ' )
			end--;

		widen ( out.name, count_of ( out.name ), brand + start, end - start );

		if ( !out.name [ 0 ] )
			copy_text ( out.name, count_of ( out.name ), L"Unknown processor" );

		shorten ( out.name, out.label, count_of ( out.label ) );

		out.threads = max ( static_cast < uint32_t > ( GetActiveProcessorCount ( ALL_PROCESSOR_GROUPS ) ), 1u );
		out.cores = clamp ( count_cores ( ), 1u, out.threads );

	}

	bool system_prefers_light ( ) {

		DWORD value = 0;
		DWORD size = sizeof ( value );

		if ( RegGetValueW ( HKEY_CURRENT_USER, L"Software\\Microsoft\\Windows\\CurrentVersion\\Themes\\Personalize", L"AppsUseLightTheme", RRF_RT_REG_DWORD, nullptr, &value, &size ) != ERROR_SUCCESS )
			return false;

		return value != 0;

	}

}
