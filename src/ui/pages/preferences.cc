#include "ui/shell.hh"
#include "version.hh"
#include "chain/address.hh"
#include "pool/presets.hh"
#include "randomx/variant.hh"
#include "base/math.hh"
#include "base/text.hh"

namespace ui {

	static constexpr float row_height = 48.0f;

	void shell::paint_settings ( render::surface& surface, const snapshot& view, render::box area, double now ) {

		static const wchar_t* const priorities [ ] = { L"Idle", L"Low", L"Normal" };
		static const wchar_t* const kernels [ ] = { L"Auto", L"SHA extensions", L"Portable" };
		static const wchar_t* const themes [ ] = { L"System", L"Dark", L"Light" };
		static const wchar_t* const datasets [ ] = { L"Full, 2 GB", L"Light, 256 MB" };
		static const wchar_t* const address_tips [ ] = {

			L"The pool's address as host:port. Start it with stratum+ssl:// for an encrypted link. Alloy.exe --test-pool runs a local one.",
			L"The scrypt pool's address as host:port, with stratum+ssl:// in front for an encrypted link.",
			L"The Monero pool's address as host:port, with stratum+ssl:// in front for an encrypted link.",

		};

		float offset = scroll_offset ( SCROLL_SETTINGS, now );
		float x = area.x + 28.0f;
		float wide = base::min ( area.w - 56.0f, 720.0f );
		float y = area.y + 6.0f;
		bool has_card = view.adapter_count > 0;
		uint8_t kind = settings.algorithm % chain::ALGORITHM_COUNT;
		const wchar_t* algorithm_label = chain::algorithm_name ( kind );
		base::text value;
		base::text tip;

		scroll_area [ SCROLL_SETTINGS ] = area;
		surface.push_shift ( 0.0f, -offset );
		hit_offset = -offset;
		hit_top = area.y;
		hit_bottom = area.bottom ( );

		auto section = [ & ] ( const wchar_t* title, bool per_algorithm ) {

			y += 18.0f;
			caption ( surface, render::box { x, y, wide, 16.0f }, title );

			if ( per_algorithm ) {

				base::text note;

				note.add ( L"for " ).add ( algorithm_label );
				surface.text ( note.get ( ), render::FONT_SMALL, render::box { x, y, wide, 16.0f }, colors.faint, render::ALIGN_RIGHT );

			}

			y += 22.0f;
			surface.rule ( x, x + wide, y - 1.0f, colors.divider );

		};

		auto row = [ & ] ( const wchar_t* label, const wchar_t* text, float control_width, float control_height ) {

			render::box line = { x, y, wide, row_height };

			begin_row ( surface, line, label, text );
			y += row_height;

			return render::box { line.right ( ) - control_width, line.y + ( row_height - control_height ) * 0.5f, control_width, control_height };

		};

		section ( L"POOL", true );
		field ( surface, row ( L"Pool address", address_tips [ kind ], 300.0f, 30.0f ), SETTING_ADDRESS, false, now );
		field ( surface, row ( L"Backup pool", L"Alloy moves here after three failed tries on the main pool, and goes back after ten minutes. Leave it empty to keep retrying.", 300.0f, 30.0f ), SETTING_BACKUP, false, now );

		chain::address_check check = chain::check_worker ( kind, fields [ SETTING_WORKER ].text );
		bool flagged = check.verdict == chain::ADDRESS_BROKEN || check.verdict == chain::ADDRESS_OTHER_COIN;

		if ( check.verdict == chain::ADDRESS_BROKEN )
			tip.add ( L"That looks like an address, but its checksum is wrong. Check it for a typo." );
		else if ( check.verdict == chain::ADDRESS_OTHER_COIN )
			tip.add ( L"That's a " ).add ( check.coin ).add ( L" address, which " ).add ( algorithm_label ).add ( L" pools can't pay." );
		else if ( check.verdict == chain::ADDRESS_VALID )
			tip.add ( L"A valid " ).add ( check.coin ).add ( L" address. A dot and a name for this PC can follow it." );
		else
			tip.add ( kind == chain::ALGORITHM_RANDOMX ? L"Monero pools want your wallet address here. Some take a dot and a name for this PC after it." : L"Who the pool credits for your shares: a wallet address or a username, then a dot and a name for this PC." );

		field ( surface, row ( L"Worker", tip.get ( ), 300.0f, 30.0f ), SETTING_WORKER, false, now, flagged );
		field ( surface, row ( L"Password", L"Most pools ignore this. Leave it as x unless yours says otherwise.", 300.0f, 30.0f ), SETTING_PASSWORD, true, now );

		int32_t known = pool::preset_for ( kind );

		if ( known >= 0 ) {

			const pool::preset& entry = pool::preset_at ( known );

			value.add ( L"Use " ).add ( entry.name );

			float width = base::max ( 150.0f, surface.measure ( value.get ( ), render::FONT_STRONG ) + 54.0f );

			button ( surface, row ( L"Known pool", entry.tip, width, 30.0f ), value.get ( ), ICON_ARROW, CONTROL_PRESET, known, false, true, now );
			value.clear ( );

		}

		tip.clear ( );
		section ( L"PROCESSOR", true );
		toggle ( surface, row ( L"Mine on the processor", L"Hash on the CPU as well. You can switch this while mining.", 36.0f, 20.0f ), SETTING_CPU, settings.cpu, true, now );

		if ( kind == chain::ALGORITHM_RANDOMX )
			tip.add ( L"RandomX only runs as many threads as fit in the processor's cache, so extra ones wait." );
		else
			tip.add ( L"This CPU has " ).number ( view.threads ).add ( settings.gpu && has_card ? L" threads. While the card mines, leave one free or the card loses more than the thread adds." : L" threads. Use fewer if you want the rest for other work." );

		value.number ( settings.threads );
		stepper ( surface, row ( L"Threads", tip.get ( ), 124.0f, 30.0f ), SETTING_THREADS, value.get ( ), settings.threads > 1, settings.threads < view.threads, now );
		value.clear ( );
		value.number ( settings.cpu_limit ).put ( L'%' );
		slider ( surface, row ( L"Usage limit", L"Threads take short breaks so the CPU averages this much load. Lower runs cooler and quieter.", 300.0f, 30.0f ), SETTING_CPU_LIMIT, static_cast < float > ( settings.cpu_limit - 10 ) / 90.0f, value.get ( ), true, now );
		segmented ( surface, row ( L"Priority", L"Idle only runs when nothing else wants the CPU. Low hardly gets in the way. Normal competes with your apps.", 252.0f, 30.0f ), priorities, 3, settings.priority, CONTROL_CHOICE, SETTING_PRIORITY * 16, true, now );

		if ( kind == chain::ALGORITHM_SHA256D )
			segmented ( surface, row ( L"Hash kernel", view.sha ? L"Auto uses the CPU's SHA instructions, about three times faster than the portable code." : L"This CPU has no SHA instructions, so Auto runs the portable code.", 324.0f, 30.0f ), kernels, 3, settings.kernel, CONTROL_CHOICE, SETTING_KERNEL * 16, true, now );

		if ( kind == chain::ALGORITHM_RANDOMX ) {

			static const wchar_t* const coins [ randomx::VARIANT_COUNT ] = { L"Monero", L"RX v2", L"Wownero", L"ArQmA", L"Graft", L"Safex", L"Yada", L"Keva", L"Equilibria" };
			const randomx::variant& rules = randomx::variant_of ( settings.variant );

			tip.clear ( );
			tip.add ( rules.name ).add ( L" for " ).add ( rules.coin ).add ( L". Pools that switch coins override this, and Alloy follows them without restarting." );
			segmented ( surface, row ( L"Coin", tip.get ( ), 612.0f, 30.0f ), coins, randomx::VARIANT_COUNT, settings.variant, CONTROL_CHOICE, SETTING_VARIANT * 16, true, now );

		}

		if ( kind == chain::ALGORITHM_RANDOMX )
			segmented ( surface, row ( L"Dataset", L"Full hashes about three times faster but keeps 2 GB of memory busy. Light gets by on 256 MB.", 252.0f, 30.0f ), datasets, 2, settings.full_dataset ? 0 : 1, CONTROL_CHOICE, SETTING_DATASET * 16, true, now );

		toggle ( surface, row ( L"Keep threads on their cores", L"Each thread stays on one core, physical cores first. A little faster for scrypt and RandomX. Applies next start.", 36.0f, 20.0f ), SETTING_PIN, settings.pin_threads, true, now );

		section ( L"GRAPHICS", true );

		bool card_works = has_card && kind != chain::ALGORITHM_RANDOMX;

		toggle ( surface, row ( L"Mine on the graphics card", !has_card ? L"No Direct3D 11 graphics card was found." : ( kind == chain::ALGORITHM_RANDOMX ? L"RandomX was built for processors, so the card stays idle for it." : L"Runs Alloy's own shaders. Every hit the card reports gets checked again on the CPU." ), 36.0f, 20.0f ), SETTING_GPU, settings.gpu && card_works, card_works, now );

		if ( view.adapter_count > 1 ) {

			const wchar_t* names [ adapter_room + 1 ];
			int32_t shown = base::min ( view.adapter_count, 3 );

			for ( int32_t i = 0; i < shown; i++ )
				names [ i ] = view.adapters [ i ];

			names [ shown ] = L"All cards";

			segmented ( surface, row ( L"Adapter", L"The cards that run the shaders. All cards mines on every one at once. Switching restarts mining.", 420.0f, 30.0f ), names, shown + 1, settings.adapter == all_adapters ? shown : static_cast < int32_t > ( settings.adapter ), CONTROL_CHOICE, SETTING_ADAPTER * 16, card_works, now );

		} else {

			render::box spot = row ( L"Adapter", L"The graphics card that runs the shaders.", 360.0f, 30.0f );

			surface.text ( has_card ? view.adapters [ 0 ] : L"None", render::FONT_BODY, spot, colors.muted, render::ALIGN_RIGHT );

		}

		if ( kind == chain::ALGORITHM_SHA256D ) {

			value.clear ( );
			value.number ( settings.intensity );
			tip.clear ( );
			tip.add ( L"Each batch is 2^" ).number ( settings.intensity ).add ( L", or " ).compact ( static_cast < double > ( 1u << settings.intensity ) ).add ( L" hashes. Higher is a little faster, lower keeps video smooth." );
			slider ( surface, row ( L"Intensity", tip.get ( ), 300.0f, 30.0f ), SETTING_INTENSITY, static_cast < float > ( settings.intensity - 16 ) / 10.0f, value.get ( ), card_works, now );

		}

		value.clear ( );
		value.number ( settings.gpu_limit ).put ( L'%' );
		slider ( surface, row ( L"Usage limit", L"The card pauses between batches to hold roughly this much load.", 300.0f, 30.0f ), SETTING_GPU_LIMIT, static_cast < float > ( settings.gpu_limit - 10 ) / 90.0f, value.get ( ), card_works, now );
		value.clear ( );

		if ( settings.gpu_heat )
			value.number ( settings.gpu_heat ).add ( L" \x00B0" ).put ( L'C' );
		else
			value.add ( L"Off" );

		slider ( surface, row ( L"Temperature limit", L"The card rests when it reaches this temperature and carries on once it has cooled four degrees. Off leaves it to the card.", 300.0f, 30.0f ), SETTING_HEAT, settings.gpu_heat ? static_cast < float > ( settings.gpu_heat - 59 ) / 31.0f : 0.0f, value.get ( ), card_works, now );

		section ( L"WHEN TO MINE", false );
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

		section ( L"WINDOWS", false );
		toggle ( surface, row ( L"Open Alloy when I sign in", L"Alloy starts with Windows, in the tray if closing to the tray is on, otherwise minimized.", 36.0f, 20.0f ), SETTING_LOGIN, settings.launch_at_login, true, now );
		toggle ( surface, row ( L"Close to the tray", L"The close button hides the window and mining carries on. Quit from the tray icon's menu.", 36.0f, 20.0f ), SETTING_TRAY, settings.close_to_tray, true, now );
		toggle ( surface, row ( L"Share stats on 127.0.0.1:4067", L"Serves the hashrate and shares as JSON to programs on this PC, like XMRig's API. Nothing can change settings through it.", 36.0f, 20.0f ), SETTING_API, settings.api_port != 0, true, now );

		section ( L"APPEARANCE", false );
		segmented ( surface, row ( L"Theme", L"System follows the light or dark setting in Windows.", 252.0f, 30.0f ), themes, 3, settings.theme, CONTROL_CHOICE, SETTING_THEME * 16, true, now );

		section ( L"ABOUT", false );
		surface.text ( L"Alloy " ALLOY_VERSION_TEXT, render::FONT_BODY, row ( L"Version", L"SHA-256, scrypt, RandomX, the pool protocols, the shaders and this window are all written from scratch.", 200.0f, 30.0f ), colors.muted, render::ALIGN_RIGHT );

		y += 24.0f;
		scroll_limit [ SCROLL_SETTINGS ] = base::max ( 0.0f, y - area.bottom ( ) );
		hit_offset = 0.0f;
		hit_top = -1e9f;
		hit_bottom = 1e9f;
		surface.pop_shift ( );

	}

}
