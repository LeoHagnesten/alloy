#include "ui/shell.hh"
#include "chain/target.hh"
#include "randomx/variant.hh"
#include "base/bits.hh"
#include "base/math.hh"
#include "base/memory.hh"
#include "base/strings.hh"
#include "base/text.hh"

namespace ui {

	static constexpr int32_t chart_window = 120;
	static constexpr int32_t chart_room = 260;
	static constexpr float chart_slide = 0.45f;

	struct metric {

		const wchar_t* caption = L"";
		const wchar_t* tip = L"";
		base::writer < wchar_t, 64 > value;
		base::writer < wchar_t, 96 > detail;
		bool quiet = false;

	};

	static const wchar_t* link_text ( uint8_t phase ) {

		static const wchar_t* const names [ ] = { L"Offline", L"Connecting", L"Signing in", L"Connected", L"Reconnecting", L"Refused" };

		return names [ phase < 6 ? phase : 0 ];

	}

	static void variant_label ( base::text& out, uint8_t kind ) {

		const randomx::variant& rules = randomx::variant_of ( kind );
		wchar_t key [ 16 ];

		base::widen ( key, base::count_of ( key ), rules.key );
		out.add ( rules.name ).add ( L", " ).add ( key );

	}

	static const wchar_t* dataset_text ( uint8_t phase ) {

		static const wchar_t* const names [ ] = { L"Not built yet", L"Building", L"Full, 2 GB", L"Light, 256 MB", L"Light, short on memory" };

		return names [ phase < 5 ? phase : 0 ];

	}

	void shell::paint_overview ( render::surface& surface, const snapshot& view, render::box area, double now ) {

		float x = area.x + 28.0f;
		float wide = area.w - 56.0f;
		render::box metrics = { x, area.y, wide, 92.0f };
		float rest = area.bottom ( ) - metrics.bottom ( );
		float chart_height = base::max ( 150.0f, base::min ( rest - 22.0f - 26.0f - 16.0f - 286.0f, rest * 0.55f ) );
		render::box chart = { x, metrics.bottom ( ) + 22.0f, wide, chart_height };
		render::box lower = { x, chart.bottom ( ) + 26.0f, wide, area.bottom ( ) - chart.bottom ( ) - 26.0f - 16.0f };
		float split = base::floor ( lower.w * 0.57f );

		paint_metrics ( surface, view, metrics );
		paint_chart ( surface, view, chart, now );
		paint_chart_gear ( surface, chart, now );
		surface.rule ( area.x, area.right ( ), chart.bottom ( ) + 12.0f, colors.divider );
		paint_devices ( surface, view, render::box { lower.x, lower.y, split - 28.0f, lower.h } );
		surface.column ( lower.x + split, lower.y - 13.0f, area.bottom ( ), colors.divider );
		paint_inspector ( surface, view, render::box { lower.x + split + 28.0f, lower.y, lower.w - split - 28.0f, lower.h } );

	}

