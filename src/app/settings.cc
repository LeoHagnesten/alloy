#include "app/settings.hh"
#include "randomx/variant.hh"
#include "base/system/files.hh"
#include "base/strings.hh"
#include "base/text.hh"

namespace app {

	static bool frozen = false;

	static const char* const mode_names [ ] = { "pool", "benchmark", "genesis" };
	static const char* const algorithm_names [ ] = { "sha256d", "scrypt", "randomx" };
	static const char* const dataset_names [ ] = { "light", "full" };
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

	bool settings_frozen ( ) {

		return frozen;

	}

	static uint32_t number_in ( const char* value, uint32_t low, uint32_t high ) {

		return base::clamp ( static_cast < uint32_t > ( base::read_number ( value ) ), low, high );

	}

	void load_settings ( ui::preferences& out, uint32_t threads, uint32_t suggested ) {

		wchar_t path [ base::path_room ];
		base::list < uint8_t > bytes;

		out = ui::preferences { };

		for ( int32_t i = 0; i < chain::ALGORITHM_COUNT; i++ ) {

			out.tunings [ i ].threads = i == chain::ALGORITHM_RANDOMX ? threads : base::clamp ( suggested, 1u, threads );
			out.tunings [ i ].gpu = i != chain::ALGORITHM_RANDOMX;

		}

		out.unstash ( );

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

				char whole [ 48 ];
				char value [ 512 ];

				base::copy_span ( whole, sizeof ( whole ), text + at, equals - at );
				base::copy_span ( value, sizeof ( value ), text + equals + 1, stop - equals - 1 );

				ui::pool_profile* profile = &out.pools [ chain::ALGORITHM_SHA256D ];
				ui::algorithm_tuning* tuning = &out.tunings [ chain::ALGORITHM_SHA256D ];
				const char* key = whole;

				for ( int32_t i = 0; whole [ i ]; i++ ) {

					if ( whole [ i ] != '.' )
						continue;

					whole [ i ] = 0;

					uint8_t kind = pick ( algorithm_names, whole, 0 );

					profile = &out.pools [ kind ];
					tuning = &out.tunings [ kind ];
					key = whole + i + 1;

					break;

				}

				if ( base::same_text ( key, "mode" ) )
					out.mode = pick ( mode_names, value, out.mode );
				else if ( base::same_text ( key, "algorithm" ) )
					out.algorithm = pick ( algorithm_names, value, out.algorithm );
				else if ( base::same_text ( key, "dataset" ) )
					out.full_dataset = pick ( dataset_names, value, 1 ) == 1;
				else if ( base::same_text ( key, "randomx_variant" ) )
					out.variant = static_cast < uint8_t > ( base::max ( randomx::find_variant ( value ), 0 ) );
				else if ( base::same_text ( key, "address" ) )
					base::widen ( profile->address, base::count_of ( profile->address ), value );
				else if ( base::same_text ( key, "backup" ) )
					base::widen ( profile->backup, base::count_of ( profile->backup ), value );
				else if ( base::same_text ( key, "pin" ) )
					base::copy_text ( profile->pin, sizeof ( profile->pin ), value );
				else if ( base::same_text ( key, "backup_pin" ) )
					base::copy_text ( profile->backup_pin, sizeof ( profile->backup_pin ), value );
				else if ( base::same_text ( key, "worker" ) )
					base::widen ( profile->worker, base::count_of ( profile->worker ), value );
				else if ( base::same_text ( key, "password" ) )
					base::widen ( profile->password, base::count_of ( profile->password ), value );
				else if ( base::same_text ( key, "threads" ) )
					tuning->threads = number_in ( value, 1u, threads );
				else if ( base::same_text ( key, "cpu_limit" ) )
					tuning->cpu_limit = number_in ( value, 10u, 100u );
				else if ( base::same_text ( key, "gpu" ) )
					tuning->gpu = yes ( value );
				else if ( base::same_text ( key, "gpu_limit" ) )
					tuning->gpu_limit = number_in ( value, 10u, 100u );
				else if ( base::same_text ( key, "intensity" ) )
					tuning->intensity = number_in ( value, 16u, 26u );
				else if ( base::same_text ( key, "cpu" ) )
					out.cpu = yes ( value );
				else if ( base::same_text ( key, "priority" ) )
					out.priority = pick ( priority_names, value, out.priority );
				else if ( base::same_text ( key, "kernel" ) )
					out.kernel = pick ( kernel_names, value, out.kernel );
				else if ( base::same_text ( key, "adapter" ) )
					out.adapter = base::same_text ( value, "all" ) ? ui::all_adapters : number_in ( value, 0u, ui::adapter_room - 1u );
				else if ( base::same_text ( key, "gpu_temperature_limit" ) )
					out.gpu_heat = base::same_text ( value, "off" ) ? 0u : number_in ( value, 60u, 90u );
				else if ( base::same_text ( key, "pin_threads" ) )
					out.pin_threads = yes ( value );
				else if ( base::same_text ( key, "pause_when_active" ) )
					out.pause_active = yes ( value );
				else if ( base::same_text ( key, "idle_minutes" ) )
					out.idle_minutes = number_in ( value, 1u, 60u );
				else if ( base::same_text ( key, "pause_on_battery" ) )
					out.pause_battery = yes ( value );
				else if ( base::same_text ( key, "start_on_launch" ) )
					out.autostart = yes ( value );
				else if ( base::same_text ( key, "launch_at_login" ) )
					out.launch_at_login = yes ( value );
				else if ( base::same_text ( key, "close_to_tray" ) )
					out.close_to_tray = yes ( value );
				else if ( base::same_text ( key, "api_port" ) )
					out.api_port = number_in ( value, 0u, 65535u );
				else if ( base::same_text ( key, "theme" ) )
					out.theme = pick ( theme_names, value, out.theme );

			}

