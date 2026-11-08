#include "ui/shell.hh"
#include "version.hh"
#include "pool/presets.hh"
#include "base/math.hh"
#include "base/memory.hh"
#include "base/strings.hh"
#include "base/text.hh"
#include "base/system/hardware.hh"

namespace ui {

	static constexpr uint32_t key_back = 0x08;
	static constexpr uint32_t key_tab = 0x09;
	static constexpr uint32_t key_space = 0x20;
	static constexpr uint32_t key_enter = 0x0D;
	static constexpr uint32_t key_escape = 0x1B;
	static constexpr uint32_t key_end = 0x23;
	static constexpr uint32_t key_home = 0x24;
	static constexpr uint32_t key_left = 0x25;
	static constexpr uint32_t key_right = 0x27;
	static constexpr uint32_t key_delete = 0x2E;
	static constexpr uint32_t key_one = 0x31;
	static constexpr uint32_t key_a = 0x41;
	static constexpr uint32_t key_y = 0x59;
	static constexpr uint32_t key_z = 0x5A;
	static constexpr uint32_t key_f5 = 0x74;

	static constexpr float hover_time = 0.12f;
	static constexpr float page_time = 0.26f;
	static constexpr float page_rise = 10.0f;
	static constexpr float chart_time = 0.45f;
	static constexpr double blink = 0.53;
	static constexpr double tip_delay = 0.5;
	static constexpr float tip_show = 0.16f;
	static constexpr float tip_hide = 0.1f;
	static constexpr float tip_room = 280.0f;
	static constexpr float tip_margin = 8.0f;

	static const wchar_t* const page_labels [ ] = { L"Overview", L"Activity", L"Earnings", L"Rigs", L"Settings", L"Help" };
	static const icon page_icons [ ] = { ICON_OVERVIEW, ICON_ACTIVITY, ICON_EARNINGS, ICON_RIGS, ICON_SETTINGS, ICON_HELP };
	static const wchar_t* const mode_labels [ ] = { L"Pool", L"Benchmark", L"Genesis" };

	float motion::at ( double now ) const {

		float t = duration > 0.0f ? static_cast < float > ( ( now - since ) / duration ) : 1.0f;

		if ( t >= 1.0f )
			return to;

		return base::mix ( from, to, base::ease_out ( t ) );

	}

	bool motion::done ( double now ) const {

		return now - since >= duration;

	}

	void motion::go ( float target, double now ) {

		if ( target == to )
			return;

		from = at ( now );
		to = target;
		since = now;

	}

	void motion::jump ( float target ) {

		from = target;
		to = target;
		since = -100.0;

	}

	void text_field::set ( const wchar_t* value ) {

		base::copy_text ( text, room + 1, value );
		length = base::length ( text );
		caret = length;
		anchor = length;

	}

	bool text_field::selection ( int32_t& from, int32_t& to ) const {

		from = base::min ( caret, anchor );
		to = base::max ( caret, anchor );

		return from != to;

	}

	void text_field::insert ( const wchar_t* value ) {

		int32_t from = 0;
		int32_t to = 0;

		selection ( from, to );
		base::move ( text + from, text + to, sizeof ( wchar_t ) * static_cast < size_t > ( length - to + 1 ) );
		length -= to - from;

		int32_t count = 0;

		while ( value [ count ] && value [ count ] >= L' ' )
			count++;

		count = base::min ( count, room - length );
		base::move ( text + from + count, text + from, sizeof ( wchar_t ) * static_cast < size_t > ( length - from + 1 ) );
		base::copy ( text + from, value, sizeof ( wchar_t ) * static_cast < size_t > ( count ) );
		length += count;
		caret = from + count;
		anchor = caret;

	}

	void text_field::erase ( bool forward ) {

		int32_t from = 0;
		int32_t to = 0;

		if ( !selection ( from, to ) ) {

			if ( forward ? caret >= length : caret <= 0 )
				return;

			from = forward ? caret : caret - 1;
			to = from + 1;

		}

		base::move ( text + from, text + to, sizeof ( wchar_t ) * static_cast < size_t > ( length - to + 1 ) );
		length -= to - from;
		caret = from;
		anchor = from;

	}

	void text_field::move_to ( int32_t at, bool extend ) {

		caret = base::clamp ( at, 0, length );

		if ( !extend )
			anchor = caret;

	}

	static bool same_text_field ( const text_field& a, const text_field& b ) {

		return a.length == b.length && base::same_text ( a.text, b.text );

	}

	void field_memory::clear ( ) {

		undo_count = 0;
		redo_count = 0;
		last_kind = 0;
		last_edit = -100.0;

	}

	void field_memory::remember ( const text_field& current, int32_t kind, double now ) {

		bool joined = kind == last_kind && kind < 3 && now - last_edit < 1.0 && undo_count > 0;

		last_kind = kind;
		last_edit = now;
		redo_count = 0;

		if ( joined )
			return;

		if ( undo_count == depth ) {

			base::move ( undo, undo + 1, sizeof ( text_field ) * ( depth - 1 ) );
			undo_count--;

		}

		undo [ undo_count++ ] = current;

	}

	bool field_memory::step_back ( text_field& current ) {

		while ( undo_count > 0 && same_text_field ( undo [ undo_count - 1 ], current ) )
			undo_count--;

		if ( !undo_count )
			return false;

		if ( redo_count == depth ) {

			base::move ( redo, redo + 1, sizeof ( text_field ) * ( depth - 1 ) );
			redo_count--;

		}

		redo [ redo_count++ ] = current;
		current = undo [ --undo_count ];
		last_kind = 0;

		return true;

	}

	bool field_memory::step_forward ( text_field& current ) {

		if ( !redo_count )
			return false;

		if ( undo_count == depth ) {

			base::move ( undo, undo + 1, sizeof ( text_field ) * ( depth - 1 ) );
			undo_count--;

		}

		undo [ undo_count++ ] = current;
		current = redo [ --redo_count ];
		last_kind = 0;

		return true;

	}

	static bool same_preferences ( const preferences& a, const preferences& b ) {

		return base::same ( &a, &b, sizeof ( preferences ) );

	}

	void settings_memory::remember ( const preferences& current ) {

		ahead_count = 0;

		if ( past_count && same_preferences ( past [ past_count - 1 ], current ) )
			return;

		if ( past_count == depth ) {

			base::move ( past, past + 1, sizeof ( preferences ) * ( depth - 1 ) );
			past_count--;

		}

		past [ past_count++ ] = current;

	}

	bool settings_memory::step_back ( preferences& current ) {

		while ( past_count > 0 && same_preferences ( past [ past_count - 1 ], current ) )
			past_count--;

		if ( !past_count )
			return false;

		if ( ahead_count == depth ) {

			base::move ( ahead, ahead + 1, sizeof ( preferences ) * ( depth - 1 ) );
			ahead_count--;

		}

		ahead [ ahead_count++ ] = current;
		current = past [ --past_count ];

		return true;

	}

	bool settings_memory::step_forward ( preferences& current ) {

		while ( ahead_count > 0 && same_preferences ( ahead [ ahead_count - 1 ], current ) )
			ahead_count--;

		if ( !ahead_count )
			return false;

		if ( past_count == depth ) {

			base::move ( past, past + 1, sizeof ( preferences ) * ( depth - 1 ) );
			past_count--;

		}

		past [ past_count++ ] = current;
		current = ahead [ --ahead_count ];

		return true;

	}

	void shell::add_hit ( render::box area, control id, int32_t index, bool chosen ) {

		hit item = { area, id, index, chosen, false, hit_scroller, area.y, area.bottom ( ) };

		area.y += hit_offset;

		float top = base::max ( area.y, hit_top );
		float bottom = base::min ( area.bottom ( ), hit_bottom );

		if ( bottom <= top && hit_scroller < 0 )
			return;

		item.hidden = bottom <= top;
		item.area.y = item.hidden ? area.y : top;
		item.area.h = item.hidden ? area.h : bottom - top;

		if ( hit_count < base::count_of ( hits ) )
			hits [ hit_count++ ] = item;

	}

	void shell::add_hint ( render::box area, const wchar_t* text ) {

		area.y += hit_offset;

		float top = base::max ( area.y, hit_top );
		float bottom = base::min ( area.bottom ( ), hit_bottom );

		if ( bottom <= top || hint_count >= base::count_of ( hints ) || !text || !text [ 0 ] )
			return;

		hint& item = hints [ hint_count++ ];
		uint64_t key = 1469598103934665603ull;

		for ( int32_t i = 0; text [ i ]; i++ )
			key = ( key ^ text [ i ] ) * 1099511628211ull;

		item.area = render::box { area.x, top, area.w, bottom - top };
		item.key = key | 1;
		base::copy_text ( item.text, base::count_of ( item.text ), text );

	}

	void shell::update_tip ( double now ) {

		const hint* under = nullptr;

		for ( int32_t i = hint_count - 1; i >= 0 && !under; i-- ) {

			if ( hints [ i ].area.contains ( mouse_x, mouse_y ) )
				under = &hints [ i ];

		}

		uint64_t key = under ? under->key : 0;

		if ( tip_open && tip_closing < 0.0 && key != tip_key )
			tip_closing = now;

		if ( tip_open && tip_closing >= 0.0 && now - tip_closing >= tip_hide ) {

			tip_open = false;
			tip_closing = -100.0;

		}

		tip_pending = false;

		if ( key != tip_quiet )
			tip_quiet = 0;

		if ( tip_open || !key || key == tip_quiet || press_id != CONTROL_NONE || dragging >= 0 )
			return;

		if ( now - rest_since < tip_delay ) {

			tip_pending = true;

			return;

		}

		tip_open = true;
		tip_key = key;
		tip_x = mouse_x;
		tip_y = mouse_y;
		tip_since = now;
		tip_closing = -100.0;
		base::copy_text ( tip_text, base::count_of ( tip_text ), under->text );

	}

