#pragma once

#include "ui/icons.hh"
#include "ui/model.hh"
#include "ui/theme.hh"

namespace ui {

	enum page : uint8_t {

		PAGE_OVERVIEW,
		PAGE_ACTIVITY,
		PAGE_EARNINGS,
		PAGE_RIGS,
		PAGE_SETTINGS,
		PAGE_HELP,
		PAGE_COUNT,

	};

	enum control : uint8_t {

		CONTROL_NONE,
		CONTROL_NAV,
		CONTROL_TOGGLE,
		CONTROL_MODE,
		CONTROL_TAB,
		CONTROL_CLEAR_LOG,
		CONTROL_FIELD,
		CONTROL_SWITCH,
		CONTROL_STEP_DOWN,
		CONTROL_STEP_UP,
		CONTROL_SLIDER,
		CONTROL_CHOICE,
		CONTROL_ALGORITHM,
		CONTROL_REVEAL,
		CONTROL_PRESET,
		CONTROL_ACTION,
		CONTROL_NEW_TOKEN,
		CONTROL_CARD_RESET,
		CONTROL_CHART_GEAR,
		CONTROL_CHART_MENU,
		CONTROL_CHART_RESET,

	};

	enum setting : uint8_t {

		SETTING_ADDRESS,
		SETTING_BACKUP,
		SETTING_WORKER,
		SETTING_PASSWORD,
		SETTING_APPS,
		SETTING_PROFILE_NAME,
		SETTING_RIG,
		SETTING_CPU,
		SETTING_THREADS,
		SETTING_CPU_LIMIT,
		SETTING_PRIORITY,
		SETTING_KERNEL,
		SETTING_GPU,
		SETTING_ADAPTER,
		SETTING_INTENSITY,
		SETTING_GPU_LIMIT,
		SETTING_PAUSE_ACTIVE,
		SETTING_IDLE_MINUTES,
		SETTING_PAUSE_BATTERY,
		SETTING_AUTOSTART,
		SETTING_THEME,
		SETTING_DATASET,
		SETTING_PIN,
		SETTING_HEAT,
		SETTING_LOGIN,
		SETTING_TRAY,
		SETTING_API,
		SETTING_COIN,
		SETTING_FULLSCREEN,
		SETTING_SCHEDULE,
		SETTING_SCHEDULE_FROM,
		SETTING_SCHEDULE_TO,
		SETTING_SCHEDULE_DAYS,
		SETTING_NOTIFY,
		SETTING_HOTKEY,
		SETTING_API_LAN,
		SETTING_API_CONTROL,
		SETTING_CHART_SPAN,
		SETTING_CHART_SERIES,
		SETTING_CHART_STYLE,
		SETTING_CHART_SMOOTHING,
		SETTING_CHART_UPDATE,
		SETTING_CHART_TIME,
		SETTING_FIAT,
		SETTING_LOOKUPS,
		SETTING_CURRENCY,
		SETTING_PROFILE,
		SETTING_CARD,
		SETTING_CARD_ON,
		SETTING_CARD_INTENSITY,
		SETTING_CARD_LIMIT,
		SETTING_CARD_HEAT,
		SETTING_COUNT,

	};

	enum frame_part : uint8_t {

		PART_CLIENT,
		PART_CAPTION,
		PART_MINIMIZE,
		PART_MAXIMIZE,
		PART_CLOSE,

	};

	enum scroller : uint8_t {

		SCROLL_SHARES,
		SCROLL_LOG,
		SCROLL_SETTINGS,
		SCROLL_EARNINGS,
		SCROLL_RIGS,
		SCROLL_HELP,
		SCROLL_SETUP,
		SCROLL_COUNT,

	};

	static constexpr int32_t field_count = 7;
	static constexpr int32_t pool_fields = 4;

	struct text_field {

		static constexpr int32_t room = 255;

