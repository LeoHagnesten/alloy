#include "ui/shell.hh"
#include "version.hh"
#include "chain/address.hh"
#include "pool/presets.hh"
#include "randomx/variant.hh"
#include "base/math.hh"
#include "base/strings.hh"
#include "base/text.hh"

namespace ui {

	static constexpr float row_height = 48.0f;

	void shell::paint_settings ( render::surface& surface, const snapshot& view, render::box area, double now ) {

		static const wchar_t* const priorities [ ] = { L"Idle", L"Low", L"Normal" };
		static const wchar_t* const kernels [ ] = { L"Auto", L"SHA extensions", L"Portable" };
		static const wchar_t* const themes [ ] = { L"System", L"Dark", L"Light" };
		static const wchar_t* const datasets [ ] = { L"Full, 2 GB", L"Light, 256 MB" };
		static const wchar_t* const address_tips [ ] = {

			L"The pool's address as host:port. Start it with stratum+ssl:// for an encrypted link, or stratum2+tcp://host:port/ and the pool's authority key for Stratum V2. Your own node's RPC address, like http://127.0.0.1:8332, mines solo. Alloy.exe --test-pool runs a local pool.",
			L"The scrypt pool's address as host:port, with stratum+ssl:// in front for an encrypted link.",
			L"The Monero pool's address as host:port, with stratum+ssl:// in front for an encrypted link. To mine solo, put your monerod's address, like http://127.0.0.1:18081.",
			L"The Kaspa pool's address as host:port. Kaspa pools speak the Stratum bridge dialect, which Alloy follows.",

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
		hit_scroller = SCROLL_SETTINGS;

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

		section ( L"PROFILES", false );

		if ( view.profile_count ) {

			render::box saved = row ( L"Switch to", L"A profile holds every setting on this page, pools and wallets included. Switching to one applies it at once and keeps mining if Alloy was mining.", wide * 0.5f, 30.0f );
			float right = saved.right ( );

			for ( int32_t i = view.profile_count - 1; i >= 0; i-- ) {

				float width = surface.measure ( view.profiles [ i ], render::FONT_STRONG ) + 32.0f;

				if ( right - width < saved.x )
					break;

				button ( surface, render::box { right - width, saved.y, width, saved.h }, view.profiles [ i ], ICON_ARROW, CONTROL_ACTION, COMMAND_APPLY_PROFILE * 256 + i, false, true, now );
				right -= width + 8.0f;

			}

		}

		{

			render::box naming = row ( L"Save these settings as", L"Type a name like Overnight or Quiet and save. Saving under an existing name replaces it, and Delete removes the profile with the name typed here.", 300.0f + 8.0f + 84.0f + 8.0f + 84.0f, 30.0f );
			int32_t matching = -1;

			for ( int32_t i = 0; i < view.profile_count; i++ ) {

				if ( base::same_folded ( view.profiles [ i ], fields [ SETTING_PROFILE_NAME ].text ) )
					matching = i;

			}

			field ( surface, render::box { naming.x, naming.y, 300.0f, naming.h }, SETTING_PROFILE_NAME, false, now );
			button ( surface, render::box { naming.x + 308.0f, naming.y, 84.0f, naming.h }, L"Save", ICON_SAVE, CONTROL_ACTION, COMMAND_SAVE_PROFILE * 256, false, fields [ SETTING_PROFILE_NAME ].length > 0, now );
			button ( surface, render::box { naming.x + 400.0f, naming.y, 84.0f, naming.h }, L"Delete", ICON_CLEAR, CONTROL_ACTION, COMMAND_DELETE_PROFILE * 256 + base::max ( matching, 0 ), false, matching >= 0, now );

		}

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

		int32_t matches [ 4 ];
		int32_t known = 0;
		float widths [ 4 ] = { };
		float total_width = 0.0f;

		for ( int32_t i = 0; i < pool::preset_count ( ) && known < 4; i++ ) {

			if ( pool::preset_at ( i ).algorithm != kind )
				continue;

			widths [ known ] = surface.measure ( pool::preset_at ( i ).name, render::FONT_STRONG ) + 54.0f;
			total_width += widths [ known ] + ( known ? 8.0f : 0.0f );
			matches [ known++ ] = i;

		}

		if ( known ) {

			tip.clear ( );

			for ( int32_t i = 0; i < known; i++ )
				tip.add ( i ? L" " : L"" ).add ( pool::preset_at ( matches [ i ] ).name ).add ( L": " ).add ( pool::preset_at ( matches [ i ] ).tip );

			render::box spot = row ( known > 1 ? L"Known pools" : L"Known pool", tip.get ( ), total_width, 30.0f );
			float left = spot.x;

			for ( int32_t i = 0; i < known; i++ ) {

				button ( surface, render::box { left, spot.y, widths [ i ], spot.h }, pool::preset_at ( matches [ i ] ).name, ICON_ARROW, CONTROL_PRESET, matches [ i ], false, true, now );
				left += widths [ i ] + 8.0f;

			}

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

		if ( kind == chain::ALGORITHM_RANDOMX ) {

			render::box pages = row ( L"Large pages", view.pages_granted ? L"Windows lets Alloy lock large pages, so the dataset reads stop missing the TLB and RandomX runs faster." : L"Large pages usually make RandomX faster. Windows asks once for permission, then you sign out and back in for it to apply.", 150.0f, 30.0f );

			if ( view.pages_granted )
				surface.text ( view.large_pages ? L"In use" : L"Allowed", render::FONT_BODY, pages, colors.muted, render::ALIGN_RIGHT );
			else
				button ( surface, pages, L"Allow", ICON_ARROW, CONTROL_ACTION, COMMAND_LARGE_PAGES * 256, false, true, now );

		}

		if ( kind == chain::ALGORITHM_RANDOMX )
			segmented ( surface, row ( L"Dataset", L"Full hashes about three times faster but keeps 2 GB of memory busy. Light gets by on 256 MB.", 252.0f, 30.0f ), datasets, 2, settings.full_dataset ? 0 : 1, CONTROL_CHOICE, SETTING_DATASET * 16, true, now );

		{

			render::box tuned = row ( L"Find the fastest settings", L"Benchmarks a few thread counts and, for SHA-256d, graphics card batch sizes, then keeps the fastest. Mining stops while it runs, which takes about a minute.", 200.0f, 30.0f );

			if ( view.tuning ) {

				value.clear ( );
				value.add ( L"Measuring, step " ).number ( view.tuning_steps + 1 );
				surface.text ( value.get ( ), render::FONT_BODY, render::box { tuned.x - 220.0f, tuned.y, 210.0f, tuned.h }, colors.muted, render::ALIGN_RIGHT );
				button ( surface, render::box { tuned.right ( ) - 96.0f, tuned.y, 96.0f, tuned.h }, L"Stop", ICON_STOP, CONTROL_ACTION, COMMAND_TUNE * 256, false, true, now );

			} else
				button ( surface, render::box { tuned.right ( ) - 96.0f, tuned.y, 96.0f, tuned.h }, L"Tune", ICON_ARROW, CONTROL_ACTION, COMMAND_TUNE * 256, false, true, now );

		}

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

		if ( settings.adapter == all_adapters && view.adapter_count > 1 ) {

			const wchar_t* names [ adapter_room ];
			int32_t shown = base::min ( view.adapter_count, 4 );

			for ( int32_t i = 0; i < shown; i++ )
				names [ i ] = view.adapters [ i ];

			card_view = base::min ( card_view, shown - 1 );

			const card_tuning& card = settings.cards [ card_view ];
			uint32_t intensity = card.custom ? card.intensity : settings.intensity;
			uint32_t limit = card.custom ? card.limit : settings.gpu_limit;
			uint32_t heat = card.custom ? card.heat : settings.gpu_heat;

			segmented ( surface, row ( L"Card", L"Pick a card to give it its own settings. Cards you leave alone follow the shared settings below.", 420.0f, 30.0f ), names, shown, card_view, CONTROL_CHOICE, SETTING_CARD * 16, card_works, now );
			toggle ( surface, row ( L"Mine on this card", L"Turn one card off and the others keep mining.", 36.0f, 20.0f ), SETTING_CARD_ON, !card.custom || card.on, card_works, now );

			if ( kind == chain::ALGORITHM_SHA256D || kind == chain::ALGORITHM_KHEAVYHASH ) {

				value.clear ( );
				value.number ( intensity );
				slider ( surface, row ( L"This card's intensity", L"A smaller card often does best a step or two lower than a big one.", 300.0f, 30.0f ), SETTING_CARD_INTENSITY, static_cast < float > ( intensity - 16 ) / 10.0f, value.get ( ), card_works, now );

			}

			value.clear ( );
			value.number ( limit ).put ( L'%' );
			slider ( surface, row ( L"This card's usage limit", L"How hard this card works, on its own.", 300.0f, 30.0f ), SETTING_CARD_LIMIT, static_cast < float > ( limit - 10 ) / 90.0f, value.get ( ), card_works, now );
			value.clear ( );

			if ( heat )
				value.number ( heat ).add ( L" \x00B0" ).put ( L'C' );
			else
				value.add ( L"Off" );

			slider ( surface, row ( L"This card's temperature limit", L"Rests this card alone when it gets this hot.", 300.0f, 30.0f ), SETTING_CARD_HEAT, heat ? static_cast < float > ( heat - 59 ) / 31.0f : 0.0f, value.get ( ), card_works, now );

			if ( card.custom )
				button ( surface, row ( L"Own settings", L"This card has its own settings. Reset it to follow the shared ones again.", 180.0f, 30.0f ), L"Use shared", ICON_ARROW, CONTROL_CARD_RESET, 0, false, true, now );

		}

		if ( kind == chain::ALGORITHM_SHA256D || kind == chain::ALGORITHM_KHEAVYHASH ) {

			uint32_t batch = kind == chain::ALGORITHM_KHEAVYHASH ? settings.intensity - 2 : settings.intensity;

			value.clear ( );
			value.number ( settings.intensity );
			tip.clear ( );
			tip.add ( L"Each batch is 2^" ).number ( batch ).add ( L", or " ).compact ( static_cast < double > ( 1u << batch ) ).add ( L" hashes. Higher is a little faster, lower keeps video smooth." );
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
		toggle ( surface, row ( L"Pause for full-screen games and video", L"Stops while a game, a film or a presentation fills the screen, so nothing stutters.", 36.0f, 20.0f ), SETTING_FULLSCREEN, settings.pause_fullscreen, true, now );
		field ( surface, row ( L"Pause while these run", L"Program names separated by commas, like obs64, Photoshop. Alloy checks every two seconds and leaves the .exe optional.", 300.0f, 30.0f ), SETTING_APPS, false, now );
		toggle ( surface, row ( L"Mine on a schedule", L"Only mine during the hours and days below, for example overnight when power is cheaper.", 36.0f, 20.0f ), SETTING_SCHEDULE, settings.schedule, true, now );

		{

			static const wchar_t* const days [ 7 ] = { L"Mon", L"Tue", L"Wed", L"Thu", L"Fri", L"Sat", L"Sun" };
			render::box hours = row ( L"Hours", L"Mining runs from the first hour up to the second. When the second is earlier, the night carries on into the next morning.", 256.0f, 30.0f );
			base::text from;
			base::text to;

			from.padded ( settings.schedule_from, 2 ).add ( L":00" );
			to.padded ( settings.schedule_to, 2 ).add ( L":00" );

			if ( !settings.schedule )
				surface.push_fade ( 0.45f );

			stepper ( surface, render::box { hours.x, hours.y, 124.0f, hours.h }, SETTING_SCHEDULE_FROM, from.get ( ), settings.schedule, settings.schedule, now );
			surface.text ( L"to", render::FONT_SMALL, render::box { hours.x + 124.0f, hours.y, hours.w - 248.0f, hours.h }, colors.faint, render::ALIGN_CENTER );
			stepper ( surface, render::box { hours.right ( ) - 124.0f, hours.y, 124.0f, hours.h }, SETTING_SCHEDULE_TO, to.get ( ), settings.schedule, settings.schedule, now );

			if ( !settings.schedule )
				surface.pop_fade ( );

			chips ( surface, row ( L"Days", L"The days the schedule mines on. A night that starts on a chosen day runs into the next morning.", 336.0f, 28.0f ), days, 7, settings.schedule_days, SETTING_SCHEDULE_DAYS, settings.schedule, now );

		}

		toggle ( surface, row ( L"Start mining when Alloy opens", L"Starts whichever mode is picked at the top as soon as the window opens.", 36.0f, 20.0f ), SETTING_AUTOSTART, settings.autostart, true, now );

		section ( L"WINDOWS", false );
		toggle ( surface, row ( L"Open Alloy when I sign in", L"Alloy starts with Windows, in the tray if closing to the tray is on, otherwise minimized.", 36.0f, 20.0f ), SETTING_LOGIN, settings.launch_at_login, true, now );
		toggle ( surface, row ( L"Close to the tray", L"The close button hides the window and mining carries on. Quit from the tray icon's menu.", 36.0f, 20.0f ), SETTING_TRAY, settings.close_to_tray, true, now );
		toggle ( surface, row ( L"Notifications", L"A Windows notification when Alloy falls back to the backup pool, a card hits its temperature limit, shares keep getting rejected or a block is found.", 36.0f, 20.0f ), SETTING_NOTIFY, settings.notify, true, now );
		toggle ( surface, row ( L"Pause shortcut, Ctrl+Alt+M", L"Pauses and resumes mining from anywhere, even with the window closed to the tray.", 36.0f, 20.0f ), SETTING_HOTKEY, settings.hotkey, true, now );
		section ( L"EARNINGS", false );
		toggle ( surface, row ( L"Look up balance and network stats", L"Reads your pending balance from SupportXMR or CKPool and Monero's difficulty from SupportXMR's public stats. Only your wallet address is sent, to the pool you already mine on.", 36.0f, 20.0f ), SETTING_LOOKUPS, settings.lookups, true, now );
		toggle ( surface, row ( L"Show the value in money", L"Fetches prices from CoinGecko every ten minutes. Nothing about you is sent.", 36.0f, 20.0f ), SETTING_FIAT, settings.show_fiat, settings.lookups, now );

		{

			static const wchar_t* const currencies [ ] = { L"USD", L"EUR", L"GBP", L"SEK" };

			segmented ( surface, row ( L"Currency", L"The currency prices and earnings are shown in.", 252.0f, 30.0f ), currencies, 4, settings.currency, CONTROL_CHOICE, SETTING_CURRENCY * 16, settings.lookups && settings.show_fiat, now );

		}

		section ( L"REMOTE", false );
		toggle ( surface, row ( L"Stats API and web dashboard", L"Serves the hashrate and shares as JSON on port 4067, like XMRig's API, plus a dashboard page for any browser. Nothing can change settings through it.", 36.0f, 20.0f ), SETTING_API, settings.api_port != 0, true, now );
		toggle ( surface, row ( L"Let other computers connect", L"Listens on the network as well, so other rigs and phones can watch this one. They need the access token below.", 36.0f, 20.0f ), SETTING_API_LAN, settings.api_lan, settings.api_port != 0, now );
		toggle ( surface, row ( L"Allow pause and resume", L"Lets the dashboard and scripts pause, resume, start and stop mining with POST /1/pause and friends. Always needs the token.", 36.0f, 20.0f ), SETTING_API_CONTROL, settings.api_control, settings.api_port != 0, now );

		if ( settings.api_token [ 0 ] ) {

			render::box access = row ( L"Access token", L"Send it as Authorization: Bearer, or open the dashboard with #token= after the address. A new token locks out everything that used the old one.", 360.0f, 30.0f );
			wchar_t shown [ 40 ];

			base::widen ( shown, base::count_of ( shown ), settings.api_token );
			shown [ 8 ] = L'\x2026';
			shown [ 9 ] = 0;
			surface.text ( shown, render::FONT_MONO, render::box { access.x, access.y, 110.0f, access.h }, colors.muted );
			button ( surface, render::box { access.x + 116.0f, access.y, 74.0f, access.h }, L"Copy", ICON_ARROW, CONTROL_ACTION, COMMAND_COPY_TOKEN * 256, false, true, now );
			button ( surface, render::box { access.x + 196.0f, access.y, 74.0f, access.h }, L"New", ICON_ARROW, CONTROL_NEW_TOKEN, 0, false, true, now );
			button ( surface, render::box { access.right ( ) - 84.0f, access.y, 84.0f, access.h }, L"Open", ICON_ARROW, CONTROL_ACTION, COMMAND_OPEN_DASHBOARD * 256, false, settings.api_port != 0, now );

		} else if ( settings.api_port )
			button ( surface, row ( L"Dashboard", L"Opens the web dashboard in your browser.", 150.0f, 30.0f ), L"Open", ICON_ARROW, CONTROL_ACTION, COMMAND_OPEN_DASHBOARD * 256, false, true, now );

		section ( L"APPEARANCE", false );
		segmented ( surface, row ( L"Theme", L"System follows the light or dark setting in Windows.", 252.0f, 30.0f ), themes, 3, settings.theme, CONTROL_CHOICE, SETTING_THEME * 16, true, now );

		{

			static const wchar_t* const spans [ ] = { L"2 minutes", L"10 minutes", L"1 hour" };
			static const wchar_t* const smoothings [ ] = { L"Off", L"5 seconds", L"30 seconds" };

			segmented ( surface, row ( L"Hashrate graph", L"How far back the graph on Overview reaches. Alloy keeps an hour of samples either way.", 324.0f, 30.0f ), spans, 3, settings.chart_span % 3, CONTROL_CHOICE, SETTING_CHART_SPAN * 16, true, now );
			segmented ( surface, row ( L"Graph smoothing", L"Averages each point over the seconds before it, which hides the second to second jitter.", 324.0f, 30.0f ), smoothings, 3, settings.chart_smoothing % 3, CONTROL_CHOICE, SETTING_CHART_SMOOTHING * 16, true, now );

		}

		section ( L"ABOUT", false );
		surface.text ( L"Alloy " ALLOY_VERSION_TEXT, render::FONT_BODY, row ( L"Version", L"SHA-256, scrypt, RandomX, the pool protocols, the shaders and this window are all written from scratch.", 200.0f, 30.0f ), colors.muted, render::ALIGN_RIGHT );
		surface.text ( L"Leo Hagnesten", render::FONT_BODY, row ( L"Author", L"Designed and written by Leo Hagnesten.", 200.0f, 30.0f ), colors.muted, render::ALIGN_RIGHT );
		button ( surface, row ( L"Logs and crash reports", L"Alloy keeps two weeks of logs, and a report with a stack trace if it ever crashes.", 150.0f, 30.0f ), L"Open folder", ICON_ARROW, CONTROL_ACTION, COMMAND_OPEN_LOGS * 256, false, true, now );

		y += 24.0f;
		scroll_limit [ SCROLL_SETTINGS ] = base::max ( 0.0f, y - area.bottom ( ) );
		hit_offset = 0.0f;
		hit_top = -1e9f;
		hit_bottom = 1e9f;
		hit_scroller = -1;
		surface.pop_shift ( );

	}

}