	void shell::paint_tip ( render::surface& surface, double now ) {

		if ( !tip_open )
			return;

		float text_width = base::min ( surface.measure ( tip_text, render::FONT_SMALL ), tip_room );
		float text_height = surface.paragraph_height ( tip_text, render::FONT_SMALL, text_width + 1.0f );
		float wide = base::ceil ( text_width + 1.0f ) + 24.0f;
		float tall = base::ceil ( text_height ) + 18.0f;
		float x = tip_x + 10.0f;
		float y = tip_y + 22.0f;

		if ( x + wide > width - 8.0f )
			x = base::max ( 8.0f, width - 8.0f - wide );

		if ( y + tall > height - 8.0f )
			y = tip_y - tall - 12.0f;

		if ( !surface.begin_layer ( wide + tip_margin * 2.0f, tall + tip_margin * 2.0f ) )
			return;

		render::box frame = { tip_margin, tip_margin, wide, tall };

		surface.fill ( render::box { frame.x - 1.0f, frame.y + 1.0f, frame.w + 2.0f, frame.h + 3.0f }, render::rgb ( 0x000000, 0.10f ), 7.0f );
		surface.fill ( render::box { frame.x, frame.y + 1.0f, frame.w, frame.h + 1.0f }, render::rgb ( 0x000000, 0.12f ), 6.0f );
		surface.fill ( frame, colors.tip, 6.0f );
		surface.outline ( frame, colors.control_line, 1.0f / zoom, 6.0f );
		surface.paragraph ( tip_text, render::FONT_SMALL, render::box { frame.x + 12.0f, frame.y + 9.0f, text_width + 1.0f, text_height }, colors.text );
		surface.end_layer ( );

		float growth = 1.0f;
		float opacity = 1.0f;

		if ( tip_closing >= 0.0 )
			opacity = 1.0f - base::clamp ( static_cast < float > ( ( now - tip_closing ) / tip_hide ), 0.0f, 1.0f );
		else {

			float t = static_cast < float > ( ( now - tip_since ) / tip_show );

			if ( t < 1.0f ) {

				float eased = base::ease_out ( t );

				growth = base::mix ( 0.92f, 1.0f, eased );
				opacity = eased;

			}

		}

		surface.draw_layer ( x - tip_margin, y - tip_margin, growth, tip_x, tip_y, opacity );

	}

	const shell::hit* shell::find ( float x, float y ) const {

		for ( int32_t i = hit_count - 1; i >= 0; i-- ) {

			if ( !hits [ i ].hidden && hits [ i ].area.contains ( x, y ) )
				return &hits [ i ];

		}

		return nullptr;

	}

	float shell::glow ( control id, int32_t index, double now ) const {

		if ( id == CONTROL_NONE )
			return 0.0f;

		if ( id == hover_id && index == hover_index )
			return base::ease_out ( static_cast < float > ( ( now - hover_since ) / hover_time ) );

		if ( id == fade_id && index == fade_index )
			return fade_level * ( 1.0f - base::ease_out ( static_cast < float > ( ( now - fade_since ) / hover_time ) ) );

		return 0.0f;

	}

	frame_part shell::part_at ( float x, float y ) const {

		if ( y >= title_height || x < 0.0f || x >= width )
			return PART_CLIENT;

		if ( x >= width - caption_width )
			return PART_CLOSE;

		if ( x >= width - caption_width * 2.0f )
			return PART_MAXIMIZE;

		if ( x >= width - caption_width * 3.0f )
			return PART_MINIMIZE;

		return PART_CAPTION;

	}

	void shell::hover_frame ( frame_part part, double now ) {

		if ( part == PART_CAPTION )
			part = PART_CLIENT;

		if ( part != frame_hover ) {

			frame_hover = part;
			frame_since = now;

		}

		if ( part == PART_CLIENT )
			frame_pressed = PART_CLIENT;

	}

	void shell::press_frame ( frame_part part ) {

		frame_pressed = part;

	}

	command shell::mouse_move ( float x, float y, double now ) {

		mouse_x = x;
		mouse_y = y;

		if ( base::abs ( x - rest_x ) > 3.0f || base::abs ( y - rest_y ) > 3.0f ) {

			rest_x = x;
			rest_y = y;
			rest_since = now;

		}

		if ( dragging >= 0 )
			return slide ( dragging, x ) ? COMMAND_PREFERENCES : COMMAND_NONE;

		const hit* item = find ( x, y );
		control id = item ? item->id : CONTROL_NONE;
		int32_t index = item ? item->index : 0;

		if ( id != hover_id || index != hover_index ) {

			fade_level = glow ( hover_id, hover_index, now );
			fade_id = hover_id;
			fade_index = hover_index;
			fade_since = now;
			hover_id = id;
			hover_index = index;
			hover_since = now;

		}

		return COMMAND_NONE;

	}

	void shell::mouse_leave ( double now ) {

		mouse_move ( -1000.0f, -1000.0f, now );
		hover_frame ( PART_CLIENT, now );

	}

	command shell::mouse_down ( float x, float y, bool shift, double now ) {

		const hit* item = find ( x, y );
		command result = COMMAND_NONE;

		press_id = item ? item->id : CONTROL_NONE;
		press_index = item ? item->index : 0;
		rest_since = now;
		tip_quiet = 0;
		keyboard_on = false;

		for ( int32_t i = hint_count - 1; i >= 0 && !tip_quiet; i-- ) {

			if ( hints [ i ].area.contains ( x, y ) )
				tip_quiet = hints [ i ].key;

		}

		if ( tip_open && tip_closing < 0.0 )
			tip_closing = now;

		bool keeps_focus = item && ( ( item->id == CONTROL_FIELD && item->index == focus ) || ( item->id == CONTROL_REVEAL && focus == SETTING_PASSWORD ) );

		if ( focus >= 0 && !keeps_focus )
			result = commit_field ( now );

		if ( item && item->id == CONTROL_FIELD ) {

			int32_t slot = item->index;
			int32_t at = caret_from ( slot, x );

			if ( focus != slot ) {

				focus = slot;
				field_history [ slot ].clear ( );
				fields [ slot ].move_to ( at, false );

			} else
				fields [ slot ].move_to ( at, shift );

			caret_since = now;
			dragging = -1;

		}

		if ( item && item->id == CONTROL_SLIDER ) {

			settings_history.remember ( settings );
			dragging = item->index;

			if ( slide ( dragging, x ) )
				result = result == COMMAND_NONE ? COMMAND_PREFERENCES : result;

		}

		return result;

	}

	command shell::mouse_up ( float x, float y, double now ) {

		if ( dragging >= 0 ) {

			dragging = -1;
			press_id = CONTROL_NONE;
			mouse_move ( x, y, now );

			return COMMAND_PREFERENCES;

		}

		const hit* item = find ( x, y );
		command result = COMMAND_NONE;

		if ( item && item->id == press_id && item->index == press_index )
			result = activate ( item->id, item->index, now );

		press_id = CONTROL_NONE;

		return result;

	}

	void shell::wheel ( float x, float y, float delta, double now ) {

		rest_since = now;

		if ( tip_open && tip_closing < 0.0 )
			tip_closing = now;

		for ( int32_t i = 0; i < SCROLL_COUNT; i++ ) {

			if ( !scroll_area [ i ].contains ( x, y ) || scroll_limit [ i ] <= 0.0f )
				continue;

			float goal = base::clamp ( scroll_goal [ i ] - delta / 120.0f * 64.0f, 0.0f, scroll_limit [ i ] );

			goal = base::round ( goal * zoom ) / zoom;
			scroll_goal [ i ] = goal;
			scroll_motion [ i ].duration = 0.2f;
			scroll_motion [ i ].go ( goal, now );

			return;

		}

	}

	float shell::scroll_offset ( scroller which, double now ) const {

		return scroll_motion [ which ].at ( now );

	}