		wchar_t text [ room + 1 ] = { };
		int32_t length = 0;
		int32_t caret = 0;
		int32_t anchor = 0;

		void set ( const wchar_t* value );

		void insert ( const wchar_t* value );

		void erase ( bool forward );

		void move_to ( int32_t at, bool extend );

		bool selection ( int32_t& from, int32_t& to ) const;

	};

	struct field_memory {

		static constexpr int32_t depth = 32;

		text_field undo [ depth ];
		text_field redo [ depth ];
		int32_t undo_count = 0;
		int32_t redo_count = 0;
		int32_t last_kind = 0;
		double last_edit = -100.0;

		void clear ( );

		void remember ( const text_field& current, int32_t kind, double now );

		bool step_back ( text_field& current );

		bool step_forward ( text_field& current );

	};

	struct settings_memory {

		static constexpr int32_t depth = 24;

		preferences past [ depth ];
		preferences ahead [ depth ];
		int32_t past_count = 0;
		int32_t ahead_count = 0;

		void remember ( const preferences& current );

		bool step_back ( preferences& current );

		bool step_forward ( preferences& current );

	};

	struct motion {

		float from = 0.0f;
		float to = 0.0f;
		double since = -100.0;
		float duration = 0.2f;

		float at ( double now ) const;

		bool done ( double now ) const;

		void go ( float target, double now );

		void jump ( float target );

	};

	class shell {

	public:

		static constexpr float title_height = 40.0f;
		static constexpr float sidebar_width = 216.0f;
		static constexpr float status_height = 28.0f;
		static constexpr float header_height = 64.0f;
		static constexpr float caption_width = 46.0f;

		explicit shell ( preferences& settings ) : settings ( settings ) { }

		bool paint ( render::surface& surface, const snapshot& view, double now );

		void compose ( render::surface& surface, const snapshot& view, double now );

		render::color background ( ) const {

			return colors.content;

		}

		frame_part part_at ( float x, float y ) const;

		void hover_frame ( frame_part part, double now );

		void press_frame ( frame_part part );

		command mouse_move ( float x, float y, double now );

		void mouse_leave ( double now );

		command mouse_down ( float x, float y, bool shift, double now );

		command mouse_up ( float x, float y, double now );

		void wheel ( float x, float y, float delta, double now );

		command key ( uint32_t code, bool shift, bool control, double now );

		command character ( wchar_t value, double now );

		command paste ( const wchar_t* value, double now );

		bool copy_selection ( wchar_t* out, int32_t room, bool cut, double now );

		command blur ( double now );

		bool animating ( double now ) const;

		double wake_after ( double now ) const;

		bool over_text ( float x, float y ) const;

		void open_page ( page which, double now );

		void open_tab ( int32_t which, double now );

		void open_setup ( double now );

		bool in_setup ( ) const {

			return setup_open;

		}

		void focus_field ( setting which, double now );

		bool locate ( control id, int32_t index, render::box& out ) const;

		void set_maximized ( bool value ) {

			maximized = value;

		}

		void set_size ( float wide, float tall ) {

			width = wide;
			height = tall;

		}

		void set_light ( bool value ) {

			colors = value ? light_palette ( ) : dark_palette ( );

		}

		page current ( ) const {

			return shown_page;

		}

		void fresh_token ( );

		const wchar_t* pending_rig ( ) const {

			return rig_entry;

		}

		const wchar_t* pending_profile ( ) const {

			return profile_name;

		}

		void clear_entry ( setting which );

		void reload ( ) {

			focus = -1;
			sync_fields ( );

		}

		int32_t argument ( ) const {

			return action_argument;

		}

		bool chart_menu_open ( ) const {

			return chart_menu;

		}

		bool keyboard_at ( control id, int32_t index ) const {

			return keyboard_on && keyboard_id == id && keyboard_index == index;

		}

		bool editing ( ) const {

			return focus >= 0;

		}

		bool waiting ( ) const {

			return focus >= 0 || tip_pending;

		}

