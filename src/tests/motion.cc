#ifdef ALLOY_DEBUG

#include "tests/harness.hh"
#include "render/surface.hh"
#include "ui/shell.hh"
#include "pool/presets.hh"
#include "base/math.hh"
#include "base/memory.hh"
#include "base/strings.hh"
#include "base/text.hh"

namespace tests {

	static constexpr int32_t frames = 30;

	struct ink {

		double x = 0.0;
		double y = 0.0;
		double weight = 0.0;

	};

	static ink centroid ( const base::list < uint8_t >& rgba, int32_t wide, int32_t tall ) {

		ink out;

		for ( int32_t y = 0; y < tall; y++ ) {

			for ( int32_t x = 0; x < wide; x++ ) {

				double value = rgba [ ( y * wide + x ) * 4 ];

				out.x += value * x;
				out.y += value * y;
				out.weight += value;

			}

		}

		if ( out.weight > 0.0 ) {

			out.x /= out.weight;
			out.y /= out.weight;

		}

		return out;

	}

	static bool draw_label ( render::surface& surface, float dx, float dy, float fade, base::list < uint8_t >& out ) {

		if ( !surface.begin ( render::rgb ( 0x000000 ) ) )
			return false;

		bool moving = dx != 0.0f || dy != 0.0f || fade < 1.0f;

		if ( moving ) {

			surface.push_shift ( dx, dy );
			surface.push_fade ( fade );

		}

		surface.text ( L"Hashrate 52.41 MH/s", render::FONT_FIGURE, render::box { 20.0f, 18.0f, 300.0f, 40.0f }, render::rgb ( 0xFFFFFF ) );
		surface.text ( L"16 threads, SHA extensions", render::FONT_SMALL, render::box { 20.3f, 52.6f, 300.0f, 18.0f }, render::rgb ( 0xFFFFFF ) );

		if ( moving ) {

			surface.pop_fade ( );
			surface.pop_shift ( );

		}

		return surface.end ( ) && surface.capture ( out );

	}

	static bool same_pixels ( const base::list < uint8_t >& a, const base::list < uint8_t >& b ) {

		return a.size ( ) == b.size ( ) && base::same ( a.data ( ), b.data ( ), static_cast < size_t > ( a.size ( ) ) );

	}

	static void slide_test ( float scale, bool across ) {

		render::surface surface;
		base::list < uint8_t > rest;
		base::list < uint8_t > frame;
		int32_t wide = static_cast < int32_t > ( 360.0f * scale );
		int32_t tall = static_cast < int32_t > ( 84.0f * scale );
		base::text note;

		note.add ( across ? L"sideways" : L"rising" ).add ( L" text at " ).number ( static_cast < uint64_t > ( scale * 100.0f + 0.5f ) ).add ( L"% scale " );

		if ( !surface.open_offscreen ( wide, tall, scale ) || !draw_label ( surface, 0.0f, 0.0f, 1.0f, rest ) ) {

			note.add ( L"couldn't render" );
			check ( false, note.get ( ) );

			return;

		}

		ink settled = centroid ( rest, wide, tall );
		double previous = 0.0;
		double largest_step = 0.0;
		double last_step = 0.0;
		bool monotonic = true;
		bool identical = false;
		bool measured = false;

		for ( int32_t i = 0; i <= frames; i++ ) {

			float t = static_cast < float > ( i ) / frames;
			float eased = base::ease_out ( t );
			float distance = 12.0f * ( 1.0f - eased );

			draw_label ( surface, across ? distance : 0.0f, across ? 0.0f : distance, eased, frame );

			ink seen = centroid ( frame, wide, tall );
			double gap = across ? seen.x - settled.x : seen.y - settled.y;

			if ( seen.weight < settled.weight * 0.05 )
				continue;

			if ( measured ) {

				double step = previous - gap;

				monotonic = monotonic && step >= -0.02;
				largest_step = base::max ( largest_step, step );

				if ( i == frames )
					last_step = step;

			}

			previous = gap;
			measured = true;

			if ( i == frames )
				identical = same_pixels ( frame, rest );

		}

		double expected = 12.0 * 3.0 / frames * scale;

		note.add ( L"lands on the resting pixels, moves steadily (largest step " ).decimal ( largest_step, 2 ).add ( L" px, last " ).decimal ( last_step, 4 ).add ( L" px)" );

		if ( !identical )
			note.add ( L", final frame differs from rest" );

		if ( !monotonic )
			note.add ( L", moved backwards" );
		check ( identical && monotonic && largest_step < expected * 1.3 + ( across ? 1.0 : 0.0 ) && last_step < 0.05, note.get ( ) );

	}