	command shell::activate ( control id, int32_t index, double now ) {

		switch ( id ) {

			case CONTROL_NAV:
				open_page ( static_cast < page > ( index ), now );
				return COMMAND_NONE;

			case CONTROL_TOGGLE:
				return COMMAND_TOGGLE;

			case CONTROL_MODE:
				if ( data && data->running )
					return COMMAND_NONE;

				settings_history.remember ( settings );
				settings.mode = static_cast < uint8_t > ( index );
				return COMMAND_PREFERENCES;

			case CONTROL_ALGORITHM:
				if ( data && data->running )
					return COMMAND_NONE;

				settings_history.remember ( settings );
				settings.choose_algorithm ( static_cast < uint8_t > ( index ) );
				return COMMAND_PREFERENCES;

			case CONTROL_REVEAL:
				reveal = !reveal;
				return COMMAND_NONE;

			case CONTROL_PRESET: {

				const pool::preset& known = pool::preset_at ( index );
				pool_profile& profile = settings.pools [ known.algorithm % chain::ALGORITHM_COUNT ];
				bool restart = data && data->running && settings.mode == MODE_POOL && known.algorithm == settings.algorithm;

				settings_history.remember ( settings );
				base::copy_text ( profile.address, base::count_of ( profile.address ), known.address );
				base::copy_text ( profile.backup, base::count_of ( profile.backup ), known.backup );
				profile.pin [ 0 ] = 0;
				profile.backup_pin [ 0 ] = 0;
				sync_fields ( );

				return restart ? COMMAND_RESTART : COMMAND_PREFERENCES;

			}

			case CONTROL_TAB:
				open_tab ( index, now );
				return COMMAND_NONE;

			case CONTROL_CLEAR_LOG:
				return COMMAND_CLEAR_LOG;

			case CONTROL_ACTION:
				action_argument = index % 256;
				return static_cast < command > ( index / 256 );

			case CONTROL_NEW_TOKEN:
				settings_history.remember ( settings );
				fresh_token ( );
				return COMMAND_PREFERENCES;

			case CONTROL_CARD_RESET:
				settings_history.remember ( settings );
				settings.cards [ card_view % adapter_room ] = card_tuning { };
				return COMMAND_PREFERENCES;

			case CONTROL_SWITCH:
				settings_history.remember ( settings );

				if ( index == SETTING_CPU )
					settings.cpu = !settings.cpu;
				else if ( index == SETTING_GPU )
					settings.gpu = !settings.gpu;
				else if ( index == SETTING_PAUSE_ACTIVE )
					settings.pause_active = !settings.pause_active;
				else if ( index == SETTING_PAUSE_BATTERY )
					settings.pause_battery = !settings.pause_battery;
				else if ( index == SETTING_AUTOSTART )
					settings.autostart = !settings.autostart;
				else if ( index == SETTING_PIN )
					settings.pin_threads = !settings.pin_threads;
				else if ( index == SETTING_LOGIN )
					settings.launch_at_login = !settings.launch_at_login;
				else if ( index == SETTING_TRAY )
					settings.close_to_tray = !settings.close_to_tray;
				else if ( index == SETTING_API )
					settings.api_port = settings.api_port ? 0u : 4067u;
				else if ( index == SETTING_API_LAN || index == SETTING_API_CONTROL ) {

					bool& flag = index == SETTING_API_LAN ? settings.api_lan : settings.api_control;

					flag = !flag;

					if ( flag && !settings.api_token [ 0 ] )
						fresh_token ( );

				}
				else if ( index == SETTING_FULLSCREEN )
					settings.pause_fullscreen = !settings.pause_fullscreen;
				else if ( index == SETTING_SCHEDULE )
					settings.schedule = !settings.schedule;
				else if ( index == SETTING_NOTIFY )
					settings.notify = !settings.notify;
				else if ( index == SETTING_CARD_ON ) {

					card_tuning& card = settings.cards [ card_view % adapter_room ];

					if ( !card.custom ) {

						card.custom = true;
						card.limit = settings.gpu_limit;
						card.intensity = settings.intensity;
						card.heat = settings.gpu_heat;

					}

					card.on = !card.on;

				} else if ( index == SETTING_LOOKUPS )
					settings.lookups = !settings.lookups;
				else if ( index == SETTING_FIAT )
					settings.show_fiat = !settings.show_fiat;
				else if ( index == SETTING_HOTKEY )
					settings.hotkey = !settings.hotkey;
				return COMMAND_PREFERENCES;

			case CONTROL_STEP_DOWN:
			case CONTROL_STEP_UP: {

				int32_t step = id == CONTROL_STEP_UP ? 1 : -1;
				uint32_t threads = data ? data->threads : 1;

				settings_history.remember ( settings );

				if ( index == SETTING_THREADS )
					settings.threads = static_cast < uint32_t > ( base::clamp ( static_cast < int32_t > ( settings.threads ) + step, 1, static_cast < int32_t > ( threads ) ) );
				else if ( index == SETTING_IDLE_MINUTES )
					settings.idle_minutes = static_cast < uint32_t > ( base::clamp ( static_cast < int32_t > ( settings.idle_minutes ) + step, 1, 60 ) );
				else if ( index == SETTING_SCHEDULE_FROM )
					settings.schedule_from = static_cast < uint32_t > ( ( static_cast < int32_t > ( settings.schedule_from ) + step + 24 ) % 24 );
				else if ( index == SETTING_SCHEDULE_TO )
					settings.schedule_to = static_cast < uint32_t > ( ( static_cast < int32_t > ( settings.schedule_to ) + step + 24 ) % 24 );

				return COMMAND_PREFERENCES;

			}

			case CONTROL_CHOICE: {

				int32_t which = index / 16;
				uint8_t option = static_cast < uint8_t > ( index % 16 );
				preferences* before = new preferences ( settings );

				if ( which == SETTING_PRIORITY )
					settings.priority = option;
				else if ( which == SETTING_KERNEL )
					settings.kernel = option;
				else if ( which == SETTING_ADAPTER )
					settings.adapter = data && option >= data->adapter_count ? all_adapters : option;
				else if ( which == SETTING_THEME )
					settings.theme = option;
				else if ( which == SETTING_DATASET )
					settings.full_dataset = option == 0;
				else if ( which == SETTING_VARIANT )
					settings.variant = option;
				else if ( which == SETTING_CURRENCY )
					settings.currency = option;
				else if ( which == SETTING_CARD ) {

					card_view = option;
					delete before;

					return COMMAND_NONE;

				} else if ( which == SETTING_CHART_SPAN )
					settings.chart_span = option;
				else if ( which == SETTING_CHART_SMOOTHING )
					settings.chart_smoothing = option;
				else if ( which == SETTING_SCHEDULE_DAYS ) {

					uint32_t flipped = settings.schedule_days ^ 1u << option;

					settings.schedule_days = flipped ? flipped : settings.schedule_days;

				}

				bool changed = !base::same ( before, &settings, sizeof ( preferences ) );

				if ( changed )
					settings_history.remember ( *before );

				delete before;

				return changed ? COMMAND_PREFERENCES : COMMAND_NONE;

			}

			default:
				return COMMAND_NONE;

		}

	}

	void shell::clear_entry ( setting which ) {

		int32_t room = 0;
		wchar_t* target = field_target ( which, room );

		target [ 0 ] = 0;
		fields [ which ].set ( L"" );

		if ( focus == which )
			focus = -1;

	}

	void shell::fresh_token ( ) {

		uint8_t bytes [ 16 ] = { };

		base::random_bytes ( bytes, sizeof ( bytes ) );
		base::write_hex ( settings.api_token, bytes, sizeof ( bytes ) );
		settings.api_token [ 32 ] = 0;

	}

	bool shell::slide ( int32_t which, float x ) {

		render::box track = slider_boxes [ which ];
		float fraction = track.w > 0.0f ? base::clamp ( ( x - track.x ) / track.w, 0.0f, 1.0f ) : 0.0f;
		uint32_t* target = nullptr;
		uint32_t value = 0;

		if ( which == SETTING_CPU_LIMIT || which == SETTING_GPU_LIMIT ) {

			target = which == SETTING_CPU_LIMIT ? &settings.cpu_limit : &settings.gpu_limit;
			value = 10 + static_cast < uint32_t > ( base::round ( fraction * 18.0f ) ) * 5;

		} else if ( which == SETTING_INTENSITY ) {

			target = &settings.intensity;
			value = 16 + static_cast < uint32_t > ( base::round ( fraction * 10.0f ) );

		} else if ( which == SETTING_HEAT ) {

			uint32_t step = static_cast < uint32_t > ( base::round ( fraction * 31.0f ) );

			target = &settings.gpu_heat;
			value = step ? 59 + step : 0;

		}

		if ( which == SETTING_CARD_INTENSITY || which == SETTING_CARD_LIMIT || which == SETTING_CARD_HEAT ) {

			card_tuning& card = settings.cards [ card_view % adapter_room ];

			if ( !card.custom ) {

				card.custom = true;
				card.limit = settings.gpu_limit;
				card.intensity = settings.intensity;
				card.heat = settings.gpu_heat;

			}

			if ( which == SETTING_CARD_INTENSITY ) {

				target = &card.intensity;
				value = 16 + static_cast < uint32_t > ( base::round ( fraction * 10.0f ) );

			} else if ( which == SETTING_CARD_LIMIT ) {

				target = &card.limit;
				value = 10 + static_cast < uint32_t > ( base::round ( fraction * 18.0f ) ) * 5;

			} else {

				uint32_t step = static_cast < uint32_t > ( base::round ( fraction * 31.0f ) );

				target = &card.heat;
				value = step ? 59 + step : 0;

			}

		}

		if ( !target || *target == value )
			return false;

		*target = value;

		return true;

	}

	int32_t shell::caret_from ( int32_t slot, float x ) {

		const text_field& item = fields [ slot ];
		float local = x - ( field_boxes [ slot ].x + 10.0f ) + field_shift [ slot ];
		wchar_t shown [ text_field::room + 1 ];
		int32_t best = 0;
		float best_gap = 1e9f;

		if ( !measurer )
			return item.length;

		for ( int32_t i = 0; i < item.length; i++ )
			shown [ i ] = slot == SETTING_PASSWORD && !reveal ? L'\x2022' : item.text [ i ];

		shown [ item.length ] = 0;

		for ( int32_t i = 0; i <= item.length; i++ ) {

			float gap = base::abs ( measurer->measure ( shown, render::FONT_BODY, i ) - local );

			if ( gap < best_gap ) {

				best_gap = gap;
				best = i;

			}

		}

		return best;

	}