			at = text [ end ] ? end + 1 : end;

		}

		out.unstash ( );

	}

	bool save_settings ( const ui::preferences& value ) {

		wchar_t path [ base::path_room ];

		if ( frozen || !settings_path ( path, base::path_room ) )
			return false;

		ui::preferences* copy = new ui::preferences ( value );
		base::line out;
		char text [ 512 ];

		copy->stash ( );
		out.add ( "mode=" ).add ( mode_names [ copy->mode % 3 ] ).add ( "\r\n" );
		out.add ( "algorithm=" ).add ( algorithm_names [ copy->algorithm % 3 ] ).add ( "\r\n" );
		out.add ( "dataset=" ).add ( dataset_names [ copy->full_dataset ? 1 : 0 ] ).add ( "\r\n" );
		out.add ( "randomx_variant=" ).add ( randomx::variant_of ( copy->variant ).key ).add ( "\r\n" );

		for ( int32_t i = 0; i < chain::ALGORITHM_COUNT; i++ ) {

			const ui::pool_profile& profile = copy->pools [ i ];
			const ui::algorithm_tuning& tuning = copy->tunings [ i ];
			const char* name = algorithm_names [ i ];

			base::narrow ( text, sizeof ( text ), profile.address );
			out.add ( name ).add ( ".address=" ).add ( text ).add ( "\r\n" );
			base::narrow ( text, sizeof ( text ), profile.backup );
			out.add ( name ).add ( ".backup=" ).add ( text ).add ( "\r\n" );
			out.add ( name ).add ( ".pin=" ).add ( profile.pin ).add ( "\r\n" );
			out.add ( name ).add ( ".backup_pin=" ).add ( profile.backup_pin ).add ( "\r\n" );
			base::narrow ( text, sizeof ( text ), profile.worker );
			out.add ( name ).add ( ".worker=" ).add ( text ).add ( "\r\n" );
			base::narrow ( text, sizeof ( text ), profile.password );
			out.add ( name ).add ( ".password=" ).add ( text ).add ( "\r\n" );
			out.add ( name ).add ( ".threads=" ).number ( tuning.threads ).add ( "\r\n" );
			out.add ( name ).add ( ".cpu_limit=" ).number ( tuning.cpu_limit ).add ( "\r\n" );
			out.add ( name ).add ( ".gpu=" ).add ( tuning.gpu ? "yes" : "no" ).add ( "\r\n" );
			out.add ( name ).add ( ".gpu_limit=" ).number ( tuning.gpu_limit ).add ( "\r\n" );
			out.add ( name ).add ( ".intensity=" ).number ( tuning.intensity ).add ( "\r\n" );

		}

		out.add ( "cpu=" ).add ( copy->cpu ? "yes" : "no" ).add ( "\r\n" );
		out.add ( "priority=" ).add ( priority_names [ copy->priority % 3 ] ).add ( "\r\n" );
		out.add ( "kernel=" ).add ( kernel_names [ copy->kernel % 3 ] ).add ( "\r\n" );

		if ( copy->adapter == ui::all_adapters )
			out.add ( "adapter=all\r\n" );
		else
			out.add ( "adapter=" ).number ( copy->adapter ).add ( "\r\n" );

		if ( copy->gpu_heat )
			out.add ( "gpu_temperature_limit=" ).number ( copy->gpu_heat ).add ( "\r\n" );
		else
			out.add ( "gpu_temperature_limit=off\r\n" );

		out.add ( "pin_threads=" ).add ( copy->pin_threads ? "yes" : "no" ).add ( "\r\n" );
		out.add ( "pause_when_active=" ).add ( copy->pause_active ? "yes" : "no" ).add ( "\r\n" );
		out.add ( "idle_minutes=" ).number ( copy->idle_minutes ).add ( "\r\n" );
		out.add ( "pause_on_battery=" ).add ( copy->pause_battery ? "yes" : "no" ).add ( "\r\n" );
		out.add ( "start_on_launch=" ).add ( copy->autostart ? "yes" : "no" ).add ( "\r\n" );
		out.add ( "launch_at_login=" ).add ( copy->launch_at_login ? "yes" : "no" ).add ( "\r\n" );
		out.add ( "close_to_tray=" ).add ( copy->close_to_tray ? "yes" : "no" ).add ( "\r\n" );
		out.add ( "api_port=" ).number ( copy->api_port ).add ( "\r\n" );
		out.add ( "theme=" ).add ( theme_names [ copy->theme % 3 ] ).add ( "\r\n" );
		delete copy;

		return base::write_file ( path, out.get ( ), static_cast < size_t > ( out.size ( ) ) );

	}

}
