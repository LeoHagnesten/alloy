#include "app/journal.hh"
#include "app/settings.hh"
#include "version.hh"
#include "base/system/clock.hh"
#include "base/system/files.hh"
#include "base/system/threads.hh"
#include "base/strings.hh"
#include "base/text.hh"
#include <windows.h>
#include <dbghelp.h>

namespace app {

	static constexpr uint32_t keep_seconds = 14 * 86400;
	static constexpr int32_t listing_room = 256;

	using dump_writer = BOOL ( WINAPI* ) ( HANDLE, DWORD, HANDLE, MINIDUMP_TYPE, PMINIDUMP_EXCEPTION_INFORMATION, PMINIDUMP_USER_STREAM_INFORMATION, PMINIDUMP_CALLBACK_INFORMATION );

	static bool opened = false;
	static bool unclean_exit = false;
	static wchar_t folder [ base::path_room ] = { };
	static wchar_t crash_report [ base::path_room ] = { };
	static base::lock writing;
	static volatile uint32_t crashing = 0;
	static base::writer < char, 16384 > crash_text;

	static const char* const level_names [ ] = { "info ", "ok   ", "warn ", "error" };

	template < typename writer >
	static void stamp ( writer& out, const base::calendar& now ) {

		out.number ( now.year ).put ( '-' ).padded ( now.month, 2 ).put ( '-' ).padded ( now.day, 2 ).put ( ' ' );
		out.padded ( now.hour, 2 ).put ( ':' ).padded ( now.minute, 2 ).put ( ':' ).padded ( now.second, 2 );

	}

	static bool path_in_folder ( wchar_t* out, int32_t room, const wchar_t* name ) {

		return folder [ 0 ] && base::join_path ( out, room, folder, name );

	}

	static void dated_name ( wchar_t* out, int32_t room, const wchar_t* prefix, const base::calendar& now, bool with_time, const wchar_t* suffix ) {

		base::text name;

		name.add ( prefix ).number ( now.year ).put ( L'-' ).padded ( now.month, 2 ).put ( L'-' ).padded ( now.day, 2 );

		if ( with_time )
			name.put ( L'-' ).padded ( now.hour, 2 ).padded ( now.minute, 2 ).padded ( now.second, 2 );

		name.add ( suffix );
		path_in_folder ( out, room, name.get ( ) );

	}

	static void prune ( ) {

		base::file_entry* entries = new base::file_entry [ listing_room ];
		uint32_t now = base::unix_time ( );
		int32_t count = base::list_files ( folder, L"*.*", entries, listing_room );

		for ( int32_t i = 0; i < count; i++ ) {

			const base::file_entry& entry = entries [ i ];
			bool ours = base::starts_with ( entry.name, L"alloy-" ) || base::starts_with ( entry.name, L"crash-" );

			if ( !ours || entry.written + keep_seconds > now )
				continue;

			wchar_t path [ base::path_room ];

			if ( path_in_folder ( path, base::path_room, entry.name ) )
				base::delete_file ( path );

		}

		delete [ ] entries;

	}

	static void write_line ( uint8_t level, const wchar_t* text );

	static const char* exception_name ( DWORD code ) {

		switch ( code ) {

			case EXCEPTION_ACCESS_VIOLATION: return "access violation";
			case EXCEPTION_ILLEGAL_INSTRUCTION: return "illegal instruction";
			case EXCEPTION_INT_DIVIDE_BY_ZERO: return "integer divide by zero";
			case EXCEPTION_STACK_OVERFLOW: return "stack overflow";
			case EXCEPTION_IN_PAGE_ERROR: return "in-page error";
			case EXCEPTION_PRIV_INSTRUCTION: return "privileged instruction";
			case EXCEPTION_DATATYPE_MISALIGNMENT: return "misaligned data";
			case EXCEPTION_BREAKPOINT: return "breakpoint";
			default: return "unexpected exception";

		}

	}