	command shell::commit_field ( double now ) {

		if ( focus < 0 )
			return COMMAND_NONE;

		pool_profile& profile = settings.pool ( );
		int32_t room = 0;
		wchar_t* target = field_target ( focus, room );
		bool changed = !base::same_text ( target, fields [ focus ].text );
		bool stored = focus < pool_fields || focus == SETTING_APPS;

		if ( changed && stored ) {

			settings_history.remember ( settings );

			if ( focus == SETTING_ADDRESS )
				profile.pin [ 0 ] = 0;
			else if ( focus == SETTING_BACKUP )
				profile.backup_pin [ 0 ] = 0;

		}

		int32_t was = focus;

		base::copy_text ( target, room, fields [ focus ].text );
		focus = -1;

		if ( !changed || !stored )
			return COMMAND_NONE;

		return was < pool_fields && data && data->running && settings.mode == MODE_POOL ? COMMAND_RESTART : COMMAND_PREFERENCES;

	}

	wchar_t* shell::field_target ( int32_t slot, int32_t& room ) {

		pool_profile& profile = settings.pool ( );

		switch ( slot ) {

			case SETTING_ADDRESS:
				room = base::count_of ( profile.address );
				return profile.address;

			case SETTING_BACKUP:
				room = base::count_of ( profile.backup );
				return profile.backup;

			case SETTING_WORKER:
				room = base::count_of ( profile.worker );
				return profile.worker;

			case SETTING_PASSWORD:
				room = base::count_of ( profile.password );
				return profile.password;

			case SETTING_APPS:
				room = base::count_of ( settings.pause_apps );
				return settings.pause_apps;

			case SETTING_PROFILE_NAME:
				room = base::count_of ( profile_name );
				return profile_name;

			default:
				room = base::count_of ( rig_entry );
				return rig_entry;

		}

	}

	command shell::blur ( double now ) {

		dragging = -1;
		press_id = CONTROL_NONE;

		return commit_field ( now );

	}

	command shell::settled ( const preferences& before ) const {

		bool pools = !base::same ( before.pools, settings.pools, sizeof ( settings.pools ) ) || before.algorithm != settings.algorithm;

		return pools && data && data->running && settings.mode == MODE_POOL ? COMMAND_RESTART : COMMAND_PREFERENCES;

	}

	command shell::undo ( bool forward, double now ) {

		preferences before = settings;
		bool moved = forward ? settings_history.step_forward ( settings ) : settings_history.step_back ( settings );

		if ( !moved )
			return COMMAND_NONE;

		sync_fields ( );

		return settled ( before );

	}

	void shell::edit_field ( int32_t kind, double now ) {

		if ( focus >= 0 )
			field_history [ focus ].remember ( fields [ focus ], kind, now );

	}

	command shell::key ( uint32_t code, bool shift, bool control, double now ) {

		if ( focus >= 0 ) {

			text_field& item = fields [ focus ];
			int32_t from = 0;
			int32_t to = 0;
			bool selected = item.selection ( from, to );

			caret_since = now;

			if ( code == key_left )
				item.move_to ( selected && !shift ? from : item.caret - 1, shift );
			else if ( code == key_right )
				item.move_to ( selected && !shift ? to : item.caret + 1, shift );
			else if ( code == key_home )
				item.move_to ( 0, shift );
			else if ( code == key_end )
				item.move_to ( item.length, shift );
			else if ( code == key_back || code == key_delete ) {

				edit_field ( 2, now );
				item.erase ( code == key_delete );

			} else if ( code == key_z && control && !shift )
				field_history [ focus ].step_back ( item );
			else if ( ( code == key_y && control ) || ( code == key_z && control && shift ) )
				field_history [ focus ].step_forward ( item );
			else if ( code == key_a && control ) {

				item.anchor = 0;
				item.caret = item.length;

			} else if ( code == key_enter )
				return commit_field ( now );
			else if ( code == key_escape ) {

				focus = -1;
				sync_fields ( );

			} else if ( code == key_tab ) {

				int32_t left = focus;
				command result = commit_field ( now );

				keyboard_on = true;
				keyboard_id = CONTROL_FIELD;
				keyboard_index = left;
				move_keyboard ( shift ? -1 : 1, now );

				if ( keyboard_id == CONTROL_FIELD && keyboard_index != left )
					focus_field ( static_cast < setting > ( keyboard_index ), now );

				return result;

			}

			return COMMAND_NONE;

		}

		if ( code == key_f5 )
			return COMMAND_TOGGLE;

		if ( code == key_tab && !control ) {

			move_keyboard ( shift ? -1 : 1, now );

			return COMMAND_NONE;

		}

		if ( keyboard_on && ( code == key_enter || ( code == key_space && keyboard_id != CONTROL_FIELD ) ) )
			return press_keyboard ( now );

		if ( keyboard_on && ( code == key_left || code == key_right ) )
			return nudge_keyboard ( code == key_right ? 1 : -1, now );

		if ( keyboard_on && code == key_escape ) {

			keyboard_on = false;

			return COMMAND_NONE;

		}

		if ( control && code == key_z )
			return undo ( shift, now );

		if ( control && code == key_y )
			return undo ( true, now );

		if ( control && code >= key_one && code < key_one + PAGE_COUNT )
			open_page ( static_cast < page > ( code - key_one ), now );

		return COMMAND_NONE;

	}

	int32_t shell::group_of ( control id, int32_t index ) const {

		if ( id == CONTROL_CHOICE )
			return index / 16;

		if ( id == CONTROL_NAV || id == CONTROL_TAB || id == CONTROL_ALGORITHM || id == CONTROL_MODE )
			return 0;

		return index;

	}

	void shell::move_keyboard ( int32_t direction, double now ) {

		int32_t at = -1;

		for ( int32_t i = 0; i < hit_count; i++ ) {

			if ( hits [ i ].id == keyboard_id && hits [ i ].index == keyboard_index )
				at = i;

		}

		if ( !keyboard_on || at < 0 )
			at = direction > 0 ? -1 : hit_count;

		keyboard_on = true;

		int32_t group = at >= 0 && at < hit_count ? group_of ( hits [ at ].id, hits [ at ].index ) : -1;
		control kind = at >= 0 && at < hit_count ? hits [ at ].id : CONTROL_NONE;

		for ( int32_t step = 1; step <= hit_count; step++ ) {

			int32_t next = at + direction * step;

			if ( next < 0 || next >= hit_count )
				break;

			const hit& item = hits [ next ];

			if ( item.id == CONTROL_NONE || ( item.id == kind && group_of ( item.id, item.index ) == group ) )
				continue;

			int32_t landing = next;

			for ( int32_t k = 0; k < hit_count; k++ ) {

				if ( hits [ k ].id == item.id && group_of ( hits [ k ].id, hits [ k ].index ) == group_of ( item.id, item.index ) && hits [ k ].chosen ) {

					landing = k;

					break;

				}

			}

			keyboard_id = hits [ landing ].id;
			keyboard_index = hits [ landing ].index;
			show_keyboard ( now );

			return;

		}

	}

	void shell::show_keyboard ( double now ) {

		for ( int32_t i = 0; i < hit_count; i++ ) {

			const hit& item = hits [ i ];

			if ( item.id != keyboard_id || item.index != keyboard_index || item.scroller < 0 )
				continue;

			render::box view = scroll_area [ item.scroller ];
			float goal = scroll_goal [ item.scroller ];
			float margin = 24.0f;

			if ( item.top - goal < view.y + margin )
				goal = item.top - view.y - margin;
			else if ( item.bottom - goal > view.bottom ( ) - margin )
				goal = item.bottom - view.bottom ( ) + margin;

			goal = base::round ( base::clamp ( goal, 0.0f, scroll_limit [ item.scroller ] ) * zoom ) / zoom;

			if ( goal != scroll_goal [ item.scroller ] ) {

				scroll_goal [ item.scroller ] = goal;
				scroll_motion [ item.scroller ].duration = 0.2f;
				scroll_motion [ item.scroller ].go ( goal, now );

			}

			return;

		}

	}

	command shell::press_keyboard ( double now ) {

		if ( keyboard_id == CONTROL_FIELD ) {

			focus_field ( static_cast < setting > ( keyboard_index ), now );

			return COMMAND_NONE;

		}

		if ( keyboard_id == CONTROL_SLIDER || keyboard_id == CONTROL_NONE )
			return COMMAND_NONE;

		return activate ( keyboard_id, keyboard_index, now );

	}

