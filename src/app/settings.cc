#include "app/settings.hh"
#include "app/rigs.hh"
#include "randomx/variant.hh"
#include "base/system/files.hh"
#include "base/strings.hh"
#include "base/text.hh"

namespace app {

	static bool frozen = false;

	static const char* const mode_names [ ] = { "pool", "benchmark", "genesis" };
	static const char* const algorithm_names [ ] = { "sha256d", "scrypt", "randomx", "kheavyhash" };

	static_assert ( base::count_of ( algorithm_names ) == chain::ALGORITHM_COUNT );
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

	static const char* const day_names [ ] = { "mon", "tue", "wed", "thu", "fri", "sat", "sun" };
	static const char* const currency_names [ ] = { "usd", "eur", "gbp", "sek" };
	static const char* const style_names [ ] = { "area", "line", "bars" };
	static const char* const span_names [ ] = { "1m", "2m", "10m", "30m", "1h" };
	static const char* const update_names [ ] = { "1s", "5s", "15s", "continuous" };
	static const char* const clock_names [ ] = { "ago", "clock" };
	static const char* const smoothing_names [ ] = { "off", "5s", "30s" };

	static uint32_t day_mask ( const char* value ) {

		uint32_t mask = 0;

		for ( int32_t at = 0; value [ at ]; ) {

			while ( value [ at ] == ',' || value [ at ] == ' ' )
				at++;

			for ( uint32_t day = 0; day < 7; day++ ) {

				if ( base::same_span ( value + at, 3, day_names [ day ] ) )
					mask |= 1u << day;

			}

			while ( value [ at ] && value [ at ] != ',' )
				at++;

		}

		return mask ? mask : 0x7Fu;

	}