	template < typename writer >
	static void locate ( writer& out, uint64_t address ) {

		HMODULE module = nullptr;
		wchar_t path [ MAX_PATH ] = { };
		char name [ MAX_PATH ] = { };

		if ( GetModuleHandleExW ( GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS | GET_MODULE_HANDLE_EX_FLAG_UNCHANGED_REFCOUNT, reinterpret_cast < LPCWSTR > ( address ), &module ) && GetModuleFileNameW ( module, path, MAX_PATH ) ) {

			const wchar_t* file = path;

			for ( int32_t i = 0; path [ i ]; i++ ) {

				if ( path [ i ] == L'\\' || path [ i ] == L'/' )
					file = path + i + 1;

			}

			base::narrow ( name, sizeof ( name ), file );
			out.add ( name ).add ( "+0x" ).hex64 ( address - reinterpret_cast < uint64_t > ( module ) );

			return;

		}

		out.add ( "0x" ).hex64 ( address ).add ( " (generated code)" );

	}

	static LONG WINAPI on_crash ( EXCEPTION_POINTERS* info ) {

		if ( base::atomic_exchange ( crashing, 1u ) )
			return EXCEPTION_CONTINUE_SEARCH;

		base::calendar now = base::local_calendar ( );
		base::writer < char, 16384 >* report = &crash_text;
		CONTEXT context = *info->ContextRecord;
		DWORD code = info->ExceptionRecord->ExceptionCode;
		wchar_t text_path [ base::path_room ] = { };
		wchar_t dump_path [ base::path_room ] = { };

		report->clear ( );
		report->add ( "Alloy " ALLOY_VERSION_TEXT " crashed on " );
		stamp ( *report, now );
		report->add ( "\r\n\r\nException: " ).add ( exception_name ( code ) ).add ( " (0x" ).hex32 ( code ).add ( ")\r\nAt: " );
		locate ( *report, reinterpret_cast < uint64_t > ( info->ExceptionRecord->ExceptionAddress ) );

		if ( code == EXCEPTION_ACCESS_VIOLATION && info->ExceptionRecord->NumberParameters >= 2 )
			report->add ( "\r\nWhile " ).add ( info->ExceptionRecord->ExceptionInformation [ 0 ] == 1 ? "writing" : "reading" ).add ( " 0x" ).hex64 ( info->ExceptionRecord->ExceptionInformation [ 1 ] );

		report->add ( "\r\nThread: " ).number ( GetCurrentThreadId ( ) ).add ( "\r\n\r\nRegisters:\r\n" );

		const char* const names [ ] = { "rax", "rbx", "rcx", "rdx", "rsi", "rdi", "rbp", "rsp", "r8", "r9", "r10", "r11", "r12", "r13", "r14", "r15", "rip" };
		const DWORD64 values [ ] = { context.Rax, context.Rbx, context.Rcx, context.Rdx, context.Rsi, context.Rdi, context.Rbp, context.Rsp, context.R8, context.R9, context.R10, context.R11, context.R12, context.R13, context.R14, context.R15, context.Rip };

		for ( int32_t i = 0; i < 17; i++ )
			report->add ( "  " ).add ( names [ i ] ).add ( base::length ( names [ i ] ) == 2 ? "   " : "  " ).hex64 ( values [ i ] ).add ( "\r\n" );

		report->add ( "\r\nStack:\r\n" );

		for ( int32_t frame = 0; frame < 48 && context.Rip; frame++ ) {

			DWORD64 image = 0;
			PRUNTIME_FUNCTION entry = RtlLookupFunctionEntry ( context.Rip, &image, nullptr );

			report->add ( "  " ).number ( static_cast < uint64_t > ( frame ) ).add ( frame < 10 ? "   " : "  " );
			locate ( *report, context.Rip );
			report->add ( "\r\n" );

			if ( entry ) {

				void* handler_data = nullptr;
				DWORD64 establisher = 0;

				RtlVirtualUnwind ( UNW_FLAG_NHANDLER, image, context.Rip, entry, &context, &handler_data, &establisher, nullptr );

			} else {

				if ( !context.Rsp || IsBadReadPtr ( reinterpret_cast < const void* > ( context.Rsp ), 8 ) )
					break;

				context.Rip = *reinterpret_cast < const DWORD64* > ( context.Rsp );
				context.Rsp += 8;

			}

		}

		dated_name ( text_path, base::path_room, L"crash-", now, true, L".txt" );
		dated_name ( dump_path, base::path_room, L"crash-", now, true, L".dmp" );

		if ( text_path [ 0 ] )
			base::write_file ( text_path, report->get ( ), static_cast < size_t > ( report->size ( ) ) );

		HMODULE helper = LoadLibraryW ( L"dbghelp.dll" );
		dump_writer write_dump = helper ? reinterpret_cast < dump_writer > ( reinterpret_cast < void* > ( GetProcAddress ( helper, "MiniDumpWriteDump" ) ) ) : nullptr;

		if ( write_dump && dump_path [ 0 ] ) {

			HANDLE file = CreateFileW ( dump_path, GENERIC_WRITE, 0, nullptr, CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL, nullptr );

			if ( file != INVALID_HANDLE_VALUE ) {

				MINIDUMP_EXCEPTION_INFORMATION exception = { GetCurrentThreadId ( ), info, FALSE };

				write_dump ( GetCurrentProcess ( ), GetCurrentProcessId ( ), file, static_cast < MINIDUMP_TYPE > ( MiniDumpWithThreadInfo | MiniDumpWithIndirectlyReferencedMemory ), &exception, nullptr, nullptr );
				CloseHandle ( file );

			}

		}

		wchar_t marker [ base::path_room ];
		char saved [ base::path_room * 3 ];

		if ( text_path [ 0 ] && path_in_folder ( marker, base::path_room, L"crash.txt" ) ) {

			int32_t size = base::narrow ( saved, sizeof ( saved ), text_path );

			base::write_file ( marker, saved, static_cast < size_t > ( size ) );

		}

		base::writer < wchar_t, 200 > line;

		line.add ( L"Alloy crashed with an " );

		for ( const char* name = exception_name ( code ); *name; name++ )
			line.put ( static_cast < wchar_t > ( *name ) );

		line.add ( L". The report is in the logs folder." );
		write_line ( 3, line.get ( ) );

		return EXCEPTION_EXECUTE_HANDLER;

	}

