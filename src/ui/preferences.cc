#include "ui/shell.hh"
#include "base/math.hh"
#include "base/text.hh"

namespace ui {

	static constexpr float row_height = 48.0f;

	void shell::paint_settings ( render::surface& surface, const snapshot& view, render::box area, double now ) {

		static const wchar_t* const priorities [ ] = { L"Idle", L"Low", L"Normal" };
		static const wchar_t* const kernels [ ] = { L"Auto", L"SHA extensions", L"Portable" };
		static const wchar_t* const themes [ ] = { L"System", L"Dark", L"Light" };

		float offset = scroll_offset ( SCROLL_SETTINGS, now );
		float x = area.x + 28.0f;
		float wide = base::min ( area.w - 56.0f, 720.0f );
		float y = area.y + 6.0f;
		bool has_card = view.adapter_count > 0;
		base::text value;
		base::text tip;

		scroll_area [ SCROLL_SETTINGS ] = area;
		surface.push_shift ( 0.0f, -offset );
		hit_offset = -offset;
		hit_top = area.y;
		hit_bottom = area.bottom ( );

		auto section = [ & ] ( const wchar_t* title ) {

			y += 18.0f;
			caption ( surface, render::box { x, y, wide, 16.0f }, title );
			y += 22.0f;
			surface.rule ( x, x + wide, y - 1.0f, colors.divider );

		};

		auto row = [ & ] ( const wchar_t* label, const wchar_t* text, float control_width, float control_height ) {

			render::box line = { x, y, wide, row_height };

			begin_row ( surface, line, label, text );
			y += row_height;

			return render::box { line.right ( ) - control_width, line.y + ( row_height - control_height ) * 0.5f, control_width, control_height };

		};

		static const wchar_t* const datasets [ ] = { L"Full, 2 GB", L"Light, 256 MB" };
		static const wchar_t* const address_tips [ ] = {

			L"The pool's stratum address as host:port. Run Alloy.exe --test-pool to get a local one on 127.0.0.1:3333.",
			L"The scrypt pool's address as host:port. Alloy.exe --test-pool --algorithm scrypt --port 3334 starts a local one.",
			L"The Monero pool's address as host:port. Alloy.exe --test-pool --algorithm randomx --port 3335 starts a local one.",

		};

		section ( L"POOL" );
		value.add ( L"for " ).add ( chain::algorithm_name ( settings.algorithm ) );
		surface.text ( value.get ( ), render::FONT_SMALL, render::box { x, y - 40.0f, wide, 16.0f }, colors.faint, render::ALIGN_RIGHT );
		field ( surface, row ( L"Pool address", address_tips [ settings.algorithm % chain::ALGORITHM_COUNT ], 300.0f, 30.0f ), SETTING_ADDRESS, false, now );
		field ( surface, row ( L"Worker", settings.algorithm == chain::ALGORITHM_RANDOMX ? L"Monero pools want your wallet address here. Some take a dot and a name for this PC after it." : L"Who the pool credits for your shares. Most pools want your wallet or username, a dot, then a name for this PC.", 300.0f, 30.0f ), SETTING_WORKER, false, now );
		field ( surface, row ( L"Password", L"Most pools ignore this. Leave it as x unless yours says otherwise.", 300.0f, 30.0f ), SETTING_PASSWORD, true, now );
		value.clear ( );

		section ( L"PROCESSOR" );
		toggle ( surface, row ( L"Mine on the processor", L"Hash on the CPU as well. You can switch this while mining.", 36.0f, 20.0f ), SETTING_CPU, settings.cpu, true, now );
		tip.add ( L"This CPU has " ).number ( view.threads ).add ( settings.gpu && has_card ? L" threads. While the graphics card mines, keep one free or the card loses more speed than the thread adds." : L" threads. Use fewer if you want the rest for other work." );
		value.number ( settings.threads );
		stepper ( surface, row ( L"Threads", tip.get ( ), 124.0f, 30.0f ), SETTING_THREADS, value.get ( ), settings.threads > 1, settings.threads < view.threads, now );
		value.clear ( );
		value.number ( settings.cpu_limit ).put ( L'%' );
		slider ( surface, row ( L"Usage limit", L"Threads take short breaks so the CPU averages this much load. Lower runs cooler and quieter.", 300.0f, 30.0f ), SETTING_CPU_LIMIT, static_cast < float > ( settings.cpu_limit - 10 ) / 90.0f, value.get ( ), true, now );
		segmented ( surface, row ( L"Priority", L"Idle only runs when nothing else wants the CPU. Low hardly gets in the way of anything. Normal competes with your apps.", 252.0f, 30.0f ), priorities, 3, settings.priority, CONTROL_CHOICE, SETTING_PRIORITY * 16, true, now );
		segmented ( surface, row ( L"Hash kernel", view.sha ? L"Only matters for SHA-256d. Auto uses the CPU's SHA instructions, about three times faster than the portable code." : L"Only matters for SHA-256d. This CPU has no SHA instructions, so Auto runs the portable code.", 324.0f, 30.0f ), kernels, 3, settings.kernel, CONTROL_CHOICE, SETTING_KERNEL * 16, true, now );
		segmented ( surface, row ( L"RandomX dataset", L"Full hashes about three times faster but keeps 2 GB of memory busy. Light gets by on 256 MB.", 252.0f, 30.0f ), datasets, 2, settings.full_dataset ? 0 : 1, CONTROL_CHOICE, SETTING_DATASET * 16, true, now );

		section ( L"GRAPHICS" );
		toggle ( surface, row ( L"Mine on the graphics card", has_card ? L"Runs Alloy's own shaders for SHA-256d and scrypt. RandomX stays on the CPU. Every hit the card reports gets checked again on the CPU." : L"No Direct3D 11 graphics card was found.", 36.0f, 20.0f ), SETTING_GPU, settings.gpu && has_card, has_card, now );

		if ( view.adapter_count > 1 ) {

			const wchar_t* names [ adapter_room ];

			for ( int32_t i = 0; i < view.adapter_count; i++ )
				names [ i ] = view.adapters [ i ];

			segmented ( surface, row ( L"Adapter", L"The graphics card that runs the shader. Switching cards restarts mining.", 360.0f, 30.0f ), names, base::min ( view.adapter_count, 3 ), static_cast < int32_t > ( settings.adapter ), CONTROL_CHOICE, SETTING_ADAPTER * 16, true, now );

		} else {

			render::box spot = row ( L"Adapter", L"The graphics card that runs the shader.", 360.0f, 30.0f );

			surface.text ( has_card ? view.adapters [ 0 ] : L"None", render::FONT_BODY, spot, colors.muted, render::ALIGN_RIGHT );

		}

		value.clear ( );
		value.number ( settings.intensity );
		tip.clear ( );
		tip.add ( L"A SHA-256d batch is 2^" ).number ( settings.intensity ).add ( L", or " ).compact ( static_cast < double > ( 1u << settings.intensity ) ).add ( L" hashes. Higher is a little faster, lower keeps video smooth. Scrypt batches are sized by memory." );
		slider ( surface, row ( L"Intensity", tip.get ( ), 300.0f, 30.0f ), SETTING_INTENSITY, static_cast < float > ( settings.intensity - 16 ) / 10.0f, value.get ( ), has_card, now );
		value.clear ( );
		value.number ( settings.gpu_limit ).put ( L'%' );
		slider ( surface, row ( L"Usage limit", L"The card pauses between batches to hold roughly this much load.", 300.0f, 30.0f ), SETTING_GPU_LIMIT, static_cast < float > ( settings.gpu_limit - 10 ) / 90.0f, value.get ( ), has_card, now );

		section ( L"WHEN TO MINE" );
		toggle ( surface, row ( L"Pause while I use the computer", L"Mining stops while you type or move the mouse, then picks up again once the PC has sat idle for a while.", 36.0f, 20.0f ), SETTING_PAUSE_ACTIVE, settings.pause_active, true, now );
		value.clear ( );
		value.number ( settings.idle_minutes ).add ( L" min" );

		render::box idle = row ( L"Idle time", L"How long the keyboard and mouse have to sit still before mining starts again.", 124.0f, 30.0f );

		if ( !settings.pause_active )
			surface.push_fade ( 0.45f );

		stepper ( surface, idle, SETTING_IDLE_MINUTES, value.get ( ), settings.pause_active && settings.idle_minutes > 1, settings.pause_active && settings.idle_minutes < 60, now );

		if ( !settings.pause_active )
			surface.pop_fade ( );

		toggle ( surface, row ( L"Pause on battery power", L"Stops mining while a laptop is unplugged.", 36.0f, 20.0f ), SETTING_PAUSE_BATTERY, settings.pause_battery, true, now );
		toggle ( surface, row ( L"Start mining when Alloy opens", L"Starts whichever mode is picked at the top as soon as the window opens.", 36.0f, 20.0f ), SETTING_AUTOSTART, settings.autostart, true, now );

		section ( L"APPEARANCE" );
		segmented ( surface, row ( L"Theme", L"System follows the light or dark setting in Windows.", 252.0f, 30.0f ), themes, 3, settings.theme, CONTROL_CHOICE, SETTING_THEME * 16, true, now );

		section ( L"ABOUT" );
		surface.text ( L"Alloy 0.3", render::FONT_BODY, row ( L"Version", L"SHA-256, scrypt, RandomX, both pool protocols, the shaders and this window are all written from scratch.", 200.0f, 30.0f ), colors.muted, render::ALIGN_RIGHT );

		y += 24.0f;
		scroll_limit [ SCROLL_SETTINGS ] = base::max ( 0.0f, y - area.bottom ( ) );
		hit_offset = 0.0f;
		hit_top = -1e9f;
		hit_bottom = 1e9f;
		surface.pop_shift ( );

	}

}