	void shell::paint_metrics ( render::surface& surface, const snapshot& view, render::box area ) {

		metric cells [ 6 ];
		bool started = view.running || view.hashes > 0;
		uint8_t kind = view.running ? view.algorithm : settings.algorithm;
		bool gpu_on = settings.gpu && view.adapter_count > 0 && kind != chain::ALGORITHM_RANDOMX;

		cells [ 0 ].caption = L"HASHRATE";
		cells [ 1 ].caption = L"PROCESSOR";
		cells [ 2 ].caption = L"GRAPHICS";
		cells [ 0 ].tip = L"Hashes per second across every device, averaged over the last five seconds.";
		cells [ 1 ].tip = L"How fast the CPU threads are hashing right now.";
		cells [ 2 ].tip = L"How fast the graphics card is hashing right now.";
		cells [ 5 ].tip = L"How long this run has been going, and how many hashes it has tried.";

		if ( view.running )
			cells [ 0 ].value.scaled ( view.rate, L"H/s" );
		else
			cells [ 0 ].value.add ( L"\x2014" );

		if ( started )
			cells [ 0 ].detail.add ( L"average " ).scaled ( view.average, L"H/s" );
		else
			cells [ 0 ].detail.add ( L"not mining" );

		if ( !settings.cpu ) {

			cells [ 1 ].value.add ( L"Off" );
			cells [ 1 ].quiet = true;

		} else if ( view.running )
			cells [ 1 ].value.scaled ( view.cpu_rate, L"H/s" );
		else
			cells [ 1 ].value.add ( L"\x2014" );

		uint32_t working = view.running && view.active_threads ? view.active_threads : settings.threads;

		cells [ 1 ].detail.number ( working ).add ( working == 1 ? L" thread" : L" threads" );

		if ( settings.cpu_limit < 100 )
			cells [ 1 ].detail.add ( L" at " ).number ( settings.cpu_limit ).put ( L'%' );

		if ( kind == chain::ALGORITHM_RANDOMX && view.adapter_count ) {

			cells [ 2 ].value.add ( L"Idle" );
			cells [ 2 ].quiet = true;
			cells [ 2 ].detail.add ( L"RandomX is CPU only" );

		} else if ( !gpu_on ) {

			cells [ 2 ].value.add ( L"Off" );
			cells [ 2 ].quiet = true;
			cells [ 2 ].detail.add ( view.adapter_count ? L"enable it in Settings" : L"no adapter" );

		} else if ( view.running && view.gpu_state == GPU_BROKEN ) {

			cells [ 2 ].value.add ( L"Failed" );
			cells [ 2 ].detail.add ( view.gpu_name );

		} else {

			if ( view.running && view.gpu_state == GPU_WORKING )
				cells [ 2 ].value.scaled ( view.gpu_rate, L"H/s" );
			else
				cells [ 2 ].value.add ( view.running ? L"Starting" : L"\x2014" );

			if ( kind == chain::ALGORITHM_SCRYPT )
				cells [ 2 ].detail.add ( L"scrypt kernels" );
			else
				cells [ 2 ].detail.add ( L"intensity " ).number ( settings.intensity );

			if ( settings.gpu_limit < 100 )
				cells [ 2 ].detail.add ( L" at " ).number ( settings.gpu_limit ).put ( L'%' );

		}

		if ( view.mode == MODE_POOL ) {

			cells [ 3 ].caption = L"SHARES";
			cells [ 4 ].caption = L"DIFFICULTY";
			cells [ 5 ].caption = L"UPTIME";
			cells [ 3 ].tip = L"Shares the pool accepted this run. Rejected ones are listed under Activity.";
			cells [ 4 ].tip = L"The pool's share difficulty. A hash has to beat it to count as a share.";
			cells [ 3 ].value.number ( view.accepted );
			cells [ 3 ].detail.number ( view.rejected ).add ( L" rejected" );

			if ( view.stale )
				cells [ 3 ].detail.add ( L", " ).number ( view.stale ).add ( L" stale" );

			if ( view.difficulty > 0.0 )
				cells [ 4 ].value.compact ( view.difficulty );
			else
				cells [ 4 ].value.add ( L"\x2014" );

			cells [ 4 ].detail.add ( L"best share " );

			if ( view.best > 0.0 )
				cells [ 4 ].detail.compact ( view.best );
			else
				cells [ 4 ].detail.add ( L"\x2014" );

		} else if ( view.mode == MODE_BENCHMARK ) {

			cells [ 3 ].caption = L"PEAK";
			cells [ 4 ].caption = L"BEST SHARE";
			cells [ 5 ].caption = L"ELAPSED";
			cells [ 3 ].tip = L"The fastest single second of this run.";
			cells [ 4 ].tip = L"The hardest hash found so far, written as a difficulty.";

			if ( view.peak > 0.0 )
				cells [ 3 ].value.scaled ( view.peak, L"H/s" );
			else
				cells [ 3 ].value.add ( L"\x2014" );

			cells [ 3 ].detail.add ( L"highest second" );

			if ( view.best > 0.0 )
				cells [ 4 ].value.compact ( view.best );
			else
				cells [ 4 ].value.add ( L"\x2014" );

			cells [ 4 ].detail.add ( L"best hash so far" );

		} else {

			cells [ 3 ].caption = L"PROGRESS";
			cells [ 4 ].caption = L"REMAINING";
			cells [ 5 ].caption = L"ELAPSED";
			cells [ 3 ].tip = L"How much of the 4.29 billion possible nonces has been checked.";
			cells [ 4 ].tip = L"Roughly how long the rest would take at the current speed.";
			cells [ 3 ].value.decimal ( view.progress * 100.0, 1 ).put ( L'%' );
			cells [ 3 ].detail.decimal ( ( static_cast < double > ( view.from ) + view.hashes ) / 1e9, 2 ).add ( L" of 4.29 G nonces" );

			if ( view.solved )
				cells [ 4 ].value.add ( L"Found" );
			else if ( view.running && view.rate > 0.0 )
				cells [ 4 ].value.duration ( view.remaining );
			else
				cells [ 4 ].value.add ( L"\x2014" );

			cells [ 4 ].detail.add ( view.solved ? L"same nonce as block 0" : L"at the current rate" );

		}

		if ( started )
			cells [ 5 ].value.duration ( view.uptime );
		else
			cells [ 5 ].value.add ( L"\x2014" );

		cells [ 5 ].detail.compact ( static_cast < double > ( view.hashes ) ).add ( L" hashes" );

		float part = area.w / 6.0f;

		for ( int32_t i = 0; i < 6; i++ ) {

			float left = area.x + part * static_cast < float > ( i ) + ( i ? 20.0f : 0.0f );
			float inner = part - ( i ? 20.0f : 0.0f ) - 12.0f;

			if ( i )
				surface.column ( area.x + part * static_cast < float > ( i ), area.y + 20.0f, area.y + 76.0f, colors.divider );

			add_hint ( render::box { left, area.y + 14.0f, inner, 70.0f }, cells [ i ].tip );
			caption ( surface, render::box { left, area.y + 18.0f, inner, 16.0f }, cells [ i ].caption );
			surface.text ( cells [ i ].value.get ( ), render::FONT_FIGURE, render::box { left, area.y + 36.0f, inner, 30.0f }, cells [ i ].quiet ? colors.faint : colors.text );
			surface.text ( cells [ i ].detail.get ( ), render::FONT_SMALL, render::box { left, area.y + 66.0f, inner, 16.0f }, colors.muted );

		}

		surface.rule ( area.x - 28.0f, area.right ( ) + 28.0f, area.bottom ( ) - 1.0f, colors.divider );

	}

	static void clock_label ( base::text& out, int32_t back, bool seconds ) {

		base::wall_time now = base::local_time ( );
		int32_t moment = ( static_cast < int32_t > ( now.hour ) * 3600 + now.minute * 60 + now.second - back + 86400 * 2 ) % 86400;

		out.clear ( );
		out.padded ( static_cast < uint32_t > ( moment / 3600 ), 2 ).put ( L':' ).padded ( static_cast < uint32_t > ( moment / 60 % 60 ), 2 );

		if ( seconds )
			out.put ( L':' ).padded ( static_cast < uint32_t > ( moment % 60 ), 2 );

	}