	bool shell::nudge_setting ( int32_t which, int32_t direction ) {

		auto bump = [ & ] ( uint32_t& value, int32_t step, uint32_t low, uint32_t high ) {

			uint32_t moved = static_cast < uint32_t > ( base::clamp ( static_cast < int32_t > ( value ) + step * direction, static_cast < int32_t > ( low ), static_cast < int32_t > ( high ) ) );
			bool changed = moved != value;

			value = moved;

			return changed;

		};

		card_tuning& card = settings.cards [ card_view % adapter_room ];

		switch ( which ) {

			case SETTING_CPU_LIMIT:
				return bump ( settings.cpu_limit, 5, 10, 100 );

			case SETTING_GPU_LIMIT:
				return bump ( settings.gpu_limit, 5, 10, 100 );

			case SETTING_INTENSITY:
				return bump ( settings.intensity, 1, 16, 26 );

			case SETTING_HEAT:
				if ( !settings.gpu_heat && direction > 0 ) {

					settings.gpu_heat = 60;

					return true;

				}

				if ( settings.gpu_heat == 60 && direction < 0 ) {

					settings.gpu_heat = 0;

					return true;

				}

				return settings.gpu_heat && bump ( settings.gpu_heat, 1, 60, 90 );

			case SETTING_CARD_LIMIT:
				card.custom = true;
				return bump ( card.limit, 5, 10, 100 );

			case SETTING_CARD_INTENSITY:
				card.custom = true;
				return bump ( card.intensity, 1, 16, 26 );

			case SETTING_CARD_HEAT:
				card.custom = true;

				if ( !card.heat && direction > 0 ) {

					card.heat = 60;

					return true;

				}

				if ( card.heat == 60 && direction < 0 ) {

					card.heat = 0;

					return true;

				}

				return card.heat && bump ( card.heat, 1, 60, 90 );

			default:
				return false;

		}

	}

	command shell::nudge_keyboard ( int32_t direction, double now ) {

		if ( keyboard_id == CONTROL_SLIDER ) {

			preferences before = settings;

			if ( !nudge_setting ( keyboard_index, direction ) )
				return COMMAND_NONE;

			settings_history.remember ( before );

			return COMMAND_PREFERENCES;

		}

		if ( keyboard_id == CONTROL_STEP_DOWN || keyboard_id == CONTROL_STEP_UP )
			return activate ( direction > 0 ? CONTROL_STEP_UP : CONTROL_STEP_DOWN, keyboard_index, now );

		int32_t group = group_of ( keyboard_id, keyboard_index );
		const hit* current = nullptr;
		const hit* best = nullptr;

		for ( int32_t i = 0; i < hit_count; i++ ) {

			if ( hits [ i ].id == keyboard_id && hits [ i ].index == keyboard_index )
				current = &hits [ i ];

		}

		if ( !current )
			return COMMAND_NONE;

		bool vertical = keyboard_id == CONTROL_NAV || keyboard_id == CONTROL_ALGORITHM;
		float origin = vertical ? current->area.y : current->area.x;

		for ( int32_t i = 0; i < hit_count; i++ ) {

			const hit& item = hits [ i ];

			if ( item.id != keyboard_id || group_of ( item.id, item.index ) != group || &item == current )
				continue;

			float place = vertical ? item.area.y : item.area.x;

			if ( ( place - origin ) * static_cast < float > ( direction ) <= 0.0f )
				continue;

			if ( !best || base::abs ( place - origin ) < base::abs ( ( vertical ? best->area.y : best->area.x ) - origin ) )
				best = &item;

		}

		if ( !best )
			return COMMAND_NONE;

		keyboard_index = best->index;

		return activate ( best->id, best->index, now );

	}

	void shell::paint_focus ( render::surface& surface ) {

		if ( !keyboard_on || focus >= 0 )
			return;

		for ( int32_t i = 0; i < hit_count; i++ ) {

			if ( hits [ i ].id != keyboard_id || hits [ i ].index != keyboard_index || hits [ i ].hidden )
				continue;

			surface.outline ( hits [ i ].area.inset ( -2.0f ), colors.accent, 1.5f, corner + 2.0f );

			return;

		}

	}

	command shell::character ( wchar_t value, double now ) {

		if ( focus < 0 || value < L' ' || value == 0x7F )
			return COMMAND_NONE;

		wchar_t text [ 2 ] = { value, 0 };

		edit_field ( value == L' ' ? 3 : 1, now );
		fields [ focus ].insert ( text );
		caret_since = now;

		return COMMAND_NONE;

	}

	command shell::paste ( const wchar_t* value, double now ) {

		if ( focus < 0 || !value )
			return COMMAND_NONE;

		edit_field ( 3, now );
		fields [ focus ].insert ( value );
		caret_since = now;

		return COMMAND_NONE;

	}

	bool shell::copy_selection ( wchar_t* out, int32_t room, bool cut, double now ) {

		int32_t from = 0;
		int32_t to = 0;

		if ( focus < 0 || ( focus == SETTING_PASSWORD && !reveal ) || !fields [ focus ].selection ( from, to ) )
			return false;

		int32_t count = base::min ( to - from, room - 1 );

		base::copy ( out, fields [ focus ].text + from, sizeof ( wchar_t ) * static_cast < size_t > ( count ) );
		out [ count ] = 0;

		if ( cut ) {

			edit_field ( 3, now );
			fields [ focus ].erase ( false );
			caret_since = now;

		}

		return true;

	}

	bool shell::animating ( double now ) const {

		if ( now - page_since < page_time || now - hover_since < hover_time || now - fade_since < hover_time || now - frame_since < hover_time )
			return true;

		if ( !nav_marker.done ( now ) || !tab_left.done ( now ) || !tab_width.done ( now ) || !chart_ceiling.done ( now ) || now - chart_since < chart_time )
			return true;

		if ( tip_open && ( tip_closing >= 0.0 || now - tip_since < tip_show ) )
			return true;

		for ( const motion& item : switches ) {

			if ( !item.done ( now ) )
				return true;

		}

		for ( const motion& item : scroll_motion ) {

			if ( !item.done ( now ) )
				return true;

		}

		return false;

	}

	double shell::wake_after ( double now ) const {

		double wait = 1.0;

		if ( tip_pending )
			wait = base::max ( rest_since + tip_delay - now, 0.0 );

		if ( focus < 0 )
			return wait;

		double phase = now - caret_since;

		return base::min ( wait, blink - ( phase - base::floor ( phase / blink ) * blink ) );

	}

	bool shell::over_text ( float x, float y ) const {

		const hit* item = find ( x, y );

		return item && item->id == CONTROL_FIELD;

	}

	void shell::open_page ( page which, double now ) {

		if ( which >= PAGE_COUNT || ( which == shown_page && !setup_open ) )
			return;

		blur ( now );
		setup_open = false;
		shown_page = which;
		page_since = now;
		nav_marker.duration = 0.22f;
		nav_marker.go ( static_cast < float > ( which ), now );

	}

	void shell::open_setup ( double now ) {

		blur ( now );
		setup_open = true;
		page_since = now;

	}

	void shell::open_tab ( int32_t which, double now ) {

		if ( which == tab )
			return;

		tab = which;
		page_since = now;

	}

	void shell::focus_field ( setting which, double now ) {

		if ( which >= field_count )
			return;

		focus = which;
		field_history [ which ].clear ( );
		fields [ which ].anchor = fields [ which ].length;
		fields [ which ].caret = fields [ which ].length;
		caret_since = now;

	}

	bool shell::locate ( control id, int32_t index, render::box& out ) const {

		for ( int32_t i = 0; i < hit_count; i++ ) {

			if ( hits [ i ].id == id && hits [ i ].index == index && !hits [ i ].hidden ) {

				out = hits [ i ].area;

				return true;

			}

		}

		return false;

	}

	void shell::sync_fields ( ) {

		for ( int32_t i = 0; i < field_count; i++ ) {

			int32_t room = 0;
			const wchar_t* value = field_target ( i, room );

			if ( i != focus && !base::same_text ( fields [ i ].text, value ) )
				fields [ i ].set ( value );

		}

	}

	bool shell::paint ( render::surface& surface, const snapshot& view, double now ) {

		if ( !surface.begin ( colors.content ) )
			return false;

		compose ( surface, view, now );

		return surface.end ( );

	}