	void motion_tests ( ) {

		section ( L"motion" );

		static const float scales [ ] = { 1.0f, 1.25f, 1.5f };

		for ( float scale : scales ) {

			slide_test ( scale, false );
			slide_test ( scale, true );

		}

		render::surface surface;
		ui::preferences settings;
		ui::snapshot* view = new ui::snapshot;
		base::list < uint8_t > animated;
		base::list < uint8_t > still;
		bool ready = surface.open_offscreen ( 1600, 1000, 1.25f );

		settings.threads = 16;
		base::copy_text ( view->cpu_name, base::count_of ( view->cpu_name ), L"AMD Ryzen 7 5800X" );
		base::copy_text ( view->status, base::count_of ( view->status ), L"Ready" );
		view->threads = 16;
		view->cores = 8;
		view->sha = true;

		ui::shell* moving = new ui::shell ( settings );
		ui::shell* resting = new ui::shell ( settings );
		double start = 50.0;
		bool drawn = ready;
		bool changed_midway = false;

		for ( int32_t p = 1; p < ui::PAGE_COUNT && drawn; p++ ) {

			moving->paint ( surface, *view, start - 1.0 );
			moving->open_page ( static_cast < ui::page > ( p ), start );

			for ( int32_t i = 1; i <= 16 && drawn; i++ ) {

				drawn = moving->paint ( surface, *view, start + 0.02 * i ) && surface.capture ( animated );

				if ( i == 4 ) {

					resting->open_page ( static_cast < ui::page > ( p ), start - 10.0 );
					resting->paint ( surface, *view, start + 0.08 );
					surface.capture ( still );
					changed_midway = changed_midway || !same_pixels ( animated, still );

				}

			}

			drawn = drawn && moving->paint ( surface, *view, start + 1.0 ) && surface.capture ( animated );
			drawn = drawn && resting->paint ( surface, *view, start + 1.0 ) && surface.capture ( still ) && same_pixels ( animated, still );
			start += 10.0;

		}

		check ( drawn && changed_midway, L"switching pages animates and every page settles pixel for pixel on its resting render" );

		ui::shell* hinting = new ui::shell ( settings );
		base::list < uint8_t > before;
		base::list < uint8_t > midway;
		base::list < uint8_t > settled;
		base::list < uint8_t > later;
		double rest = 200.0;

		hinting->open_page ( ui::PAGE_SETTINGS, rest - 10.0 );
		hinting->paint ( surface, *view, rest - 1.0 );
		hinting->mouse_move ( 300.0f, 174.0f, rest );

		bool hinted = hinting->paint ( surface, *view, rest ) && surface.capture ( before );

		hinted = hinted && hinting->paint ( surface, *view, rest + 0.5 );
		hinted = hinted && hinting->paint ( surface, *view, rest + 0.56 ) && surface.capture ( midway );
		hinted = hinted && hinting->paint ( surface, *view, rest + 0.68 ) && surface.capture ( settled );
		hinted = hinted && hinting->paint ( surface, *view, rest + 2.0 ) && surface.capture ( later );
		check ( hinted && !same_pixels ( before, settled ) && !same_pixels ( midway, settled ) && same_pixels ( settled, later ), L"resting the cursor opens a hint that zooms in and then holds still pixel for pixel" );

		ui::shell* editing = new ui::shell ( settings );
		double at = 300.0;
		uint32_t threads = settings.threads;

		auto click = [ & ] ( ui::control id, int32_t index ) {

			render::box spot;

			if ( !editing->locate ( id, index, spot ) )
				return false;

			editing->mouse_move ( spot.middle_x ( ), spot.middle_y ( ), at );
			editing->mouse_down ( spot.middle_x ( ), spot.middle_y ( ), false, at );
			editing->mouse_up ( spot.middle_x ( ), spot.middle_y ( ), at );
			at += 0.5;

			return editing->paint ( surface, *view, at );

		};

		auto type = [ & ] ( const wchar_t* text ) {

			for ( int32_t i = 0; text [ i ]; i++ ) {

				editing->character ( text [ i ], at );
				at += 0.1;

			}

		};

		editing->open_page ( ui::PAGE_SETTINGS, at - 10.0 );
		editing->paint ( surface, *view, at );

		bool stepped = click ( ui::CONTROL_STEP_DOWN, ui::SETTING_THREADS ) && click ( ui::CONTROL_STEP_DOWN, ui::SETTING_THREADS ) && settings.threads == threads - 2;

		editing->key ( 'Z', false, true, at );
		stepped = stepped && settings.threads == threads - 1;
		editing->key ( 'Z', false, true, at );
		stepped = stepped && settings.threads == threads;
		editing->key ( 'Y', false, true, at );
		stepped = stepped && settings.threads == threads - 1;
		check ( stepped, L"Ctrl+Z steps settings back one change at a time and Ctrl+Y brings them forward again" );

		bool typed = click ( ui::CONTROL_FIELD, ui::SETTING_WORKER );

		editing->key ( 0x23, false, false, at );
		type ( L".rig" );
		at += 1.5;
		type ( L"2" );
		editing->key ( 'Z', false, true, at );
		editing->key ( 0x0D, false, false, at );
		typed = typed && base::same_text ( settings.pool ( ).worker, L"alloy.worker.rig" );
		editing->key ( 'Z', false, true, at );
		typed = typed && base::same_text ( settings.pool ( ).worker, L"alloy.worker" );
		check ( typed, L"Ctrl+Z inside a field undoes typing in word-sized steps, and the committed edit can be undone too" );

		base::list < uint8_t > hidden;
		base::list < uint8_t > shown;
		wchar_t copied [ 64 ];
		bool masked = editing->paint ( surface, *view, at ) && surface.capture ( hidden ) && click ( ui::CONTROL_FIELD, ui::SETTING_PASSWORD );

		editing->key ( 'A', false, true, at );
		masked = masked && !editing->copy_selection ( copied, 64, false, at );
		masked = masked && click ( ui::CONTROL_REVEAL, 0 ) && editing->copy_selection ( copied, 64, false, at ) && base::same_text ( copied, L"x" );
		editing->blur ( at );
		masked = masked && editing->paint ( surface, *view, at + 1.0 ) && surface.capture ( shown ) && !same_pixels ( hidden, shown );
		check ( masked, L"the eye button shows the password, and it can only be copied while shown" );

		ui::shell* keyed = new ui::shell ( settings );
		base::list < uint8_t > plain;
		base::list < uint8_t > ringed;
		base::list < uint8_t > cleared;
		double when = 400.0;

		keyed->open_page ( ui::PAGE_OVERVIEW, when - 10.0 );

		bool steered = keyed->paint ( surface, *view, when ) && surface.capture ( plain );

		keyed->key ( 0x09, false, false, when );
		steered = steered && keyed->paint ( surface, *view, when + 0.1 ) && surface.capture ( ringed ) && !same_pixels ( plain, ringed );
		keyed->key ( 0x27, false, false, when + 0.1 );
		steered = steered && keyed->current ( ) == ui::PAGE_ACTIVITY;
		keyed->key ( 0x25, false, false, when + 0.2 );
		steered = steered && keyed->current ( ) == ui::PAGE_OVERVIEW;
		keyed->key ( 0x1B, false, false, when + 0.3 );
		steered = steered && keyed->paint ( surface, *view, when + 5.0 ) && surface.capture ( cleared ) && same_pixels ( plain, cleared );
		check ( steered, L"Tab puts a focus ring on the page list, the arrow keys move between pages, and Escape clears the ring" );

		uint8_t theme = settings.theme;
		int32_t target = ui::SETTING_THEME * 16 + theme;
		render::box spot;
		double tick = 500.0;

		keyed->open_page ( ui::PAGE_SETTINGS, tick - 10.0 );
		keyed->paint ( surface, *view, tick );

		bool below = !keyed->locate ( ui::CONTROL_CHOICE, target, spot );
		bool reached = false;

		for ( int32_t i = 0; i < 200 && !reached; i++ ) {

			keyed->key ( 0x09, false, false, tick );
			tick += 0.5;
			keyed->paint ( surface, *view, tick );
			reached = keyed->keyboard_at ( ui::CONTROL_CHOICE, target );

		}

		bool scrolled = reached && keyed->locate ( ui::CONTROL_CHOICE, target, spot );

		keyed->key ( 0x27, false, false, tick );

		bool turned = settings.theme != theme;

		keyed->key ( 'Z', false, true, tick );
		check ( below && reached && scrolled && turned && settings.theme == theme, L"Tab reaches the theme choice below the fold on Settings and scrolls it into view, Right changes it and Ctrl+Z undoes that" );

		ui::preferences* fresh = new ui::preferences;
		ui::shell* welcome = new ui::shell ( *fresh );
		double moment = 600.0;
		render::box start_button;
		ui::command finished = ui::COMMAND_NONE;

		fresh->cpu = true;
		welcome->open_setup ( moment - 10.0 );
		welcome->paint ( surface, *view, moment );

		bool picked = welcome->locate ( ui::CONTROL_ALGORITHM, chain::ALGORITHM_RANDOMX, start_button );

		if ( picked ) {

			welcome->mouse_down ( start_button.middle_x ( ), start_button.middle_y ( ), false, moment );
			welcome->mouse_up ( start_button.middle_x ( ), start_button.middle_y ( ), moment );
			welcome->paint ( surface, *view, moment + 0.5 );

		}

		picked = picked && fresh->algorithm == chain::ALGORITHM_RANDOMX && welcome->locate ( ui::CONTROL_PRESET, pool::preset_for ( chain::COIN_MONERO ), start_button );

		if ( picked ) {

			welcome->mouse_down ( start_button.middle_x ( ), start_button.middle_y ( ), false, moment + 0.5 );
			welcome->mouse_up ( start_button.middle_x ( ), start_button.middle_y ( ), moment + 0.5 );
			welcome->paint ( surface, *view, moment + 0.8 );

		}

		picked = picked && base::starts_with ( fresh->pools [ chain::ALGORITHM_RANDOMX ].address, L"stratum+ssl://pool.supportxmr.com" );
		welcome->focus_field ( ui::SETTING_WORKER, moment + 1.0 );
		welcome->key ( 'A', false, true, moment + 1.0 );

		for ( const wchar_t* letter = L"44AFFq5kSiGBoZ4NMDwYtN18obc8AemS33DBLWs3H7otXft3XjrpDtQGv7SqSsaBYBb98uNbr2VBBEt7f2wfn3RVGQBEP3A"; *letter; letter++ )
			welcome->character ( *letter, moment + 1.0 );

		welcome->key ( 0x0D, false, false, moment + 1.1 );
		welcome->paint ( surface, *view, moment + 1.5 );

		bool locked = welcome->locate ( ui::CONTROL_ACTION, ui::COMMAND_FINISH_SETUP * 256 + 1, start_button );

		if ( locked ) {

			welcome->mouse_move ( start_button.middle_x ( ), start_button.middle_y ( ), moment + 2.0 );
			welcome->mouse_down ( start_button.middle_x ( ), start_button.middle_y ( ), false, moment + 2.0 );
			finished = welcome->mouse_up ( start_button.middle_x ( ), start_button.middle_y ( ), moment + 2.0 );

		}

		bool named = base::same_text ( fresh->pools [ chain::ALGORITHM_RANDOMX ].worker, L"44AFFq5kSiGBoZ4NMDwYtN18obc8AemS33DBLWs3H7otXft3XjrpDtQGv7SqSsaBYBb98uNbr2VBBEt7f2wfn3RVGQBEP3A" );

		welcome->open_page ( ui::PAGE_OVERVIEW, moment + 3.0 );
		check ( picked && named && locked && finished == ui::COMMAND_FINISH_SETUP && welcome->argument ( ) == 1 && !welcome->in_setup ( ), L"the first-run page picks the coin, takes the wallet and starts mining, then gives way to Overview" );
		delete welcome;
		delete fresh;

		{

			ui::preferences* plain = new ui::preferences;
			ui::shell* charted = new ui::shell ( *plain );
			ui::snapshot* busy = new ui::snapshot;
			base::list < uint8_t > pixels;

			base::copy_text ( busy->cpu_name, base::count_of ( busy->cpu_name ), L"AMD Ryzen 7 5800X" );
			busy->threads = 16;
			busy->cores = 8;
			busy->sha = true;
			busy->running = true;
			busy->rate = 2.3e9;
			busy->history_first = 1000;
			busy->history_count = 900;
			busy->history_serial = 77;

			for ( int32_t i = 0; i < busy->history_count; i++ )
				busy->history [ i ] = 2.2e9f + static_cast < float > ( ( i * 7919 ) % 211 ) * 1.0e6f;

			base::list < uint8_t > hovered;
			base::list < uint8_t > after;
			double clock = 900.0;
			render::box gear;

			auto settle = [ & ] ( base::list < uint8_t >& out ) {

				clock += 2.0;

				return charted->paint ( surface, *busy, clock ) && surface.capture ( out );

			};

			auto press = [ & ] ( float x, float y ) {

				charted->mouse_move ( x, y, clock );
				charted->mouse_down ( x, y, false, clock );

				ui::command result = charted->mouse_up ( x, y, clock );

				clock += 0.5;
				charted->paint ( surface, *busy, clock );

				return result;

			};

			charted->paint ( surface, *busy, clock );

			bool drawn = settle ( pixels ) && charted->locate ( ui::CONTROL_CHART_GEAR, 0, gear );

			charted->mouse_move ( gear.x - 300.0f, gear.y + 120.0f, clock );
			drawn = drawn && settle ( hovered );
			charted->mouse_leave ( clock );
			drawn = drawn && settle ( after );
			check ( drawn && !same_pixels ( pixels, hovered ) && same_pixels ( pixels, after ), L"resting the cursor on the graph fades its options gear in, and leaving puts back the exact default pixels" );

			render::box choice;

			press ( gear.middle_x ( ), gear.middle_y ( ) );

			bool opened = charted->chart_menu_open ( ) && charted->locate ( ui::CONTROL_CHOICE, ui::SETTING_CHART_STYLE * 16 + 1, choice );

			if ( opened )
				press ( choice.middle_x ( ), choice.middle_y ( ) );

			bool lined = plain->chart_style == 1;

			charted->key ( 0x1B, false, false, clock );

			bool closed = !charted->chart_menu_open ( );

			charted->mouse_leave ( clock );
			settle ( hovered );
			check ( opened && lined && closed && !same_pixels ( pixels, hovered ), L"the gear opens the graph options, picking Line redraws the graph, and Escape closes them" );

			press ( gear.middle_x ( ), gear.middle_y ( ) );

			bool reopened = charted->chart_menu_open ( );
			ui::command outside = press ( gear.x - 600.0f, gear.bottom ( ) + 400.0f );

			check ( reopened && !charted->chart_menu_open ( ) && outside == ui::COMMAND_NONE, L"a click outside the graph options closes them without pressing what lies underneath" );

			bool every = true;

			for ( uint8_t style = 0; style < 3 && every; style++ ) {

				for ( uint8_t span = 0; span < 5 && every; span++ ) {

					for ( uint8_t update = 0; update < 4 && every; update++ ) {

						plain->chart_style = style;
						plain->chart_span = span;
						plain->chart_update = update;
						plain->chart_smoothing = static_cast < uint8_t > ( ( style + span ) % 3 );
						plain->chart_clock = static_cast < uint8_t > ( update % 2 );
						every = settle ( after );

					}

				}

			}

			plain->chart_style = 2;
			plain->chart_span = 1;
			plain->chart_update = 0;
			plain->chart_smoothing = 0;
			plain->chart_clock = 0;
			settle ( hovered );
			plain->chart_style = 1;
			settle ( after );

			bool bars = !same_pixels ( hovered, after );

			check ( every && bars, L"every type, span, update rate, smoothing and time label setting draws, and bars differ from a line" );

			{

				base::list < uint8_t > early;
				base::list < uint8_t > later;
				base::list < uint8_t > again;
				double moment = clock + 10.0;

				plain->chart_update = 3;
				busy->live_rate = 2.5e9;
				busy->history_time = moment + 0.9;
				charted->paint ( surface, *busy, moment );
				charted->paint ( surface, *busy, moment + 1.0 );
				surface.capture ( early );

				bool wants = charted->animating ( moment + 1.05 );

				charted->paint ( surface, *busy, moment + 1.3 );
				surface.capture ( later );
				charted->paint ( surface, *busy, moment + 1.3 );
				surface.capture ( again );

				bool gliding = wants && !same_pixels ( early, later ) && same_pixels ( later, again );

				busy->running = false;
				charted->paint ( surface, *busy, moment + 2.0 );
				surface.capture ( early );
				charted->paint ( surface, *busy, moment + 2.6 );
				surface.capture ( later );

				bool still = same_pixels ( early, later ) && !charted->animating ( moment + 4.0 );

				busy->running = true;
				plain->chart_update = 0;
				clock = moment + 10.0;
				check ( gliding && still, L"Continuous keeps the graph gliding between samples, draws the same frame for the same moment, and holds still when mining stops" );

			}

			plain->chart_clock = 1;
			press ( gear.middle_x ( ), gear.middle_y ( ) );

			bool reset = charted->locate ( ui::CONTROL_CHART_RESET, 0, choice );

			if ( reset )
				press ( choice.middle_x ( ), choice.middle_y ( ) );

			charted->key ( 0x1B, false, false, clock );
			charted->mouse_leave ( clock );
			clock += 30.0;
			settle ( after );
			check ( reset && plain->chart_style == 0 && plain->chart_span == 1 && plain->chart_clock == 0 && same_pixels ( pixels, after ), L"Reset in the graph options brings back the standard graph pixel for pixel" );
			delete busy;
			delete charted;
			delete plain;

		}

		delete keyed;
		delete editing;
		delete hinting;
		delete moving;
		delete resting;
		delete view;

	}

}

#endif