	void shell::paint_chart ( render::surface& surface, const snapshot& view, render::box area, double now ) {

		static constexpr float gutter = 84.0f;
		static const wchar_t* const spans [ 5 ] = { L"last minute", L"last two minutes", L"last ten minutes", L"last half hour", L"last hour" };
		static const wchar_t* const marks [ 5 ] [ 2 ] = { { L"1 min ago", L"30 s ago" }, { L"2 min ago", L"1 min ago" }, { L"10 min ago", L"5 min ago" }, { L"30 min ago", L"15 min ago" }, { L"1 hour ago", L"30 min ago" } };
		static constexpr int32_t windows [ 5 ] = { 60, chart_window, 600, 1800, 3600 };
		static constexpr int32_t sizes [ 5 ] = { 1, 1, 3, 10, 15 };
		static constexpr int32_t smoothings [ 3 ] = { 1, 5, 30 };
		static constexpr int32_t updates [ 3 ] = { 1, 5, 15 };
		uint8_t span = settings.chart_span % 5;
		int32_t window = windows [ span ];
		int32_t size = sizes [ span ];
		int32_t smooth = smoothings [ settings.chart_smoothing % 3 ];
		bool flow = settings.chart_update == 3;
		int32_t every = flow ? 1 : updates [ settings.chart_update % 3 ];
		uint8_t style = settings.chart_style % 3;
		render::box plot = { area.x, area.y + 30.0f, area.w - gutter, area.h - 30.0f - 20.0f };
		int32_t count = view.history_count;
		float highest = 0.0f;

		if ( every > 1 && count > every )
			count -= static_cast < int32_t > ( ( view.history_first + static_cast < uint64_t > ( count ) ) % static_cast < uint64_t > ( every ) );

		int32_t first = base::max ( 0, count - window - 1 );

		caption ( surface, render::box { area.x, area.y, 200.0f, 16.0f }, L"HASHRATE" );
		surface.text ( spans [ span ], render::FONT_SMALL, render::box { plot.right ( ) - 200.0f, area.y, 200.0f, 16.0f }, colors.faint, render::ALIGN_RIGHT );
		surface.rule ( plot.x, plot.right ( ), plot.bottom ( ), colors.control_line );

		if ( settings.chart_clock ) {

			base::text label;
			bool seconds = window <= 600;

			clock_label ( label, window, seconds );
			surface.text ( label.get ( ), render::FONT_SMALL, render::box { plot.x, plot.bottom ( ) + 4.0f, 120.0f, 16.0f }, colors.faint );
			clock_label ( label, window / 2, seconds );
			surface.text ( label.get ( ), render::FONT_SMALL, render::box { plot.middle_x ( ) - 60.0f, plot.bottom ( ) + 4.0f, 120.0f, 16.0f }, colors.faint, render::ALIGN_CENTER );
			clock_label ( label, 0, seconds );
			surface.text ( label.get ( ), render::FONT_SMALL, render::box { plot.right ( ) - 120.0f, plot.bottom ( ) + 4.0f, 120.0f, 16.0f }, colors.faint, render::ALIGN_RIGHT );

		} else {

			surface.text ( marks [ span ] [ 0 ], render::FONT_SMALL, render::box { plot.x, plot.bottom ( ) + 4.0f, 120.0f, 16.0f }, colors.faint );
			surface.text ( marks [ span ] [ 1 ], render::FONT_SMALL, render::box { plot.middle_x ( ) - 60.0f, plot.bottom ( ) + 4.0f, 120.0f, 16.0f }, colors.faint, render::ALIGN_CENTER );
			surface.text ( L"now", render::FONT_SMALL, render::box { plot.right ( ) - 120.0f, plot.bottom ( ) + 4.0f, 120.0f, 16.0f }, colors.faint, render::ALIGN_RIGHT );

		}

		if ( count < 2 ) {

			const wchar_t* message = L"Start mining to plot the hashrate";

			if ( view.running )
				message = L"Measuring the first seconds";
			else if ( view.hashes )
				message = L"The last run ended before a full second of samples";

			surface.text ( message, render::FONT_BODY, plot, colors.faint, render::ALIGN_CENTER );
			chart_ceiling.jump ( 0.0f );

			return;

		}

		float values [ chart_room ];
		int32_t ages [ chart_room ];
		int32_t used = 0;
		int32_t warm = base::max ( 0, first - smooth + 1 );
		double running = 0.0;
		uint64_t current = ~0ull;

		for ( int32_t i = warm; i < count; i++ ) {

			running += view.history [ i ];

			if ( i - smooth >= warm )
				running -= view.history [ i - smooth ];

			if ( i < first )
				continue;

			float value = smooth == 1 ? view.history [ i ] : static_cast < float > ( running / base::min ( i - warm + 1, smooth ) );
			uint64_t bucket = ( view.history_first + static_cast < uint64_t > ( i ) ) / static_cast < uint64_t > ( size );

			if ( bucket != current && used < chart_room ) {

				current = bucket;
				values [ used ] = 0.0f;
				ages [ used ] = 0;
				used++;

			}

			int32_t slot = used - 1;
			int32_t filled = static_cast < int32_t > ( ( view.history_first + static_cast < uint64_t > ( i ) ) % static_cast < uint64_t > ( size ) );

			values [ slot ] += ( value - values [ slot ] ) / static_cast < float > ( filled + 1 );
			ages [ slot ] = count - 1 - i;

		}

		bool live = flow && view.running && !view.paused;
		bool head = live && style != 2;
		float since = live ? static_cast < float > ( base::clamp ( now - view.history_time, 0.0, 1.0 ) ) : 0.0f;

		for ( int32_t i = 0; i < used; i++ )
			highest = base::max ( highest, values [ i ] );

		if ( head )
			highest = base::max ( highest, static_cast < float > ( view.live_rate ) );

		float target = static_cast < float > ( base::nice_ceiling ( static_cast < double > ( highest ) * 1.12 ) );

		chart_ceiling.duration = 0.4f;

		if ( chart_ceiling.to <= 0.0f )
			chart_ceiling.jump ( target );
		else
			chart_ceiling.go ( target, now );

		float ceiling = base::max ( chart_ceiling.at ( now ), 1.0f );

		for ( int32_t level = 1; level <= 4; level++ ) {

			float y = plot.bottom ( ) - plot.h * static_cast < float > ( level ) / 4.0f;
			base::text label;

			surface.rule ( plot.x, plot.right ( ), y, colors.divider );
			label.scaled ( static_cast < double > ( target ) * level / 4.0, L"H/s" );
			surface.text ( label.get ( ), render::FONT_SMALL, render::box { plot.right ( ) + 10.0f, y - 9.0f, gutter - 10.0f, 18.0f }, colors.faint, render::ALIGN_RIGHT );

		}

		float step = plot.w / static_cast < float > ( window - 1 );
		float shift = 0.0f;

		if ( flow ) {

			shift = -since * step;
			chart_frame = now;

		} else if ( every == 1 ) {

			float t = static_cast < float > ( ( now - chart_since ) / chart_slide );

			shift = t < 1.0f ? step * ( 1.0f - base::ease_out ( t ) ) : 0.0f;

		} else {

			uint64_t end = view.history_first + static_cast < uint64_t > ( count );

			if ( end != chart_end ) {

				chart_jump = chart_end && end > chart_end ? static_cast < int32_t > ( base::min ( end - chart_end, static_cast < uint64_t > ( every ) ) ) : 0;
				chart_end = end;
				chart_end_since = now;

			}

			float t = static_cast < float > ( ( now - chart_end_since ) / chart_slide );

			shift = t < 1.0f ? step * static_cast < float > ( chart_jump ) * ( 1.0f - base::ease_out ( t ) ) : 0.0f;

		}

		float points [ ( chart_room + 4 ) * 2 ];

		for ( int32_t i = 0; i < used; i++ ) {

			float age = static_cast < float > ( ages [ i ] );
			float value = base::min ( values [ i ] / ceiling, 1.0f );

			points [ i * 2 ] = plot.right ( ) - age * step + shift;
			points [ i * 2 + 1 ] = plot.bottom ( ) - value * ( plot.h - 2.0f );

		}

		int32_t drawn = used;

		if ( head ) {

			points [ used * 2 ] = plot.right ( );
			points [ used * 2 + 1 ] = plot.bottom ( ) - base::min ( static_cast < float > ( view.live_rate ) / ceiling, 1.0f ) * ( plot.h - 2.0f );
			drawn = used + 1;

		}

		float last_x = points [ ( drawn - 1 ) * 2 ];
		float last_y = points [ ( drawn - 1 ) * 2 + 1 ];

		surface.push_clip ( render::box { plot.x, plot.y - 6.0f, plot.w + 6.0f, plot.h + 6.0f } );

		if ( style == 2 ) {

			float bar = base::max ( step * static_cast < float > ( size ) * 0.62f, 1.5f );

			for ( int32_t i = 0; i < used; i++ ) {

				float top = points [ i * 2 + 1 ];

				surface.fill ( render::box { points [ i * 2 ] - bar * 0.5f, top, bar, plot.bottom ( ) - top }, i == used - 1 ? colors.accent : render::faded ( colors.accent, 0.6f ) );

			}

		} else {

			if ( style == 0 ) {

				points [ drawn * 2 ] = last_x;
				points [ drawn * 2 + 1 ] = plot.bottom ( );
				points [ drawn * 2 + 2 ] = points [ 0 ];
				points [ drawn * 2 + 3 ] = plot.bottom ( );
				surface.shaded ( points, drawn + 2, render::faded ( colors.accent, 0.26f ), render::faded ( colors.accent, 0.0f ), plot.y, plot.bottom ( ) );

			}

			surface.polyline ( points, drawn, colors.accent, 1.75f );
			surface.dot ( last_x, last_y, 4.5f, colors.content );
			surface.dot ( last_x, last_y, 3.0f, colors.accent );

		}

		surface.pop_clip ( );

	}