	template < typename writer >
	static void write_days ( writer& out, uint32_t mask ) {

		bool first = true;

		for ( uint32_t day = 0; day < 7; day++ ) {

			if ( !( mask >> day & 1u ) )
				continue;

			out.add ( first ? "" : "," ).add ( day_names [ day ] );
			first = false;

		}

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

	static void apply_text ( ui::preferences& out, char* text, uint32_t threads ) {

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

				ui::pool_profile* profile = &out.pools [ out.algorithm % chain::ALGORITHM_COUNT ];
				ui::algorithm_tuning* tuning = &out.tunings [ out.algorithm % chain::ALGORITHM_COUNT ];
				const char* key = whole;
				uint8_t which = static_cast < uint8_t > ( out.algorithm % chain::ALGORITHM_COUNT );

				for ( int32_t i = 0; whole [ i ]; i++ ) {

					if ( whole [ i ] != '.' )
						continue;

					whole [ i ] = 0;

					uint8_t kind = pick ( algorithm_names, whole, 0 );

					profile = &out.pools [ kind ];
					tuning = &out.tunings [ kind ];
					which = kind;
					key = whole + i + 1;

					break;

				}

				if ( base::same_text ( key, "mode" ) )
					out.mode = pick ( mode_names, value, out.mode );
				else if ( base::same_text ( key, "algorithm" ) )
					out.algorithm = pick ( algorithm_names, value, out.algorithm );
				else if ( base::same_text ( key, "dataset" ) )
					out.full_dataset = pick ( dataset_names, value, 1 ) == 1;
				else if ( base::same_text ( key, "randomx_variant" ) ) {

					out.variant = static_cast < uint8_t > ( base::max ( randomx::find_variant ( value ), 0 ) );
					out.coins [ chain::ALGORITHM_RANDOMX ] = chain::coin_of_variant ( out.variant );

				} else if ( base::same_text ( key, "coin" ) ) {

					uint8_t found = 0;

					if ( chain::read_coin ( value, which, found ) ) {

						out.coins [ which ] = found;

						if ( which == chain::ALGORITHM_RANDOMX )
							out.variant = chain::coin_of ( found ).variant;

					}

				}
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
				else if ( base::starts_with ( key, "card" ) && key [ 4 ] >= '0' && key [ 4 ] <= '7' && key [ 5 ] == '_' ) {

					ui::card_tuning& card = out.cards [ key [ 4 ] - '0' ];
					const char* part = key + 6;

					if ( base::same_text ( part, "custom" ) )
						card.custom = yes ( value );
					else if ( base::same_text ( part, "on" ) )
						card.on = yes ( value );
					else if ( base::same_text ( part, "limit" ) )
						card.limit = number_in ( value, 10u, 100u );
					else if ( base::same_text ( part, "intensity" ) )
						card.intensity = number_in ( value, 16u, 26u );
					else if ( base::same_text ( part, "temperature_limit" ) )
						card.heat = base::same_text ( value, "off" ) ? 0u : number_in ( value, 60u, 90u );

				} else if ( base::same_text ( key, "pin_threads" ) )
					out.pin_threads = yes ( value );
				else if ( base::same_text ( key, "pause_when_active" ) )
					out.pause_active = yes ( value );
				else if ( base::same_text ( key, "idle_minutes" ) )
					out.idle_minutes = number_in ( value, 1u, 60u );
				else if ( base::same_text ( key, "pause_on_battery" ) )
					out.pause_battery = yes ( value );
				else if ( base::same_text ( key, "pause_when_full_screen" ) )
					out.pause_fullscreen = yes ( value );
				else if ( base::same_text ( key, "pause_for_programs" ) )
					base::widen ( out.pause_apps, base::count_of ( out.pause_apps ), value );
				else if ( base::same_text ( key, "schedule" ) )
					out.schedule = yes ( value );
				else if ( base::same_text ( key, "schedule_from" ) )
					out.schedule_from = number_in ( value, 0u, 23u );
				else if ( base::same_text ( key, "schedule_to" ) )
					out.schedule_to = number_in ( value, 0u, 23u );
				else if ( base::same_text ( key, "schedule_days" ) )
					out.schedule_days = day_mask ( value );
				else if ( base::same_text ( key, "notifications" ) )
					out.notify = yes ( value );
				else if ( base::same_text ( key, "pause_shortcut" ) )
					out.hotkey = yes ( value );
				else if ( base::same_text ( key, "start_on_launch" ) )
					out.autostart = yes ( value );
				else if ( base::same_text ( key, "launch_at_login" ) )
					out.launch_at_login = yes ( value );
				else if ( base::same_text ( key, "close_to_tray" ) )
					out.close_to_tray = yes ( value );
				else if ( base::same_text ( key, "api_port" ) )
					out.api_port = number_in ( value, 0u, 65535u );
				else if ( base::same_text ( key, "rig" ) ) {

					for ( int32_t i = 0; i < ui::rig_room; i++ ) {

						if ( out.rigs [ i ].address [ 0 ] )
							continue;

						wchar_t wide [ 160 ];

						base::widen ( wide, base::count_of ( wide ), value );
						app::parse_rig ( wide, out.rigs [ i ] );

						break;

					}

				} else if ( base::same_text ( key, "online_lookups" ) )
					out.lookups = yes ( value );
				else if ( base::same_text ( key, "show_value" ) )
					out.show_fiat = yes ( value );
				else if ( base::same_text ( key, "currency" ) )
					out.currency = pick ( currency_names, value, out.currency );
				else if ( base::same_text ( key, "api_network" ) )
					out.api_lan = yes ( value );
				else if ( base::same_text ( key, "api_control" ) )
					out.api_control = yes ( value );
				else if ( base::same_text ( key, "api_token" ) )
					base::copy_text ( out.api_token, sizeof ( out.api_token ), value );
				else if ( base::same_text ( key, "theme" ) )
					out.theme = pick ( theme_names, value, out.theme );
				else if ( base::same_text ( key, "graph_style" ) )
					out.chart_style = pick ( style_names, value, out.chart_style );
				else if ( base::same_text ( key, "graph_span" ) )
					out.chart_span = pick ( span_names, value, out.chart_span );
				else if ( base::same_text ( key, "graph_update" ) )
					out.chart_update = pick ( update_names, value, out.chart_update );
				else if ( base::same_text ( key, "graph_time" ) )
					out.chart_clock = pick ( clock_names, value, out.chart_clock );
				else if ( base::same_text ( key, "graph_smoothing" ) )
					out.chart_smoothing = pick ( smoothing_names, value, out.chart_smoothing );

			}

			at = text [ end ] ? end + 1 : end;

		}

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