	void shell::compose ( render::surface& surface, const snapshot& view, double now ) {

		data = &view;
		measurer = &surface;
		zoom = surface.scale ( );
		width = surface.width ( );
		height = surface.height ( );
		hit_count = 0;
		hint_count = 0;
		sync_fields ( );

		if ( !primed ) {

			switches [ SETTING_CPU ].jump ( settings.cpu ? 1.0f : 0.0f );
			switches [ SETTING_GPU ].jump ( settings.gpu ? 1.0f : 0.0f );
			switches [ SETTING_PAUSE_ACTIVE ].jump ( settings.pause_active ? 1.0f : 0.0f );
			switches [ SETTING_PAUSE_BATTERY ].jump ( settings.pause_battery ? 1.0f : 0.0f );
			switches [ SETTING_AUTOSTART ].jump ( settings.autostart ? 1.0f : 0.0f );
			switches [ SETTING_PIN ].jump ( settings.pin_threads ? 1.0f : 0.0f );
			switches [ SETTING_LOGIN ].jump ( settings.launch_at_login ? 1.0f : 0.0f );
			switches [ SETTING_TRAY ].jump ( settings.close_to_tray ? 1.0f : 0.0f );
			switches [ SETTING_API ].jump ( settings.api_port ? 1.0f : 0.0f );
			switches [ SETTING_FULLSCREEN ].jump ( settings.pause_fullscreen ? 1.0f : 0.0f );
			switches [ SETTING_SCHEDULE ].jump ( settings.schedule ? 1.0f : 0.0f );
			switches [ SETTING_NOTIFY ].jump ( settings.notify ? 1.0f : 0.0f );
			switches [ SETTING_HOTKEY ].jump ( settings.hotkey ? 1.0f : 0.0f );
			switches [ SETTING_LOOKUPS ].jump ( settings.lookups ? 1.0f : 0.0f );
			switches [ SETTING_FIAT ].jump ( settings.show_fiat ? 1.0f : 0.0f );
			switches [ SETTING_API_LAN ].jump ( settings.api_lan ? 1.0f : 0.0f );
			switches [ SETTING_API_CONTROL ].jump ( settings.api_control ? 1.0f : 0.0f );
			nav_marker.jump ( static_cast < float > ( shown_page ) );
			chart_serial = view.history_serial;
			primed = true;

		}

		if ( view.history_serial != chart_serial ) {

			chart_serial = view.history_serial;
			chart_since = now;

		}

		render::box body = { sidebar_width, title_height + header_height, width - sidebar_width, height - title_height - header_height - status_height };
		float t = static_cast < float > ( ( now - page_since ) / page_time );
		bool moving = t < 1.0f;

		paint_title ( surface, now );
		paint_sidebar ( surface, view, now );
		paint_header ( surface, view, now );
		surface.push_clip ( body );

		if ( moving ) {

			float eased = base::ease_out ( t );

			surface.push_shift ( 0.0f, page_rise * ( 1.0f - eased ) );
			surface.push_fade ( eased );

		}

		if ( setup_open )
			paint_setup ( surface, view, body, now );
		else if ( shown_page == PAGE_OVERVIEW )
			paint_overview ( surface, view, body, now );
		else if ( shown_page == PAGE_ACTIVITY )
			paint_activity ( surface, view, body, now );
		else if ( shown_page == PAGE_EARNINGS )
			paint_earnings ( surface, view, body, now );
		else if ( shown_page == PAGE_RIGS )
			paint_rigs ( surface, view, body, now );
		else if ( shown_page == PAGE_HELP )
			paint_help ( surface, view, body, now );
		else
			paint_settings ( surface, view, body, now );

		if ( moving ) {

			surface.pop_fade ( );
			surface.pop_shift ( );

		}

		surface.pop_clip ( );
		paint_status ( surface, view );
		paint_focus ( surface );
		update_tip ( now );
		paint_tip ( surface, now );

	}

	void shell::paint_title ( render::surface& surface, double now ) {

		surface.fill ( render::box { 0.0f, 0.0f, width, title_height }, colors.chrome );
		draw_mark ( surface, 14.0f, 11.0f, 18.0f );
		surface.text ( L"Alloy", render::FONT_STRONG, render::box { 41.0f, 0.0f, 120.0f, title_height }, colors.text );

		static const frame_part parts [ 3 ] = { PART_MINIMIZE, PART_MAXIMIZE, PART_CLOSE };

		for ( int32_t i = 0; i < 3; i++ ) {

			render::box area = { width - caption_width * static_cast < float > ( 3 - i ), 0.0f, caption_width, title_height };
			float level = frame_hover == parts [ i ] ? base::ease_out ( static_cast < float > ( ( now - frame_since ) / hover_time ) ) : 0.0f;
			bool closing = parts [ i ] == PART_CLOSE;
			render::color tint = colors.text;

			if ( level > 0.0f ) {

				render::color wash = closing ? colors.close_hover : colors.control_hover;

				if ( frame_pressed == parts [ i ] )
					wash = closing ? render::blend ( colors.close_hover, render::rgb ( 0x000000 ), 0.15f ) : colors.control_line;

				surface.fill ( area, render::faded ( wash, level ) );

				if ( closing )
					tint = render::blend ( colors.text, render::rgb ( 0xFFFFFF ), level );

			}

			icon symbol = parts [ i ] == PART_MINIMIZE ? ICON_MINIMIZE : ( parts [ i ] == PART_CLOSE ? ICON_CLOSE : ( maximized ? ICON_RESTORE : ICON_MAXIMIZE ) );

			draw_icon ( surface, symbol, area.middle_x ( ) - 5.0f, area.middle_y ( ) - 5.0f, 10.0f, tint );

		}

		surface.rule ( sidebar_width, width, title_height - 1.0f, colors.divider );

	}

	void shell::paint_sidebar ( render::surface& surface, const snapshot& view, double now ) {

		render::box bar = { 0.0f, title_height, sidebar_width, height - title_height - status_height };
		float marker = nav_marker.at ( now );

		surface.fill ( bar, colors.chrome );
		surface.column ( sidebar_width - 1.0f, title_height, height - status_height, colors.divider );

		render::box chosen = { 10.0f, title_height + 12.0f + marker * 34.0f, sidebar_width - 20.0f, 32.0f };

		if ( !setup_open ) {

			surface.fill ( chosen, colors.control, corner );
			surface.fill ( render::box { chosen.x, chosen.y + 9.0f, 3.0f, 14.0f }, colors.accent, 1.5f );

		}

		for ( int32_t i = 0; i < PAGE_COUNT; i++ ) {

			render::box item = { 10.0f, title_height + 12.0f + static_cast < float > ( i ) * 34.0f, sidebar_width - 20.0f, 32.0f };
			float level = glow ( CONTROL_NAV, i, now );
			bool selected = i == shown_page && !setup_open;
			float closeness = setup_open ? 0.0f : base::clamp ( 1.0f - base::abs ( marker - static_cast < float > ( i ) ), 0.0f, 1.0f );
			render::color tint = render::blend ( render::blend ( colors.muted, colors.text, level ), colors.text, closeness );

			if ( !selected && level > 0.0f )
				surface.fill ( item, render::faded ( colors.control_hover, level * ( 1.0f - closeness ) ), corner );

			draw_icon ( surface, page_icons [ i ], item.x + 14.0f, item.y + 8.0f, 16.0f, tint );
			surface.text ( page_labels [ i ], render::FONT_BODY, render::box { item.x + 40.0f, item.y, item.w - 48.0f, item.h }, tint );
			add_hit ( item, CONTROL_NAV, i, selected );

		}

		static const wchar_t* const algorithm_tips [ ] = {

			L"Bitcoin's hash. The graphics card does most of the work here, and the SHA instructions help the CPU keep up.",
			L"Litecoin and Dogecoin. Every hash needs 128 KB of scratch memory, which suits the graphics card.",
			L"Monero. It was built for processors, so only the CPU mines it. Full mode keeps a 2 GB dataset in memory.",
			L"Kaspa. Two Keccak hashes around a 64 by 64 matrix product, which the graphics card does best. Dedicated machines mine most of it now.",

		};

		float list = title_height + 12.0f + static_cast < float > ( PAGE_COUNT ) * 34.0f + 22.0f;
		bool locked = view.running;

		caption ( surface, render::box { 22.0f, list, sidebar_width - 44.0f, 16.0f }, L"ALGORITHM" );
		list += 24.0f;

		for ( int32_t i = 0; i < chain::ALGORITHM_COUNT; i++ ) {

			render::box item = { 10.0f, list + static_cast < float > ( i ) * 42.0f, sidebar_width - 20.0f, 40.0f };
			bool selected = i == settings.algorithm;
			float level = !locked && !selected ? glow ( CONTROL_ALGORITHM, i, now ) : 0.0f;
			render::color tint = selected ? colors.text : render::blend ( colors.muted, colors.text, level );

			if ( locked && !selected )
				surface.push_fade ( 0.5f );

			if ( level > 0.0f )
				surface.fill ( item, render::faded ( colors.control_hover, level ), corner );

			if ( selected ) {

				surface.dot ( item.x + 18.0f, item.y + 14.0f, 5.0f, colors.accent );
				surface.dot ( item.x + 18.0f, item.y + 14.0f, 2.0f, colors.chrome );

			} else
				surface.ring ( item.x + 18.0f, item.y + 14.0f, 4.5f, render::blend ( colors.faint, colors.muted, level ), 1.25f );

			surface.text ( chain::algorithm_name ( static_cast < uint8_t > ( i ) ), selected ? render::FONT_STRONG : render::FONT_BODY, render::box { item.x + 32.0f, item.y + 4.0f, item.w - 40.0f, 20.0f }, tint );
			surface.text ( chain::algorithm_coins ( static_cast < uint8_t > ( i ) ), render::FONT_SMALL, render::box { item.x + 32.0f, item.y + 21.0f, item.w - 40.0f, 16.0f }, colors.faint );

			if ( locked && !selected )
				surface.pop_fade ( );

			if ( !locked && !selected )
				add_hit ( item, CONTROL_ALGORITHM, i );

			add_hint ( item, locked && !selected ? L"Stop mining first, then pick another algorithm." : algorithm_tips [ i ] );

		}

		float top = height - status_height - 112.0f;
		float inner = sidebar_width - 44.0f;
		base::text line;

		if ( list + static_cast < float > ( chain::ALGORITHM_COUNT ) * 42.0f + 8.0f > top )
			return;

		caption ( surface, render::box { 22.0f, top, inner, 16.0f }, L"THIS COMPUTER" );
		draw_icon ( surface, ICON_PROCESSOR, 22.0f, top + 30.0f, 14.0f, colors.muted );
		surface.text ( view.cpu_name, render::FONT_SMALL, render::box { 44.0f, top + 24.0f, inner - 22.0f, 18.0f }, colors.text );
		line.number ( view.cores ).add ( L" cores, " ).number ( view.threads ).add ( L" threads" );
		surface.text ( line.get ( ), render::FONT_SMALL, render::box { 44.0f, top + 41.0f, inner - 22.0f, 16.0f }, colors.faint );
		draw_icon ( surface, ICON_GRAPHICS, 22.0f, top + 72.0f, 14.0f, colors.muted );
		surface.text ( view.adapter_count ? view.adapters [ base::min ( static_cast < int32_t > ( settings.adapter ), view.adapter_count - 1 ) ] : L"No graphics adapter", render::FONT_SMALL, render::box { 44.0f, top + 66.0f, inner - 22.0f, 18.0f }, colors.text );
		surface.text ( view.adapter_count ? L"Direct3D 11 compute" : L"GPU mining unavailable", render::FONT_SMALL, render::box { 44.0f, top + 83.0f, inner - 22.0f, 16.0f }, colors.faint );

	}