	void shell::sync_gear ( double now ) {

		bool wanted = shown_page == PAGE_OVERVIEW && !setup_open && ( chart_area.contains ( mouse_x, mouse_y ) || chart_menu || keyboard_at ( CONTROL_CHART_GEAR, 0 ) );

		if ( wanted == gear_wanted )
			return;

		gear_wanted = wanted;
		gear_fade.duration = wanted ? 0.16f : 0.24f;
		gear_fade.go ( wanted ? 1.0f : 0.0f, now );

	}

	void shell::close_chart_menu ( double now ) {

		if ( !chart_menu )
			return;

		chart_menu = false;
		menu_fade.duration = 0.1f;
		menu_fade.go ( 0.0f, now );
		sync_gear ( now );

	}

	void shell::paint_chart_gear ( render::surface& surface, render::box area, double now ) {

		chart_area = render::box { area.x, area.y - 8.0f, area.w, area.h + 8.0f };
		gear_box = render::box { area.right ( ) - 24.0f, area.y - 5.0f, 24.0f, 24.0f };
		sync_gear ( now );
		add_hit ( gear_box, CONTROL_CHART_GEAR, 0 );
		add_hint ( gear_box, L"Graph options: the type, how far back it reaches, how often it moves, smoothing and the time labels." );

		float shown = gear_fade.at ( now );

		if ( shown <= 0.0f )
			return;

		float level = chart_menu ? 1.0f : glow ( CONTROL_CHART_GEAR, 0, now );

		surface.push_fade ( shown );

		if ( level > 0.0f )
			surface.fill ( gear_box, render::faded ( colors.control_hover, level ), corner );

		draw_icon ( surface, ICON_GEAR, gear_box.x + 4.0f, gear_box.y + 4.0f, 16.0f, render::blend ( colors.muted, colors.text, level ) );
		surface.pop_fade ( );

	}

