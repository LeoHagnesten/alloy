#include "ui/shell.hh"
#include "chain/target.hh"
#include "base/bits.hh"
#include "base/math.hh"
#include "base/memory.hh"
#include "base/strings.hh"
#include "base/text.hh"

namespace ui {

	static constexpr int32_t chart_window = 120;
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

		cells [ 1 ].detail.number ( settings.threads ).add ( settings.threads == 1 ? L" thread" : L" threads" );

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

	void shell::paint_chart ( render::surface& surface, const snapshot& view, render::box area, double now ) {

		static constexpr float gutter = 84.0f;
		render::box plot = { area.x, area.y + 30.0f, area.w - gutter, area.h - 30.0f - 20.0f };
		int32_t count = view.history_count;
		int32_t first = base::max ( 0, count - chart_window - 1 );
		float highest = 0.0f;

		caption ( surface, render::box { area.x, area.y, 200.0f, 16.0f }, L"HASHRATE" );
		surface.text ( L"last two minutes", render::FONT_SMALL, render::box { plot.right ( ) - 200.0f, area.y, 200.0f, 16.0f }, colors.faint, render::ALIGN_RIGHT );
		surface.rule ( plot.x, plot.right ( ), plot.bottom ( ), colors.control_line );
		surface.text ( L"2 min ago", render::FONT_SMALL, render::box { plot.x, plot.bottom ( ) + 4.0f, 120.0f, 16.0f }, colors.faint );
		surface.text ( L"1 min ago", render::FONT_SMALL, render::box { plot.middle_x ( ) - 60.0f, plot.bottom ( ) + 4.0f, 120.0f, 16.0f }, colors.faint, render::ALIGN_CENTER );
		surface.text ( L"now", render::FONT_SMALL, render::box { plot.right ( ) - 120.0f, plot.bottom ( ) + 4.0f, 120.0f, 16.0f }, colors.faint, render::ALIGN_RIGHT );

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

		for ( int32_t i = first; i < count; i++ )
			highest = base::max ( highest, view.history [ i ] );

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

		float step = plot.w / static_cast < float > ( chart_window - 1 );
		float t = static_cast < float > ( ( now - chart_since ) / chart_slide );
		float shift = t < 1.0f ? step * ( 1.0f - base::ease_out ( t ) ) : 0.0f;
		float points [ ( chart_window + 3 ) * 2 ];
		int32_t used = 0;

		for ( int32_t i = first; i < count; i++ ) {

			float age = static_cast < float > ( count - 1 - i );
			float value = base::min ( view.history [ i ] / ceiling, 1.0f );

			points [ used * 2 ] = plot.right ( ) - age * step + shift;
			points [ used * 2 + 1 ] = plot.bottom ( ) - value * ( plot.h - 2.0f );
			used++;

		}

		float last_x = points [ ( used - 1 ) * 2 ];
		float last_y = points [ ( used - 1 ) * 2 + 1 ];

		surface.push_clip ( render::box { plot.x, plot.y - 6.0f, plot.w + 6.0f, plot.h + 6.0f } );

		points [ used * 2 ] = last_x;
		points [ used * 2 + 1 ] = plot.bottom ( );
		points [ used * 2 + 2 ] = points [ 0 ];
		points [ used * 2 + 3 ] = plot.bottom ( );
		surface.shaded ( points, used + 2, render::faded ( colors.accent, 0.26f ), render::faded ( colors.accent, 0.0f ), plot.y, plot.bottom ( ) );
		surface.polyline ( points, used, colors.accent, 1.75f );
		surface.dot ( last_x, last_y, 4.5f, colors.content );
		surface.dot ( last_x, last_y, 3.0f, colors.accent );
		surface.pop_clip ( );

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

		add_hint ( render::box { area.x, row, area.w, 70.0f }, L"One bar per thread, scaled to the fastest. Threads left out in Settings stay grey." );
		draw_icon ( surface, ICON_PROCESSOR, area.x, row + 12.0f, 16.0f, settings.cpu ? colors.text : colors.faint );
		surface.text ( view.cpu_name, render::FONT_BODY, render::box { area.x + 28.0f, row + 6.0f, name_width, 20.0f }, settings.cpu ? colors.text : colors.faint );

		uint8_t kind = view.running ? view.algorithm : settings.algorithm;

		if ( settings.cpu ) {

			detail.number ( base::min ( settings.threads, view.threads ) ).add ( L" of " ).number ( view.threads ).add ( L" threads, " );

			if ( kind == chain::ALGORITHM_SCRYPT )
				detail.add ( L"SSE2 scrypt" );
			else if ( kind == chain::ALGORITHM_RANDOMX )
				detail.add ( settings.full_dataset ? L"RandomX full dataset" : L"RandomX light" );
			else
				detail.add ( view.sha && settings.kernel != 2 ? L"SHA extensions" : L"portable kernel" );

			if ( settings.cpu_limit < 100 )
				detail.add ( L", " ).number ( settings.cpu_limit ).add ( L"% limit" );

		} else
			detail.add ( L"Off" );

		surface.text ( detail.get ( ), render::FONT_SMALL, render::box { area.x + 28.0f, row + 26.0f, name_width, 16.0f }, colors.muted );

		if ( settings.cpu && view.running )
			rate.scaled ( view.cpu_rate, L"H/s" );
		else
			rate.add ( L"\x2014" );

		surface.text ( rate.get ( ), render::FONT_STRONG, render::box { rate_right - 130.0f, row + 6.0f, 130.0f, 20.0f }, settings.cpu ? colors.text : colors.faint, render::ALIGN_RIGHT );
		share_bar ( row + 16.0f, view.cpu_rate, settings.cpu && view.running );

		int32_t lanes = base::min ( view.lane_count > 0 ? view.lane_count : static_cast < int32_t > ( view.threads ), lane_room );
		float lane_area = name_width;
		float lane_width = base::clamp ( lane_area / static_cast < float > ( base::max ( lanes, 1 ) ) - 2.0f, 2.0f, 7.0f );
		float fastest = 1.0f;

		for ( int32_t i = 0; i < view.lane_count && i < lane_room; i++ )
			fastest = base::max ( fastest, view.lane_rates [ i ] );

		for ( int32_t i = 0; i < lanes; i++ ) {

			bool working = settings.cpu && view.running && static_cast < uint32_t > ( i ) < view.active_threads;
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

			if ( kind == chain::ALGORITHM_SCRYPT )
				detail.add ( L"scrypt, 128 KB of video memory per hash" );
			else
				detail.add ( L"intensity " ).number ( settings.intensity ).add ( L", " ).compact ( static_cast < double > ( 1u << settings.intensity ) ).add ( L" hashes per dispatch" );

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
			property ( surface, x, y, wide, L"Algorithm", chain::algorithm_name ( view.running ? view.algorithm : settings.algorithm ) );
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
				value.grouped ( view.height );
				property ( surface, x, y, wide, L"Height", value.get ( ) );
				hash_property ( surface, x, y, wide, L"Seed hash", view.seed );
				value.clear ( );
				value.number ( static_cast < uint64_t > ( view.blob_size ) ).add ( L" bytes, nonce at byte 39" );
				property ( surface, x, y, wide, L"Blob", value.get ( ) );
				value.clear ( );
				value.add ( dataset_text ( view.dataset ) );

				if ( view.dataset == DATASET_BUILDING )
					value.add ( L", " ).number ( view.dataset_progress / 10 ).put ( L'%' );

				property ( surface, x, y, wide, L"Dataset", value.get ( ) );
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

			} else if ( kind == chain::ALGORITHM_RANDOMX ) {

				value.add ( dataset_text ( view.dataset ) );

				if ( view.dataset == DATASET_BUILDING )
					value.add ( L", " ).number ( view.dataset_progress / 10 ).put ( L'%' );

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