	void shell::paint_header ( render::surface& surface, const snapshot& view, double now ) {

		render::box head = { sidebar_width, title_height, width - sidebar_width, header_height };
		render::box toggle_area = { head.right ( ) - 28.0f - 108.0f, head.y + 16.0f, 108.0f, 32.0f };
		render::box modes = { toggle_area.x - 12.0f - 270.0f, toggle_area.y, 270.0f, 32.0f };
		bool can_start = settings.cpu || ( settings.gpu && view.adapter_count > 0 );

		surface.text ( setup_open ? L"Set up" : page_labels [ shown_page ], render::FONT_HEADING, render::box { head.x + 28.0f, head.y, 260.0f, head.h }, colors.text );

		if ( view.running )
			button ( surface, toggle_area, L"Stop", ICON_STOP, CONTROL_TOGGLE, 0, false, true, now );
		else
			button ( surface, toggle_area, L"Start", ICON_PLAY, CONTROL_TOGGLE, 0, true, can_start, now );

		static const wchar_t* const mode_tips [ ] = {

			L"Mine for the pool set in Settings and send it your shares.",
			L"Hash flat out on test work to see how fast this PC is. Nothing gets sent anywhere.",
			L"Search block 0's nonces until it finds the one Satoshi found in January 2009.",

		};

		segmented ( surface, modes, mode_labels, MODE_COUNT, settings.mode, CONTROL_MODE, 0, !view.running, now );

		float part = modes.w / static_cast < float > ( MODE_COUNT );

		for ( int32_t i = 0; i < MODE_COUNT; i++ )
			add_hint ( render::box { modes.x + part * static_cast < float > ( i ), modes.y, part, modes.h }, mode_tips [ i ] );

		surface.rule ( head.x, head.right ( ), head.bottom ( ) - 1.0f, colors.divider );

	}

	void shell::paint_status ( render::surface& surface, const snapshot& view ) {

		render::box bar = { 0.0f, height - status_height, width, status_height };
		render::color state = colors.faint;
		base::text summary;

		surface.fill ( bar, colors.chrome );
		surface.rule ( 0.0f, width, bar.y, colors.divider );

		if ( view.running && view.paused )
			state = colors.warning;
		else if ( view.running && view.mode == MODE_POOL && view.link == PHASE_REFUSED )
			state = colors.bad;
		else if ( view.running && view.mode == MODE_POOL && view.link != PHASE_READY )
			state = colors.warning;
		else if ( view.running )
			state = colors.good;
		else if ( view.solved )
			state = colors.accent;

		surface.dot ( 17.0f, bar.middle_y ( ), 3.5f, state );
		surface.text ( view.status, render::FONT_SMALL, render::box { 28.0f, bar.y, width * 0.55f, bar.h }, colors.muted );

		if ( settings.cpu ) {

			summary.add ( L"CPU " ).number ( settings.threads ).add ( settings.threads == 1 ? L" thread" : L" threads" );

			if ( settings.cpu_limit < 100 )
				summary.add ( L" at " ).number ( settings.cpu_limit ).put ( L'%' );

		}

		if ( settings.gpu && view.adapter_count && settings.algorithm != chain::ALGORITHM_RANDOMX ) {

			summary.add ( summary.size ( ) ? L"     GPU " : L"GPU " );

			if ( settings.algorithm == chain::ALGORITHM_SHA256D )
				summary.add ( L"intensity " ).number ( settings.intensity );
			else
				summary.add ( L"on" );

			if ( settings.gpu_limit < 100 )
				summary.add ( L" at " ).number ( settings.gpu_limit ).put ( L'%' );

		}

		if ( settings.algorithm == chain::ALGORITHM_RANDOMX )
			summary.add ( summary.size ( ) ? L"     " : L"" ).add ( settings.full_dataset ? L"Full dataset" : L"Light dataset" );

		summary.add ( summary.size ( ) ? L"     Alloy " ALLOY_VERSION_TEXT : L"Alloy " ALLOY_VERSION_TEXT );
		surface.text ( summary.get ( ), render::FONT_SMALL, render::box { width * 0.45f, bar.y, width * 0.55f - 16.0f, bar.h }, colors.faint, render::ALIGN_RIGHT );

	}

	void shell::button ( render::surface& surface, render::box area, const wchar_t* label, icon symbol, control id, int32_t index, bool primary, bool enabled, double now ) {

		float level = enabled ? glow ( id, index, now ) : 0.0f;
		bool pressed = enabled && press_id == id && press_index == index && hover_id == id && hover_index == index;
		render::color tint = primary ? colors.accent_text : colors.text;

		if ( !enabled )
			surface.push_fade ( 0.45f );

		if ( primary )
			surface.fill ( area, pressed ? colors.accent : render::blend ( colors.accent, colors.accent_hover, level ), corner );
		else {

			surface.fill ( area, pressed ? colors.control_line : render::blend ( colors.control, colors.control_hover, level ), corner );
			surface.outline ( area, colors.control_line, 1.0f / zoom, corner );

		}

		float wide = surface.measure ( label, render::FONT_STRONG );
		float left = area.middle_x ( ) - ( wide + 22.0f ) * 0.5f;

		draw_icon ( surface, symbol, left, area.middle_y ( ) - 7.0f, 14.0f, tint );
		surface.text ( label, render::FONT_STRONG, render::box { left + 22.0f, area.y, wide + 4.0f, area.h }, tint );

		if ( !enabled )
			surface.pop_fade ( );
		else
			add_hit ( area, id, index );

	}

	void shell::segmented ( render::surface& surface, render::box area, const wchar_t* const* labels, int32_t count, int32_t selected, control id, int32_t base, bool enabled, double now ) {

		float part = area.w / static_cast < float > ( count );

		if ( !enabled )
			surface.push_fade ( 0.5f );

		surface.fill ( area, colors.control, corner );
		surface.outline ( area, colors.control_line, 1.0f / zoom, corner );

		for ( int32_t i = 0; i < count; i++ ) {

			render::box piece = { area.x + part * static_cast < float > ( i ), area.y, part, area.h };
			render::box inner = piece.inset ( 3.0f );
			float level = enabled && i != selected ? glow ( id, base + i, now ) : 0.0f;
			render::color tint = render::blend ( colors.muted, colors.text, level );

			if ( i == selected ) {

				surface.fill ( inner, colors.accent_soft, 3.0f );
				tint = colors.accent;

			} else if ( level > 0.0f )
				surface.fill ( inner, render::faded ( colors.control_hover, level ), 3.0f );

			surface.text ( labels [ i ], i == selected ? render::FONT_SMALL_STRONG : render::FONT_SMALL, piece.inset ( 6.0f, 0.0f ), tint, render::ALIGN_CENTER );

			if ( enabled )
				add_hit ( piece, id, base + i, i == selected );

		}

		if ( !enabled )
			surface.pop_fade ( );

	}

	void shell::chips ( render::surface& surface, render::box area, const wchar_t* const* labels, int32_t count, uint32_t mask, setting which, bool enabled, double now ) {

		float gap = 4.0f;
		float part = ( area.w - gap * static_cast < float > ( count - 1 ) ) / static_cast < float > ( count );

		if ( !enabled )
			surface.push_fade ( 0.45f );

		for ( int32_t i = 0; i < count; i++ ) {

			render::box piece = { area.x + ( part + gap ) * static_cast < float > ( i ), area.y, part, area.h };
			int32_t index = which * 16 + i;
			bool on = mask >> i & 1u;
			float level = enabled ? glow ( CONTROL_CHOICE, index, now ) : 0.0f;

			surface.fill ( piece, on ? colors.accent_soft : render::blend ( colors.control, colors.control_hover, level ), corner );
			surface.outline ( piece, on ? render::faded ( colors.accent, 0.6f ) : colors.control_line, 1.0f / zoom, corner );
			surface.text ( labels [ i ], on ? render::FONT_SMALL_STRONG : render::FONT_SMALL, piece, on ? colors.accent : render::blend ( colors.muted, colors.text, level ), render::ALIGN_CENTER );

			if ( enabled )
				add_hit ( piece, CONTROL_CHOICE, index );

		}

		if ( !enabled )
			surface.pop_fade ( );

	}

	void shell::toggle ( render::surface& surface, render::box area, setting which, bool value, bool enabled, double now ) {

		motion& knob = switches [ which ];

		knob.duration = 0.16f;
		knob.go ( value ? 1.0f : 0.0f, now );

		float t = knob.at ( now );
		float level = enabled ? glow ( CONTROL_SWITCH, which, now ) : 0.0f;
		render::color off = render::blend ( colors.control_line, colors.faint, level * 0.5f );
		render::color on = render::blend ( colors.accent, colors.accent_hover, level );

		if ( !enabled )
			surface.push_fade ( 0.45f );

		surface.fill ( area, render::blend ( off, on, t ), area.h * 0.5f );
		surface.dot ( area.x + area.h * 0.5f + t * ( area.w - area.h ), area.middle_y ( ), area.h * 0.5f - 2.5f, render::rgb ( 0xFFFFFF ) );

		if ( !enabled )
			surface.pop_fade ( );
		else
			add_hit ( area.inset ( -6.0f ), CONTROL_SWITCH, which );

	}