	void shell::paint_chart_menu ( render::surface& surface, double now ) {

		static const wchar_t* const styles [ ] = { L"Area", L"Line", L"Bars" };
		static const wchar_t* const spans [ ] = { L"1 min", L"2 min", L"10 min", L"30 min", L"1 hour" };
		static const wchar_t* const updates [ ] = { L"Continuous", L"1 s", L"5 s", L"15 s" };
		static const int32_t update_places [ 4 ] = { 1, 2, 3, 0 };
		static const wchar_t* const smoothings [ ] = { L"Off", L"5 s", L"30 s" };
		static const wchar_t* const times [ ] = { L"Ago", L"Clock" };
		static constexpr float wide = 440.0f;
		static constexpr float line = 42.0f;
		float shown = menu_fade.at ( now );

		if ( shown <= 0.0f || shown_page != PAGE_OVERVIEW || setup_open )
			return;

		float tall = 16.0f + 24.0f + line * 5.0f + 14.0f + 30.0f + 16.0f;
		float top = base::min ( chart_area.bottom ( ) + 2.0f, height - status_height - 8.0f - tall );
		float saved_top = hit_top;
		float saved_bottom = hit_bottom;

		menu_box = render::box { base::max ( gear_box.right ( ) - wide, sidebar_width + 8.0f ), base::max ( top, title_height + 8.0f ), wide, tall };

		if ( !chart_menu ) {

			hit_top = 1e9f;
			hit_bottom = -1e9f;

		}

		surface.push_fade ( shown );
		surface.fill ( render::box { menu_box.x - 1.0f, menu_box.y + 1.0f, menu_box.w + 2.0f, menu_box.h + 3.0f }, render::rgb ( 0x000000, 0.10f ), corner + 3.0f );
		surface.fill ( render::box { menu_box.x, menu_box.y + 1.0f, menu_box.w, menu_box.h + 1.0f }, render::rgb ( 0x000000, 0.12f ), corner + 2.0f );
		surface.fill ( menu_box, colors.tip, corner + 2.0f );
		surface.outline ( menu_box, colors.control_line, 1.0f / zoom, corner + 2.0f );
		add_hit ( menu_box, CONTROL_CHART_MENU, 0 );
		menu_hints = hint_count;

		float x = menu_box.x + 16.0f;
		float inner = wide - 32.0f;
		float y = menu_box.y + 16.0f;

		caption ( surface, render::box { x, y, inner, 16.0f }, L"GRAPH" );
		y += 24.0f;

		auto row = [ & ] ( const wchar_t* label, const wchar_t* tip, const wchar_t* const* labels, int32_t count, int32_t selected, setting which, float control, bool last ) {

			render::box place = { x, y, inner, line };

			surface.text ( label, render::FONT_BODY, render::box { x, y, 120.0f, line }, colors.text );
			add_hint ( place, tip );
			segmented ( surface, render::box { x + inner - control, y + 6.0f, control, 30.0f }, labels, count, selected, CONTROL_CHOICE, which * 16, true, now );
			y += line;

			if ( !last )
				surface.rule ( x, x + inner, y - 0.5f, colors.divider );

		};

		row ( L"Type", L"Area fills under the line, Line draws only the line, and Bars draws one bar for each point.", styles, 3, settings.chart_style % 3, SETTING_CHART_STYLE, 204.0f, false );
		row ( L"Span", L"How far back the graph reaches. Alloy keeps an hour of samples either way.", spans, 5, settings.chart_span % 5, SETTING_CHART_SPAN, 268.0f, false );
		row ( L"Update", L"How often the graph moves on. Continuous glides along and follows the hashrate between seconds, redrawing up to 60 times a second while mining. A slower graph is calmer to watch.", updates, 4, update_places [ settings.chart_update % 4 ], SETTING_CHART_UPDATE, 300.0f, false );
		row ( L"Smoothing", L"Averages each point over the seconds before it, which hides the second to second jitter.", smoothings, 3, settings.chart_smoothing % 3, SETTING_CHART_SMOOTHING, 204.0f, false );
		row ( L"Time labels", L"Ago counts back from now. Clock shows the time of day under the graph.", times, 2, settings.chart_clock % 2, SETTING_CHART_TIME, 204.0f, true );

		bool standard = settings.chart_style == 0 && settings.chart_span == 1 && settings.chart_update == 0 && settings.chart_smoothing == 0 && settings.chart_clock == 0;

		y += 14.0f;
		surface.text ( L"Saved with your settings", render::FONT_SMALL, render::box { x, y, inner - 170.0f, 30.0f }, colors.faint );
		button ( surface, render::box { x + inner - 160.0f, y, 160.0f, 30.0f }, L"Reset to default", ICON_ARROW, CONTROL_CHART_RESET, 0, false, !standard, now );
		surface.pop_fade ( );
		hit_top = saved_top;
		hit_bottom = saved_bottom;

	}
	void shell::paint_devices ( render::surface& surface, const snapshot& view, render::box area ) {

		float rate_right = area.right ( ) - 112.0f;
		float name_width = rate_right - area.x - 130.0f;
		double total = view.cpu_rate + view.gpu_rate;
		float y = area.y;
		bool gpu_on = settings.gpu && view.adapter_count > 0;

		caption ( surface, render::box { area.x, y, 200.0f, 16.0f }, L"DEVICES" );
		y += 26.0f;
		surface.text ( L"Device", render::FONT_SMALL, render::box { area.x, y, 200.0f, 18.0f }, colors.faint );
		surface.text ( L"Hashrate", render::FONT_SMALL, render::box { rate_right - 110.0f, y, 110.0f, 18.0f }, colors.faint, render::ALIGN_RIGHT );
		surface.text ( L"Share", render::FONT_SMALL, render::box { area.right ( ) - 88.0f, y, 88.0f, 18.0f }, colors.faint );
		y += 22.0f;
		surface.rule ( area.x, area.right ( ), y, colors.divider );

		auto share_bar = [ & ] ( float top, double rate, bool on ) {

			render::box track = { area.right ( ) - 88.0f, top - 2.0f, 50.0f, 4.0f };
			double fraction = total > 0.0 && on ? rate / total : 0.0;
			base::text label;

			surface.fill ( track, colors.control_line, 2.0f );
			surface.fill ( render::box { track.x, track.y, track.w * static_cast < float > ( fraction ), 4.0f }, colors.accent, 2.0f );
			label.number ( static_cast < uint64_t > ( fraction * 100.0 + 0.5 ) ).put ( L'%' );
			surface.text ( label.get ( ), render::FONT_SMALL, render::box { track.right ( ) + 6.0f, top - 9.0f, 34.0f, 18.0f }, on ? colors.muted : colors.faint, render::ALIGN_RIGHT );

		};

		base::text detail;
		base::text rate;
		float row = y + 1.0f;

		bool randomx = ( view.running ? view.algorithm : settings.algorithm ) == chain::ALGORITHM_RANDOMX;

		add_hint ( render::box { area.x, row, area.w, 70.0f }, randomx ? L"RandomX keeps a 2 MB scratchpad per thread in the processor's cache, so Alloy only runs as many threads as fit there." : L"One bar per thread, scaled to the fastest. Threads left out in Settings stay grey." );
		uint8_t kind = view.running ? view.algorithm : settings.algorithm;
		bool cpu_on = settings.cpu && kind != chain::ALGORITHM_ETHASH;

		draw_icon ( surface, ICON_PROCESSOR, area.x, row + 12.0f, 16.0f, cpu_on ? colors.text : colors.faint );
		surface.text ( view.cpu_name, render::FONT_BODY, render::box { area.x + 28.0f, row + 6.0f, name_width, 20.0f }, cpu_on ? colors.text : colors.faint );

		if ( cpu_on ) {

			detail.number ( view.running ? view.active_threads : base::min ( settings.threads, view.threads ) ).add ( L" of " ).number ( view.threads ).add ( L" threads, " );

			if ( kind == chain::ALGORITHM_SCRYPT )
				detail.add ( L"SSE2 scrypt" );
			else if ( kind == chain::ALGORITHM_RANDOMX && settings.dataset_megabytes >= dataset_full )
				detail.add ( L"RandomX full dataset" );
			else if ( kind == chain::ALGORITHM_RANDOMX && settings.dataset_megabytes )
				detail.add ( L"RandomX, " ).number ( settings.dataset_megabytes ).add ( L" MB of the dataset" );
			else if ( kind == chain::ALGORITHM_RANDOMX )
				detail.add ( L"RandomX light" );
			else if ( kind == chain::ALGORITHM_KHEAVYHASH )
				detail.add ( L"Keccak and a 64 by 64 matrix" );
			else
				detail.add ( view.sha && settings.kernel != 2 ? L"SHA extensions" : L"portable kernel" );

			if ( settings.cpu_limit < 100 )
				detail.add ( L", " ).number ( settings.cpu_limit ).add ( L"% limit" );

		} else
			detail.add ( settings.cpu && kind == chain::ALGORITHM_ETHASH ? L"Not used, Ethash runs on the graphics card" : L"Off" );

		surface.text ( detail.get ( ), render::FONT_SMALL, render::box { area.x + 28.0f, row + 26.0f, name_width, 16.0f }, colors.muted );

		if ( cpu_on && view.running )
			rate.scaled ( view.cpu_rate, L"H/s" );
		else
			rate.add ( L"\x2014" );

		surface.text ( rate.get ( ), render::FONT_STRONG, render::box { rate_right - 130.0f, row + 6.0f, 130.0f, 20.0f }, cpu_on ? colors.text : colors.faint, render::ALIGN_RIGHT );
		share_bar ( row + 16.0f, view.cpu_rate, cpu_on && view.running );

		int32_t lanes = base::min ( view.lane_count > 0 ? view.lane_count : static_cast < int32_t > ( view.threads ), lane_room );
		float lane_area = name_width;
		float lane_width = base::clamp ( lane_area / static_cast < float > ( base::max ( lanes, 1 ) ) - 2.0f, 2.0f, 7.0f );
		float fastest = 1.0f;

		for ( int32_t i = 0; i < view.lane_count && i < lane_room; i++ )
			fastest = base::max ( fastest, view.lane_rates [ i ] );

		for ( int32_t i = 0; i < lanes; i++ ) {

			bool working = cpu_on && view.running && static_cast < uint32_t > ( i ) < view.active_threads;
			float fraction = working && i < view.lane_count ? base::clamp ( view.lane_rates [ i ] / fastest, 0.08f, 1.0f ) : 0.0f;
			float left = area.x + 28.0f + static_cast < float > ( i ) * ( lane_width + 2.0f );
			float bottom = row + 62.0f;

			surface.fill ( render::box { left, bottom - 12.0f, lane_width, 12.0f }, colors.control, 1.0f );

			if ( fraction > 0.0f )
				surface.fill ( render::box { left, bottom - 12.0f * fraction, lane_width, 12.0f * fraction }, render::faded ( colors.accent, 0.85f ), 1.0f );

		}

		row += 72.0f;
		surface.rule ( area.x, area.right ( ), row, colors.divider );
		row += 1.0f;

		const wchar_t* gpu_label = view.gpu_name [ 0 ] && view.gpu_state != GPU_BROKEN ? view.gpu_name : ( view.adapter_count ? view.adapters [ base::min ( static_cast < int32_t > ( settings.adapter ), view.adapter_count - 1 ) ] : L"No graphics adapter" );

		detail.clear ( );
		rate.clear ( );

		if ( kind == chain::ALGORITHM_RANDOMX && view.adapter_count ) {

			detail.add ( L"Not used, RandomX runs on the processor" );
			gpu_on = false;

		} else if ( !gpu_on )
			detail.add ( view.adapter_count ? L"Off" : L"Direct3D 11 compute isn't available" );
		else if ( view.running && view.gpu_state == GPU_BROKEN )
			detail.add ( view.gpu_name );
		else {

			uint32_t batch = kind == chain::ALGORITHM_KHEAVYHASH || kind == chain::ALGORITHM_ETHASH ? settings.intensity - 2 : settings.intensity;

			if ( view.running && view.gpu_state == GPU_BUILDING )
				detail.add ( L"Building the " ).decimal ( static_cast < double > ( view.dag_bytes ) / 1073741824.0, 2 ).add ( L" GB DAG, " ).number ( view.dag_progress / 10 ).put ( L'%' );
			else if ( kind == chain::ALGORITHM_SCRYPT )
				detail.add ( L"scrypt, 64 KB of video memory per hash" );
			else
				detail.add ( L"intensity " ).number ( settings.intensity ).add ( L", " ).compact ( static_cast < double > ( 1u << batch ) ).add ( L" hashes per dispatch" );

			if ( settings.gpu_limit < 100 )
				detail.add ( L", " ).number ( settings.gpu_limit ).add ( L"% limit" );

		}

		if ( gpu_on && view.running && view.gpu_state == GPU_WORKING )
			rate.scaled ( view.gpu_rate, L"H/s" );
		else
			rate.add ( L"\x2014" );

		add_hint ( render::box { area.x, row, area.w, 52.0f }, L"Batch size and the usage limit come from Settings. The card's results are checked on the CPU before they count." );
		draw_icon ( surface, ICON_GRAPHICS, area.x, row + 12.0f, 16.0f, gpu_on ? colors.text : colors.faint );
		surface.text ( gpu_label, render::FONT_BODY, render::box { area.x + 28.0f, row + 6.0f, name_width, 20.0f }, gpu_on ? colors.text : colors.faint );
		surface.text ( detail.get ( ), render::FONT_SMALL, render::box { area.x + 28.0f, row + 26.0f, name_width, 16.0f }, view.running && view.gpu_state == GPU_BROKEN ? colors.bad : colors.muted );
		surface.text ( rate.get ( ), render::FONT_STRONG, render::box { rate_right - 130.0f, row + 6.0f, 130.0f, 20.0f }, gpu_on ? colors.text : colors.faint, render::ALIGN_RIGHT );
		share_bar ( row + 16.0f, view.gpu_rate, gpu_on && view.running );

	}