	private:

		struct hit {

			render::box area;
			control id = CONTROL_NONE;
			int32_t index = 0;
			bool chosen = false;
			bool hidden = false;
			int32_t scroller = -1;
			float top = 0.0f;
			float bottom = 0.0f;

		};

		struct hint {

			render::box area;
			uint64_t key = 0;
			wchar_t text [ 640 ] = { };

		};

		void add_hit ( render::box area, control id, int32_t index, bool chosen = false );

		void add_hint ( render::box area, const wchar_t* text );

		void update_tip ( double now );

		void paint_tip ( render::surface& surface, double now );

		const hit* find ( float x, float y ) const;

		float glow ( control id, int32_t index, double now ) const;

		command activate ( control id, int32_t index, double now );

		int32_t group_of ( control id, int32_t index ) const;

		void move_keyboard ( int32_t direction, double now );

		void show_keyboard ( double now );

		command press_keyboard ( double now );

		command nudge_keyboard ( int32_t direction, double now );

		bool nudge_setting ( int32_t which, int32_t direction );

		void paint_focus ( render::surface& surface );

		void sync_gear ( double now );

		void paint_chart_gear ( render::surface& surface, render::box area, double now );

		void paint_chart_menu ( render::surface& surface, double now );

		void close_chart_menu ( double now );

		bool flowing ( ) const {

			return settings.chart_update == 3 && shown_page == PAGE_OVERVIEW && !setup_open && data && data->running && !data->paused && data->history_count >= 2;

		}

		command commit_field ( double now );

		command undo ( bool forward, double now );

		command settled ( const preferences& before ) const;

		void edit_field ( int32_t kind, double now );

		bool slide ( int32_t which, float x );

		int32_t caret_from ( int32_t slot, float x );

		void sync_fields ( );

		wchar_t* field_target ( int32_t slot, int32_t& room );

		float scroll_offset ( scroller which, double now ) const;

		void paint_title ( render::surface& surface, double now );

		void paint_sidebar ( render::surface& surface, const snapshot& view, double now );

		void paint_header ( render::surface& surface, const snapshot& view, double now );

		void paint_status ( render::surface& surface, const snapshot& view );

		void paint_overview ( render::surface& surface, const snapshot& view, render::box area, double now );

		void paint_metrics ( render::surface& surface, const snapshot& view, render::box area );

		void paint_chart ( render::surface& surface, const snapshot& view, render::box area, double now );

		void paint_devices ( render::surface& surface, const snapshot& view, render::box area );

		void paint_inspector ( render::surface& surface, const snapshot& view, render::box area );

		void paint_activity ( render::surface& surface, const snapshot& view, render::box area, double now );

		void paint_settings ( render::surface& surface, const snapshot& view, render::box area, double now );

		void paint_earnings ( render::surface& surface, const snapshot& view, render::box area, double now );

		void paint_rigs ( render::surface& surface, const snapshot& view, render::box area, double now );

		void paint_help ( render::surface& surface, const snapshot& view, render::box area, double now );

		void paint_setup ( render::surface& surface, const snapshot& view, render::box area, double now );

		void button ( render::surface& surface, render::box area, const wchar_t* label, icon symbol, control id, int32_t index, bool primary, bool enabled, double now );

		void segmented ( render::surface& surface, render::box area, const wchar_t* const* labels, int32_t count, int32_t selected, control id, int32_t base, bool enabled, double now );

		void chips ( render::surface& surface, render::box area, const wchar_t* const* labels, int32_t count, uint32_t mask, setting which, bool enabled, double now );

		void toggle ( render::surface& surface, render::box area, setting which, bool value, bool enabled, double now );

		void stepper ( render::surface& surface, render::box area, setting which, const wchar_t* value, bool lower, bool higher, double now );

		void slider ( render::surface& surface, render::box area, setting which, float fraction, const wchar_t* value, bool enabled, double now );