		apply_text ( out, reinterpret_cast < char* > ( bytes.data ( ) ), threads );
		out.unstash ( );

	}

	static bool write_settings ( const ui::preferences& value, const wchar_t* path ) {

		ui::preferences* copy = new ui::preferences ( value );
		base::writer < char, 32768 >* buffer = new base::writer < char, 32768 >;
		base::writer < char, 32768 >& out = *buffer;
		char text [ 512 ];

		copy->stash ( );
		out.add ( "mode=" ).add ( mode_names [ copy->mode % 3 ] ).add ( "\r\n" );
		out.add ( "algorithm=" ).add ( algorithm_names [ copy->algorithm % chain::ALGORITHM_COUNT ] ).add ( "\r\n" );
		out.add ( "dataset=" ).add ( dataset_names [ copy->full_dataset ? 1 : 0 ] ).add ( "\r\n" );
		out.add ( "randomx_variant=" ).add ( randomx::variant_of ( copy->variant ).key ).add ( "\r\n" );

		for ( int32_t i = 0; i < chain::ALGORITHM_COUNT; i++ ) {

			const ui::pool_profile& profile = copy->pools [ i ];
			const ui::algorithm_tuning& tuning = copy->tunings [ i ];
			const char* name = algorithm_names [ i ];

			base::narrow ( text, sizeof ( text ), profile.address );
			out.add ( name ).add ( ".coin=" ).add ( chain::coin_of ( copy->coins [ i ] ).key ).add ( "\r\n" );
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

		for ( int32_t i = 0; i < ui::adapter_room; i++ ) {

			const ui::card_tuning& card = copy->cards [ i ];

			if ( !card.custom )
				continue;

			out.add ( "card" ).number ( static_cast < uint64_t > ( i ) ).add ( "_custom=yes\r\n" );
			out.add ( "card" ).number ( static_cast < uint64_t > ( i ) ).add ( "_on=" ).add ( card.on ? "yes" : "no" ).add ( "\r\n" );
			out.add ( "card" ).number ( static_cast < uint64_t > ( i ) ).add ( "_limit=" ).number ( card.limit ).add ( "\r\n" );
			out.add ( "card" ).number ( static_cast < uint64_t > ( i ) ).add ( "_intensity=" ).number ( card.intensity ).add ( "\r\n" );
			out.add ( "card" ).number ( static_cast < uint64_t > ( i ) ).add ( "_temperature_limit=" );

			if ( card.heat )
				out.number ( card.heat ).add ( "\r\n" );
			else
				out.add ( "off\r\n" );

		}

		out.add ( "pin_threads=" ).add ( copy->pin_threads ? "yes" : "no" ).add ( "\r\n" );
		out.add ( "pause_when_active=" ).add ( copy->pause_active ? "yes" : "no" ).add ( "\r\n" );
		out.add ( "idle_minutes=" ).number ( copy->idle_minutes ).add ( "\r\n" );
		out.add ( "pause_on_battery=" ).add ( copy->pause_battery ? "yes" : "no" ).add ( "\r\n" );
		out.add ( "pause_when_full_screen=" ).add ( copy->pause_fullscreen ? "yes" : "no" ).add ( "\r\n" );
		base::narrow ( text, sizeof ( text ), copy->pause_apps );
		out.add ( "pause_for_programs=" ).add ( text ).add ( "\r\n" );
		out.add ( "schedule=" ).add ( copy->schedule ? "yes" : "no" ).add ( "\r\n" );
		out.add ( "schedule_from=" ).number ( copy->schedule_from ).add ( "\r\n" );
		out.add ( "schedule_to=" ).number ( copy->schedule_to ).add ( "\r\n" );
		out.add ( "schedule_days=" );
		write_days ( out, copy->schedule_days );
		out.add ( "\r\n" );
		out.add ( "notifications=" ).add ( copy->notify ? "yes" : "no" ).add ( "\r\n" );
		out.add ( "pause_shortcut=" ).add ( copy->hotkey ? "yes" : "no" ).add ( "\r\n" );
		out.add ( "start_on_launch=" ).add ( copy->autostart ? "yes" : "no" ).add ( "\r\n" );
		out.add ( "launch_at_login=" ).add ( copy->launch_at_login ? "yes" : "no" ).add ( "\r\n" );
		out.add ( "close_to_tray=" ).add ( copy->close_to_tray ? "yes" : "no" ).add ( "\r\n" );
		out.add ( "api_port=" ).number ( copy->api_port ).add ( "\r\n" );
		for ( int32_t i = 0; i < ui::rig_room; i++ ) {

			if ( !copy->rigs [ i ].address [ 0 ] )
				continue;

			base::narrow ( text, sizeof ( text ), copy->rigs [ i ].address );
			out.add ( "rig=" ).add ( text );

			if ( copy->rigs [ i ].token [ 0 ] )
				out.add ( "#token=" ).add ( copy->rigs [ i ].token );

			out.add ( "\r\n" );

		}

		out.add ( "online_lookups=" ).add ( copy->lookups ? "yes" : "no" ).add ( "\r\n" );
		out.add ( "show_value=" ).add ( copy->show_fiat ? "yes" : "no" ).add ( "\r\n" );
		out.add ( "currency=" ).add ( currency_names [ copy->currency % 4 ] ).add ( "\r\n" );
		out.add ( "api_network=" ).add ( copy->api_lan ? "yes" : "no" ).add ( "\r\n" );
		out.add ( "api_control=" ).add ( copy->api_control ? "yes" : "no" ).add ( "\r\n" );
		out.add ( "api_token=" ).add ( copy->api_token ).add ( "\r\n" );
		out.add ( "theme=" ).add ( theme_names [ copy->theme % 3 ] ).add ( "\r\n" );
		out.add ( "graph_style=" ).add ( style_names [ copy->chart_style % 3 ] ).add ( "\r\n" );
		out.add ( "graph_span=" ).add ( span_names [ copy->chart_span % 5 ] ).add ( "\r\n" );
		out.add ( "graph_update=" ).add ( update_names [ copy->chart_update % 4 ] ).add ( "\r\n" );
		out.add ( "graph_smoothing=" ).add ( smoothing_names [ copy->chart_smoothing % 3 ] ).add ( "\r\n" );
		out.add ( "graph_time=" ).add ( clock_names [ copy->chart_clock % 2 ] ).add ( "\r\n" );
		delete copy;

		bool good = !out.full ( ) && base::write_file ( path, out.get ( ), static_cast < size_t > ( out.size ( ) ) );

		delete buffer;

		return good;

	}

	bool save_settings ( const ui::preferences& value ) {

		wchar_t path [ base::path_room ];

		return !frozen && settings_path ( path, base::path_room ) && write_settings ( value, path );

	}

	bool settings_exist ( ) {

		wchar_t path [ base::path_room ];

		return !frozen && settings_path ( path, base::path_room ) && base::file_exists ( path );

	}

	bool apply_setting ( ui::preferences& out, const char* line, uint32_t threads ) {

		char text [ 640 ];
		bool equals = false;

		base::copy_text ( text, sizeof ( text ), line );

		for ( int32_t i = 0; text [ i ]; i++ )
			equals = equals || text [ i ] == '=';

		if ( !equals )
			return false;

		out.stash ( );
		apply_text ( out, text, threads );
		out.unstash ( );

		return true;

	}

	static bool profile_folder ( wchar_t* out, int32_t room, const wchar_t* folder, bool writing ) {

		if ( folder ) {

			base::copy_text ( out, room, folder );

			return base::make_folder ( out );

		}

		return ( !writing || !frozen ) && base::data_subfolder ( out, room, L"profiles" );

	}

	static bool profile_path ( wchar_t* out, int32_t room, const wchar_t* name, const wchar_t* folder, bool writing ) {

		wchar_t place [ base::path_room ];
		wchar_t clean [ profile_name_room + 8 ];
		int32_t used = 0;

		for ( int32_t i = 0; name [ i ] && used < profile_name_room - 1; i++ ) {

			wchar_t c = name [ i ];
			bool plain = ( c >= L'a' && c <= L'z' ) || ( c >= L'A' && c <= L'Z' ) || ( c >= L'0' && c <= L'9' ) || c == L' ' || c == L'-' || c == L'_' || c > 0x7F;

			clean [ used++ ] = plain ? c : L'_';

		}

		while ( used && clean [ used - 1 ] == L' ' )
			used--;

		if ( !used )
			return false;

		clean [ used ] = 0;
		base::copy_text ( clean + used, profile_name_room + 8 - used, L".ini" );

		return profile_folder ( place, base::path_room, folder, writing ) && base::join_path ( out, room, place, clean );

	}

	int32_t list_profiles ( wchar_t ( *names ) [ profile_name_room ], int32_t room, const wchar_t* folder ) {

		wchar_t place [ base::path_room ];
		base::file_entry entries [ profile_room ];

		if ( !profile_folder ( place, base::path_room, folder, false ) )
			return 0;

		int32_t count = base::list_files ( place, L"*.ini", entries, base::min ( room, profile_room ) );

		for ( int32_t i = 0; i < count; i++ ) {

			int32_t length = base::length ( entries [ i ].name );

			base::copy_text ( names [ i ], profile_name_room, entries [ i ].name );

			if ( length >= 4 && length - 4 < profile_name_room )
				names [ i ] [ length - 4 ] = 0;

		}

		return count;

	}

	bool save_profile ( const wchar_t* name, const ui::preferences& value, const wchar_t* folder ) {

		wchar_t path [ base::path_room ];

		return profile_path ( path, base::path_room, name, folder, true ) && write_settings ( value, path );

	}

	bool load_profile ( const wchar_t* name, ui::preferences& out, uint32_t threads, const wchar_t* folder ) {

		wchar_t path [ base::path_room ];
		base::list < uint8_t > bytes;

		if ( !profile_path ( path, base::path_room, name, folder, false ) || !base::read_file ( path, bytes ) || !bytes.push ( 0 ) )
			return false;

		out.stash ( );
		apply_text ( out, reinterpret_cast < char* > ( bytes.data ( ) ), threads );
		out.unstash ( );

		return true;

	}

	bool delete_profile ( const wchar_t* name, const wchar_t* folder ) {

		wchar_t path [ base::path_room ];

		return profile_path ( path, base::path_room, name, folder, true ) && base::delete_file ( path );

	}

}