	void shell::paint_inspector ( render::surface& surface, const snapshot& view, render::box area ) {

		float x = area.x;
		float wide = area.w;
		float y = area.y;
		base::text value;

		surface.push_clip ( render::box { area.x, area.y - 4.0f, area.w, area.h + 4.0f } );

		if ( view.mode == MODE_POOL ) {

			caption ( surface, render::box { x, y, wide, 16.0f }, L"POOL" );
			y += 24.0f;
			property ( surface, x, y, wide, L"Address", settings.pool ( ).address );
			property ( surface, x, y, wide, L"Worker", settings.pool ( ).worker );
			value.add ( view.running ? link_text ( view.link ) : L"Offline" );

			if ( view.running && view.link == PHASE_READY && view.latency > 0.0 )
				value.add ( L", " ).decimal ( view.latency, view.latency < 10.0 ? 1 : 0 ).add ( L" ms" );

			property ( surface, x, y, wide, L"Connection", value.get ( ) );
			value.clear ( );

			if ( view.difficulty > 0.0 )
				value.compact ( view.difficulty );
			else
				value.add ( L"\x2014" );

			property ( surface, x, y, wide, L"Share difficulty", value.get ( ) );
			y += 14.0f;
			caption ( surface, render::box { x, y, wide, 16.0f }, L"CURRENT JOB" );
			y += 24.0f;

			if ( !view.has_job || !view.running ) {

				surface.text ( view.running ? L"Waiting for the pool's first job" : L"Start mining to receive work", render::FONT_BODY, render::box { x, y, wide, 26.0f }, colors.faint );
				surface.pop_clip ( );

				return;

			}

			property ( surface, x, y, wide, L"Job", view.job, render::FONT_MONO );

			if ( view.algorithm == chain::ALGORITHM_RANDOMX ) {

				value.clear ( );
				variant_label ( value, view.variant );
				property ( surface, x, y, wide, L"Algorithm", value.get ( ) );
				value.clear ( );
				value.grouped ( view.height );
				property ( surface, x, y, wide, L"Height", value.get ( ) );
				hash_property ( surface, x, y, wide, L"Seed hash", view.seed );
				value.clear ( );
				value.add ( dataset_text ( view.dataset ) );

				if ( view.dataset == DATASET_BUILDING )
					value.add ( L", " ).number ( view.dataset_progress / 10 ).put ( L'%' );

				property ( surface, x, y, wide, L"Dataset", value.get ( ) );
				surface.pop_clip ( );

				return;

			}

			if ( view.algorithm == chain::ALGORITHM_ETHASH ) {

				hash_property ( surface, x, y, wide, L"Header hash", view.previous );
				hash_property ( surface, x, y, wide, L"Seed hash", view.seed );
				value.clear ( );

				if ( view.dag_bytes ) {

					value.add ( L"epoch " ).number ( view.dag_epoch ).add ( L", " ).decimal ( static_cast < double > ( view.dag_bytes ) / 1073741824.0, 2 ).add ( L" GB" );

					if ( view.gpu_state == GPU_BUILDING )
						value.add ( L", building " ).number ( view.dag_progress / 10 ).put ( L'%' );

				} else
					value.add ( L"\x2014" );

				property ( surface, x, y, wide, L"DAG", value.get ( ) );
				surface.pop_clip ( );

				return;

			}
			uint8_t previous [ 32 ];

			base::copy ( previous, view.previous, 32 );
			base::reverse_words ( previous, 32 );
			hash_property ( surface, x, y, wide, L"Previous block", previous );
			value.clear ( );
			value.compact ( chain::difficulty_of_bits ( view.bits ) );
			property ( surface, x, y, wide, L"Network difficulty", value.get ( ) );
			value.clear ( );
			value.number ( static_cast < uint64_t > ( view.branches ) ).add ( L" branches, coinbase " ).number ( static_cast < uint64_t > ( view.coinbase_size ) ).add ( L" bytes" );
			property ( surface, x, y, wide, L"Merkle", value.get ( ) );

		} else if ( view.mode == MODE_BENCHMARK ) {

			uint8_t kind = view.running ? view.algorithm : settings.algorithm;

			caption ( surface, render::box { x, y, wide, 16.0f }, L"BENCHMARK" );
			y += 24.0f;
			property ( surface, x, y, wide, L"Algorithm", chain::algorithm_name ( kind ) );

			if ( kind == chain::ALGORITHM_SCRYPT ) {

				property ( surface, x, y, wide, L"Header", L"Litecoin genesis, rolling time" );
				property ( surface, x, y, wide, L"Processor kernel", L"SSE2 Salsa20/8, 128 KB a thread" );
				property ( surface, x, y, wide, L"Graphics kernel", L"HLSL compute, 64 per group" );

			} else if ( kind == chain::ALGORITHM_KHEAVYHASH ) {

				property ( surface, x, y, wide, L"Work", L"Fixed pre-PoW hash, nonce counts up" );
				property ( surface, x, y, wide, L"Processor kernel", L"cSHAKE256 and a 64 by 64 matrix" );
				property ( surface, x, y, wide, L"Graphics kernel", L"HLSL compute, 256 per group" );

			} else if ( kind == chain::ALGORITHM_ETHASH ) {

				property ( surface, x, y, wide, L"Work", L"Fixed header hash, Ethereum Classic sized DAG" );
				property ( surface, x, y, wide, L"Processor kernel", L"Not used" );
				property ( surface, x, y, wide, L"Graphics kernel", L"HLSL compute, 128 per group" );

			} else if ( kind == chain::ALGORITHM_RANDOMX ) {

				value.add ( dataset_text ( view.dataset ) );

				if ( view.dataset == DATASET_BUILDING )
					value.add ( L", " ).number ( view.dataset_progress / 10 ).put ( L'%' );

				base::text named;

				variant_label ( named, view.variant );
				property ( surface, x, y, wide, L"Variant", named.get ( ) );
				property ( surface, x, y, wide, L"Input", L"Monero block blob, 76 bytes" );
				property ( surface, x, y, wide, L"Processor kernel", view.aes ? L"Interpreter with AES-NI" : L"Interpreter, software AES" );
				property ( surface, x, y, wide, L"Dataset", value.get ( ) );
				value.clear ( );

			} else {

				property ( surface, x, y, wide, L"Header", L"Genesis block, rolling time" );
				property ( surface, x, y, wide, L"Processor kernel", view.sha && settings.kernel != 2 ? L"SHA extensions" : L"Portable" );
				property ( surface, x, y, wide, L"Graphics kernel", L"HLSL compute, 256 per group" );

			}

			value.grouped ( view.hashes );
			property ( surface, x, y, wide, L"Hashes", value.get ( ) );
			value.clear ( );

			if ( view.best > 0.0 )
				value.compact ( view.best );
			else
				value.add ( L"\x2014" );

			property ( surface, x, y, wide, L"Best share", value.get ( ) );

		} else {

			caption ( surface, render::box { x, y, wide, 16.0f }, L"GENESIS BLOCK" );
			y += 24.0f;
			property ( surface, x, y, wide, L"Mined", L"January 3, 2009" );
			property ( surface, x, y, wide, L"Target bits", L"1d00ffff", render::FONT_MONO );
			property ( surface, x, y, wide, L"Real nonce", L"2,083,236,893" );
			value.grouped ( view.from );
			property ( surface, x, y, wide, L"Search starts at", value.get ( ) );

			render::box track = { x, y + 10.0f, wide, 4.0f };

			surface.fill ( track, colors.control_line, 2.0f );
			surface.fill ( render::box { track.x, track.y, track.w * static_cast < float > ( view.progress ), 4.0f }, colors.accent, 2.0f );
			y += 30.0f;
			caption ( surface, render::box { x, y, wide, 16.0f }, L"RESULT" );
			y += 24.0f;

			if ( view.solved ) {

				value.clear ( );
				value.grouped ( view.nonce );
				property ( surface, x, y, wide, L"Nonce", value.get ( ) );
				hash_property ( surface, x, y, wide, L"Block hash", view.block_hash );
				property ( surface, x, y, wide, L"Check", view.nonce == 2083236893u ? L"Matches block 0" : L"Differs from block 0" );

			} else
				surface.text ( view.running ? L"Searching the nonce space" : L"Start to search for the nonce", render::FONT_BODY, render::box { x, y, wide, 26.0f }, colors.faint );

		}

		surface.pop_clip ( );

	}

}