		void field ( render::surface& surface, render::box area, setting which, bool masked, double now, bool warn = false );

		void caption ( render::surface& surface, render::box area, const wchar_t* text );

		void property ( render::surface& surface, float x, float& y, float wide, const wchar_t* label, const wchar_t* value, render::font role = render::FONT_BODY );

		void hash_property ( render::surface& surface, float x, float& y, float wide, const wchar_t* label, const uint8_t* hash );

		void fit_middle ( render::surface& surface, const wchar_t* value, render::font role, float wide, wchar_t* out, int32_t room );

		void begin_row ( render::surface& surface, render::box& row, const wchar_t* label, const wchar_t* tip );

		preferences& settings;
		const snapshot* data = nullptr;
		palette colors = dark_palette ( );
		hit hits [ 384 ];
		int32_t hit_count = 0;
		float hit_offset = 0.0f;
		float hit_top = -1e9f;
		float hit_bottom = 1e9f;
		int32_t hit_scroller = -1;
		hint hints [ 48 ];
		int32_t hint_count = 0;
		float rest_x = -1000.0f;
		float rest_y = -1000.0f;
		double rest_since = 0.0;
		bool tip_open = false;
		bool tip_pending = false;
		uint64_t tip_key = 0;
		uint64_t tip_quiet = 0;
		wchar_t tip_text [ 640 ] = { };
		float tip_x = 0.0f;
		float tip_y = 0.0f;
		double tip_since = -100.0;
		double tip_closing = -100.0;
		float width = 0.0f;
		float height = 0.0f;
		bool maximized = false;
		page shown_page = PAGE_OVERVIEW;
		bool setup_open = false;
		double page_since = -100.0;
		motion nav_marker;
		int32_t tab = 0;
		motion tab_left;
		motion tab_width;
		control hover_id = CONTROL_NONE;
		int32_t hover_index = 0;
		double hover_since = -100.0;
		control fade_id = CONTROL_NONE;
		int32_t fade_index = 0;
		double fade_since = -100.0;
		float fade_level = 0.0f;
		control press_id = CONTROL_NONE;
		int32_t press_index = 0;
		frame_part frame_hover = PART_CLIENT;
		frame_part frame_pressed = PART_CLIENT;
		double frame_since = -100.0;
		float mouse_x = -1.0f;
		float mouse_y = -1.0f;
		int32_t focus = -1;
		text_field fields [ field_count ];
		field_memory field_history [ field_count ];
		settings_memory settings_history;
		bool reveal = false;
		render::box field_boxes [ field_count ];
		float field_shift [ field_count ] = { };
		wchar_t profile_name [ 64 ] = { };
		wchar_t rig_entry [ 128 ] = { };
		int32_t action_argument = 0;
		int32_t card_view = 0;
		bool keyboard_on = false;
		control keyboard_id = CONTROL_NONE;
		int32_t keyboard_index = 0;
		double caret_since = 0.0;
		int32_t dragging = -1;
		render::box slider_boxes [ SETTING_COUNT ];
		motion switches [ SETTING_COUNT ];
		bool primed = false;
		render::surface* measurer = nullptr;
		float scroll_goal [ SCROLL_COUNT ] = { };
		motion scroll_motion [ SCROLL_COUNT ];
		float scroll_limit [ SCROLL_COUNT ] = { };
		render::box scroll_area [ SCROLL_COUNT ];
		uint64_t chart_serial = 0;
		double chart_since = -100.0;
		uint64_t chart_end = 0;
		double chart_end_since = -100.0;
		double chart_frame = -100.0;
		int32_t chart_jump = 1;
		render::box chart_area;
		render::box gear_box;
		render::box menu_box;
		bool chart_menu = false;
		bool gear_wanted = false;
		int32_t menu_hints = 0;
		motion gear_fade;
		motion menu_fade;
		motion chart_ceiling;
		float zoom = 1.0f;

	};

}