	void shell::stepper ( render::surface& surface, render::box area, setting which, const wchar_t* value, bool lower, bool higher, double now ) {

		render::box down = { area.x, area.y, 32.0f, area.h };
		render::box up = { area.right ( ) - 32.0f, area.y, 32.0f, area.h };

		surface.fill ( area, colors.control, corner );

		float down_level = lower ? glow ( CONTROL_STEP_DOWN, which, now ) : 0.0f;
		float up_level = higher ? glow ( CONTROL_STEP_UP, which, now ) : 0.0f;

		if ( down_level > 0.0f )
			surface.fill ( down.inset ( 1.0f ), render::faded ( colors.control_hover, down_level ), 3.0f );

		if ( up_level > 0.0f )
			surface.fill ( up.inset ( 1.0f ), render::faded ( colors.control_hover, up_level ), 3.0f );

		surface.outline ( area, colors.control_line, 1.0f / zoom, corner );
		surface.column ( down.right ( ), area.y, area.bottom ( ), colors.control_line );
		surface.column ( up.x, area.y, area.bottom ( ), colors.control_line );
		draw_icon ( surface, ICON_MINUS, down.middle_x ( ) - 7.0f, down.middle_y ( ) - 7.0f, 14.0f, lower ? colors.text : colors.faint );
		draw_icon ( surface, ICON_PLUS, up.middle_x ( ) - 7.0f, up.middle_y ( ) - 7.0f, 14.0f, higher ? colors.text : colors.faint );
		surface.text ( value, render::FONT_BODY, render::box { down.right ( ), area.y, up.x - down.right ( ), area.h }, colors.text, render::ALIGN_CENTER );

		if ( lower )
			add_hit ( down, CONTROL_STEP_DOWN, which );

		if ( higher )
			add_hit ( up, CONTROL_STEP_UP, which );

	}

	void shell::slider ( render::surface& surface, render::box area, setting which, float fraction, const wchar_t* value, bool enabled, double now ) {

		render::box track = { area.x + 8.0f, area.y, area.w - 8.0f - 64.0f, area.h };
		float level = enabled ? base::max ( glow ( CONTROL_SLIDER, which, now ), dragging == which ? 1.0f : 0.0f ) : 0.0f;
		float middle = area.middle_y ( );
		float knob = track.x + track.w * base::clamp ( fraction, 0.0f, 1.0f );

		slider_boxes [ which ] = track;

		if ( !enabled )
			surface.push_fade ( 0.45f );

		surface.fill ( render::box { track.x, middle - 2.0f, track.w, 4.0f }, colors.control_line, 2.0f );
		surface.fill ( render::box { track.x, middle - 2.0f, knob - track.x, 4.0f }, colors.accent, 2.0f );
		surface.dot ( knob, middle, 8.0f, render::blend ( colors.control_line, colors.accent, level ) );
		surface.dot ( knob, middle, 6.5f, render::rgb ( 0xFFFFFF ) );
		surface.text ( value, render::FONT_SMALL_STRONG, render::box { area.right ( ) - 56.0f, area.y, 56.0f, area.h }, colors.text, render::ALIGN_RIGHT );

		if ( !enabled )
			surface.pop_fade ( );
		else
			add_hit ( render::box { track.x - 10.0f, area.y, track.w + 20.0f, area.h }, CONTROL_SLIDER, which );

	}

	void shell::field ( render::surface& surface, render::box area, setting which, bool masked, double now, bool warn ) {

		int32_t slot = which;
		text_field& item = fields [ slot ];
		bool focused = focus == slot;
		bool hidden = masked && !reveal;
		float level = glow ( CONTROL_FIELD, slot, now );
		float eye_width = masked ? 32.0f : 0.0f;
		render::box inner = { area.x + 10.0f, area.y, area.w - 20.0f - eye_width + ( masked ? 6.0f : 0.0f ), area.h };
		render::box eye = { area.right ( ) - eye_width, area.y, eye_width, area.h };
		wchar_t shown [ text_field::room + 1 ];

		field_boxes [ slot ] = area;

		for ( int32_t i = 0; i < item.length; i++ )
			shown [ i ] = hidden ? L'\x2022' : item.text [ i ];

		shown [ item.length ] = 0;

		float caret_x = surface.measure ( shown, render::FONT_BODY, item.caret );
		float full = surface.measure ( shown, render::FONT_BODY );

		if ( !focused )
			field_shift [ slot ] = 0.0f;
		else if ( caret_x - field_shift [ slot ] > inner.w - 2.0f )
			field_shift [ slot ] = caret_x - inner.w + 2.0f;
		else if ( caret_x < field_shift [ slot ] )
			field_shift [ slot ] = caret_x;

		float shift = surface.snap ( field_shift [ slot ] );

		surface.fill ( area, colors.control, corner );

		if ( focused )
			surface.outline ( area, colors.accent, 1.5f, corner );
		else if ( warn )
			surface.outline ( area, colors.warning, 1.25f, corner );
		else
			surface.outline ( area, render::blend ( colors.control_line, colors.faint, level * 0.6f ), 1.0f / zoom, corner );

		surface.push_clip ( inner );

		int32_t from = 0;
		int32_t to = 0;

		if ( focused && item.selection ( from, to ) ) {

			float left = surface.measure ( shown, render::FONT_BODY, from );
			float right = surface.measure ( shown, render::FONT_BODY, to );

			surface.fill ( render::box { inner.x + left - shift, area.y + 6.0f, right - left, area.h - 12.0f }, colors.selection );

		}

		if ( item.length )
			surface.text ( shown, render::FONT_BODY, render::box { inner.x - shift, area.y, full + 16.0f, area.h }, colors.text );

		if ( focused ) {

			double phase = now - caret_since;

			if ( phase - base::floor ( phase / ( blink * 2.0 ) ) * blink * 2.0 < blink )
				surface.fill ( render::box { surface.snap ( inner.x + caret_x - shift ), area.y + 7.0f, 1.5f, area.h - 14.0f }, colors.text );

		}

		surface.pop_clip ( );
		add_hit ( area, CONTROL_FIELD, slot );

		if ( !masked )
			return;

		float eye_level = glow ( CONTROL_REVEAL, 0, now );
		render::box spot = eye.inset ( 4.0f, 5.0f );

		if ( eye_level > 0.0f )
			surface.fill ( spot, render::faded ( colors.control_hover, eye_level ), 3.0f );

		draw_icon ( surface, reveal ? ICON_HIDDEN : ICON_EYE, eye.middle_x ( ) - 8.0f, eye.middle_y ( ) - 8.0f, 16.0f, render::blend ( colors.muted, colors.text, eye_level ) );
		add_hit ( spot, CONTROL_REVEAL, 0 );
		add_hint ( spot, reveal ? L"Hide the password again." : L"Show the password while you type it." );

	}

	void shell::caption ( render::surface& surface, render::box area, const wchar_t* text ) {

		surface.text ( text, render::FONT_CAPTION, area, colors.muted );

	}

	void shell::property ( render::surface& surface, float x, float& y, float wide, const wchar_t* label, const wchar_t* value, render::font role ) {

		surface.text ( label, render::FONT_BODY, render::box { x, y, wide * 0.42f, 26.0f }, colors.muted );
		surface.text ( value, role, render::box { x + wide * 0.42f, y, wide * 0.58f, 26.0f }, colors.text, render::ALIGN_RIGHT );
		y += 26.0f;

	}

	void shell::hash_property ( render::surface& surface, float x, float& y, float wide, const wchar_t* label, const uint8_t* hash ) {

		base::text full;
		wchar_t fitted [ 80 ];

		full.hex ( hash, 32, true );
		fit_middle ( surface, full.get ( ), render::FONT_MONO, wide * 0.58f, fitted, base::count_of ( fitted ) );
		property ( surface, x, y, wide, label, fitted, render::FONT_MONO );

	}

	void shell::fit_middle ( render::surface& surface, const wchar_t* value, render::font role, float wide, wchar_t* out, int32_t room ) {

		int32_t count = base::length ( value );

		base::copy_text ( out, room, value );

		if ( surface.measure ( value, role ) <= wide )
			return;

		for ( int32_t keep = count / 2 - 1; keep > 2; keep-- ) {

			base::writer < wchar_t, 128 > trimmed;

			trimmed.add ( value, keep ).put ( L'\x2026' ).add ( value + count - keep );

			if ( surface.measure ( trimmed.get ( ), role ) <= wide || keep == 3 ) {

				base::copy_text ( out, room, trimmed.get ( ) );

				return;

			}

		}

	}

	void shell::begin_row ( render::surface& surface, render::box& row, const wchar_t* label, const wchar_t* tip ) {

		surface.text ( label, render::FONT_BODY, render::box { row.x, row.y, row.w * 0.5f, row.h }, colors.text );
		surface.rule ( row.x, row.right ( ), row.bottom ( ) - 1.0f, colors.divider );
		add_hint ( row, tip );

	}

}
