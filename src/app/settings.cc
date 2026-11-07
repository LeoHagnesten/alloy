#include "app/settings.hh"
#include "base/files.hh"
#include "base/strings.hh"
#include "base/text.hh"

namespace app {

	static bool frozen = false;

	static const char* const mode_names [ ] = { "pool", "benchmark", "genesis" };
	static const char* const theme_names [ ] = { "system", "dark", "light" };
	static const char* const priority_names [ ] = { "idle", "low", "normal" };
	static const char* const kernel_names [ ] = { "auto", "extensions", "portable" };

	static bool settings_path ( wchar_t* out, int32_t room ) {

		wchar_t folder [ base::path_room ];

		return base::data_folder ( folder, base::path_room ) && base::join_path ( out, room, folder, L"settings.ini" );

	}

	template < int32_t count >
	static uint8_t pick ( const char* const ( &names ) [ count ], const char* value, uint8_t fallback ) {

		for ( int32_t i = 0; i < count; i++ ) {

			if ( base::same_text ( names [ i ], value ) )
				return static_cast < uint8_t > ( i );

		}

		return fallback;

	}

	static bool yes ( const char* value ) {

		return base::same_text ( value, "yes" ) || base::same_text ( value, "true" ) || base::same_text ( value, "1" );

	}

	void freeze_settings ( ) {

		frozen = true;

	}

	void load_settings ( ui::preferences& out, uint32_t threads, uint32_t suggested ) {

		wchar_t path [ base::path_room ];
		base::list < uint8_t > bytes;

		out = ui::preferences { };
		out.threads = base::clamp ( suggested, 1u, threads );

		if ( frozen || !settings_path ( path, base::path_room ) || !base::read_file ( path, bytes ) || !bytes.push ( 0 ) )
			return;

		char* text = reinterpret_cast < char* > ( bytes.data ( ) );

		for ( int32_t at = 0; text [ at ]; ) {

			int32_t end = at;

			while ( text [ end ] && text [ end ] != '\n' )
				end++;

			int32_t stop = end;

			if ( stop > at && text [ stop - 1 ] == '\r' )
				stop--;

			int32_t equals = at;

			while ( equals < stop && text [ equals ] != '=' )
				equals++;

			if ( equals < stop ) {

				char key [ 32 ];
				char value [ 512 ];

				base::copy_span ( key, sizeof ( key ), text + at, equals - at );
				base::copy_span ( value, sizeof ( value ), text + equals + 1, stop - equals - 1 );

				if ( base::same_text ( key, "mode" ) )
					out.mode = pick ( mode_names, value, out.mode );
				else if ( base::same_text ( key, "address" ) )
					base::widen ( out.address, base::count_of ( out.address ), value );
				else if ( base::same_text ( key, "worker" ) )
					base::widen ( out.worker, base::count_of ( out.worker ), value );
				else if ( base::same_text ( key, "password" ) )
					base::widen ( out.password, base::count_of ( out.password ), value );
				else if ( base::same_text ( key, "cpu" ) )
					out.cpu = yes ( value );
				else if ( base::same_text ( key, "threads" ) )
					out.threads = base::clamp ( static_cast < uint32_t > ( base::read_number ( value ) ), 1u, threads );
				else if ( base::same_text ( key, "cpu_limit" ) )
					out.cpu_limit = base::clamp ( static_cast < uint32_t > ( base::read_number ( value ) ), 10u, 100u );
				else if ( base::same_text ( key, "priority" ) )
					out.priority = pick ( priority_names, value, out.priority );
				else if ( base::same_text ( key, "kernel" ) )
					out.kernel = pick ( kernel_names, value, out.kernel );
				else if ( base::same_text ( key, "gpu" ) )
					out.gpu = yes ( value );
				else if ( base::same_text ( key, "adapter" ) )
					out.adapter = static_cast < uint32_t > ( base::read_number ( value ) );
				else if ( base::same_text ( key, "gpu_limit" ) )
					out.gpu_limit = base::clamp ( static_cast < uint32_t > ( base::read_number ( value ) ), 10u, 100u );
				else if ( base::same_text ( key, "intensity" ) )
					out.intensity = base::clamp ( static_cast < uint32_t > ( base::read_number ( value ) ), 16u, 26u );
				else if ( base::same_text ( key, "pause_when_active" ) )
					out.pause_active = yes ( value );
				else if ( base::same_text ( key, "idle_minutes" ) )
					out.idle_minutes = base::clamp ( static_cast < uint32_t > ( base::read_number ( value ) ), 1u, 60u );
				else if ( base::same_text ( key, "pause_on_battery" ) )
					out.pause_battery = yes ( value );
				else if ( base::same_text ( key, "start_on_launch" ) )
					out.autostart = yes ( value );
				else if ( base::same_text ( key, "theme" ) )
					out.theme = pick ( theme_names, value, out.theme );

			}

			at = text [ end ] ? end + 1 : end;

		}

	}

	bool save_settings ( const ui::preferences& value ) {

		wchar_t path [ base::path_room ];

		if ( frozen || !settings_path ( path, base::path_room ) )
			return false;

		base::line out;
		char text [ 512 ];

		out.add ( "mode=" ).add ( mode_names [ value.mode % 3 ] ).add ( "\r\n" );
		base::narrow ( text, sizeof ( text ), value.address );
		out.add ( "address=" ).add ( text ).add ( "\r\n" );
		base::narrow ( text, sizeof ( text ), value.worker );
		out.add ( "worker=" ).add ( text ).add ( "\r\n" );
		base::narrow ( text, sizeof ( text ), value.password );
		out.add ( "password=" ).add ( text ).add ( "\r\n" );
		out.add ( "cpu=" ).add ( value.cpu ? "yes" : "no" ).add ( "\r\n" );
		out.add ( "threads=" ).number ( value.threads ).add ( "\r\n" );
		out.add ( "cpu_limit=" ).number ( value.cpu_limit ).add ( "\r\n" );
		out.add ( "priority=" ).add ( priority_names [ value.priority % 3 ] ).add ( "\r\n" );
		out.add ( "kernel=" ).add ( kernel_names [ value.kernel % 3 ] ).add ( "\r\n" );
		out.add ( "gpu=" ).add ( value.gpu ? "yes" : "no" ).add ( "\r\n" );
		out.add ( "adapter=" ).number ( value.adapter ).add ( "\r\n" );
		out.add ( "gpu_limit=" ).number ( value.gpu_limit ).add ( "\r\n" );
		out.add ( "intensity=" ).number ( value.intensity ).add ( "\r\n" );
		out.add ( "pause_when_active=" ).add ( value.pause_active ? "yes" : "no" ).add ( "\r\n" );
		out.add ( "idle_minutes=" ).number ( value.idle_minutes ).add ( "\r\n" );
		out.add ( "pause_on_battery=" ).add ( value.pause_battery ? "yes" : "no" ).add ( "\r\n" );
		out.add ( "start_on_launch=" ).add ( value.autostart ? "yes" : "no" ).add ( "\r\n" );
		out.add ( "theme=" ).add ( theme_names [ value.theme % 3 ] ).add ( "\r\n" );

		return base::write_file ( path, out.get ( ), static_cast < size_t > ( out.size ( ) ) );

	}

}