	void open_journal ( const wchar_t* elsewhere ) {

		if ( opened )
			return;

		if ( elsewhere ) {

			base::copy_text ( folder, base::path_room, elsewhere );

			if ( !base::make_folder ( folder ) )
				return;

		} else if ( settings_frozen ( ) || !base::data_subfolder ( folder, base::path_room, L"logs" ) )
			return;

		wchar_t marker [ base::path_room ];
		wchar_t crashed [ base::path_room ];
		base::list < uint8_t > bytes;

		if ( path_in_folder ( marker, base::path_room, L"running.lock" ) )
			unclean_exit = base::file_exists ( marker );

		if ( path_in_folder ( crashed, base::path_room, L"crash.txt" ) && base::read_file ( crashed, bytes ) && bytes.push ( 0 ) ) {

			base::widen ( crash_report, base::path_room, reinterpret_cast < const char* > ( bytes.data ( ) ) );
			base::delete_file ( crashed );

		}

		base::write_file ( marker, "running", 7 );
		opened = true;
		prune ( );
		SetUnhandledExceptionFilter ( &on_crash );
		journal ( 0, L"Alloy " ALLOY_VERSION_TEXT L" started" );

	}

	void close_journal ( ) {

		if ( !opened )
			return;

		wchar_t marker [ base::path_room ];

		journal ( 0, L"Alloy closed" );

		if ( path_in_folder ( marker, base::path_room, L"running.lock" ) )
			base::delete_file ( marker );

		opened = false;

	}

	static void write_line ( uint8_t level, const wchar_t* text ) {

		if ( !opened )
			return;

		base::calendar now = base::local_calendar ( );
		wchar_t path [ base::path_room ];
		char body [ 768 ];
		base::writer < char, 1024 > line;

		dated_name ( path, base::path_room, L"alloy-", now, false, L".log" );
		base::narrow ( body, sizeof ( body ), text );
		stamp ( line, now );
		line.add ( "  " ).add ( level_names [ level < 4 ? level : 0 ] ).add ( "  " ).add ( body ).add ( "\r\n" );
		base::append_file ( path, line.get ( ), static_cast < size_t > ( line.size ( ) ) );

	}

	void journal ( uint8_t level, const wchar_t* text ) {

		base::guard hold ( writing );

		write_line ( level, text );

	}

	bool journal_folder ( wchar_t* out, int32_t room ) {

		if ( folder [ 0 ] ) {

			base::copy_text ( out, room, folder );

			return true;

		}

		return base::data_subfolder ( out, room, L"logs" );

	}

	bool previous_crash ( wchar_t* report, int32_t room, bool& unclean ) {

		base::copy_text ( report, room, crash_report );
		unclean = unclean_exit;

		return crash_report [ 0 ] || unclean_exit;

	}

}
