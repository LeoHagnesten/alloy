#include "ui/shell.hh"
#include "chain/address.hh"
#include "pool/presets.hh"
#include "base/math.hh"
#include "base/strings.hh"
#include "base/text.hh"

namespace ui {

	static constexpr float setup_row = 48.0f;

	void shell::paint_setup ( render::surface& surface, const snapshot& view, render::box area, double now ) {

		static const wchar_t* const families [ chain::ALGORITHM_COUNT ] = { L"SHA-256d", L"Scrypt", L"RandomX", L"kHeavyHash", L"Ethash" };
		static const wchar_t* const family_notes [ chain::ALGORITHM_COUNT ] = {

			L" uses SHA-256d, where dedicated machines do nearly all the work. On a PC it's a lottery ticket: a solo pool pays the whole block if this computer ever finds one, which is very unlikely.",
			L" uses scrypt, which suits the graphics card. Dedicated machines mine most of it, so a PC earns little.",
			L" uses RandomX, which was made for processors, so this is where a PC earns the most. Pools pay out once your balance passes their minimum.",
			L" runs on the graphics card here. Dedicated machines took over its network, so a PC earns very little.",
			L" uses Ethash, which needs the graphics card and a few gigabytes of its memory for the DAG. Graphics cards still mine most of it, so a PC earns a real if small share.",

		};

		float offset = scroll_offset ( SCROLL_SETUP, now );
		float x = area.x + 28.0f;
		float wide = base::min ( area.w - 56.0f, 720.0f );
		float y = area.y + 6.0f;
		uint8_t kind = settings.algorithm % chain::ALGORITHM_COUNT;
		uint8_t chosen = settings.coins [ kind ];
		const wchar_t* coin_name = chain::coin_of ( chosen ).name;
		base::text tip;
		base::text about;
		base::text wallet;
		base::text wallet_tip;

		scroll_area [ SCROLL_SETUP ] = area;
		surface.push_shift ( 0.0f, -offset );
		hit_offset = -offset;
		hit_top = area.y;
		hit_bottom = area.bottom ( );
		hit_scroller = SCROLL_SETUP;

		auto note = [ & ] ( const wchar_t* text, render::color tint ) {

			float tall = surface.paragraph_height ( text, render::FONT_BODY, wide );

			surface.paragraph ( text, render::FONT_BODY, render::box { x, y, wide, tall + 4.0f }, tint );
			y += tall + 4.0f;

		};

		auto section = [ & ] ( const wchar_t* title ) {

			y += 22.0f;
			caption ( surface, render::box { x, y, wide, 16.0f }, title );
			y += 22.0f;
			surface.rule ( x, x + wide, y - 1.0f, colors.divider );

		};

		auto row = [ & ] ( const wchar_t* label, const wchar_t* text, float control_width, float control_height ) {

			render::box line = { x, y, wide, setup_row };

			begin_row ( surface, line, label, text );
			y += setup_row;

			return render::box { line.right ( ) - control_width, line.y + ( setup_row - control_height ) * 0.5f, control_width, control_height };

		};

		y += 18.0f;
		note ( L"Three things and Alloy is ready: what to mine, where the money goes and which pool to mine for. All of it can be changed later in Settings.", colors.muted );

		section ( L"WHAT TO MINE" );
		about.add ( coin_name ).add ( family_notes [ kind ] );
		segmented ( surface, row ( L"Algorithm", L"Each algorithm mines a family of coins. Pick the family, then the coin.", 400.0f, 30.0f ), families, chain::ALGORITHM_COUNT, kind, CONTROL_ALGORITHM, 0, !view.running, now );

		{

			uint8_t list [ chain::coin_room ];
			const wchar_t* labels [ chain::coin_room ];
			int32_t count = chain::coins_for ( kind, list, chain::coin_room );
			int32_t selected = 0;

			for ( int32_t i = 0; i < count; i++ ) {

				labels [ i ] = chain::coin_of ( list [ i ] ).label;

				if ( list [ i ] == chosen )
					selected = i;

			}

			segmented ( surface, row ( L"Coin", about.get ( ), base::min ( 68.0f * static_cast < float > ( count ), wide - 90.0f ), 30.0f ), labels, count, selected, CONTROL_CHOICE, SETTING_COIN * 16, !view.running, now );

		}

		y += 10.0f;
		note ( about.get ( ), colors.muted );

		section ( L"WHERE THE MONEY GOES" );

		chain::address_check check = chain::check_worker ( chosen, fields [ SETTING_WORKER ].text );
		bool flagged = check.verdict == chain::ADDRESS_BROKEN || check.verdict == chain::ADDRESS_OTHER_COIN;
		bool typed = fields [ SETTING_WORKER ].length > 0 && !base::same_text ( fields [ SETTING_WORKER ].text, L"alloy.worker" );

		wallet.add ( coin_name ).add ( L" address" );
		wallet_tip.add ( L"The " ).add ( coin_name ).add ( L" address that gets paid. Pools that use accounts want your username here instead, then a dot and a name for this PC." );
		field ( surface, row ( wallet.get ( ), wallet_tip.get ( ), 420.0f, 30.0f ), SETTING_WORKER, false, now, flagged );
		y += 10.0f;

		if ( check.verdict == chain::ADDRESS_BROKEN )
			note ( L"That looks like an address, but its checksum is wrong. Check it for a typo.", colors.bad );
		else if ( check.verdict == chain::ADDRESS_OTHER_COIN ) {

			tip.add ( L"That's a " ).add ( check.coin ).add ( L" address, which " ).add ( coin_name ).add ( L" pools can't pay." );
			note ( tip.get ( ), colors.bad );

		} else if ( check.verdict == chain::ADDRESS_VALID ) {

			tip.add ( L"A valid " ).add ( check.coin ).add ( L" address." );
			note ( tip.get ( ), colors.good );

		} else
			note ( wallet_tip.get ( ), colors.muted );

		section ( L"POOL" );

		int32_t matches [ 4 ];
		int32_t known = 0;
		int32_t current = -1;
		float widths [ 4 ] = { };
		float total_width = 0.0f;

		for ( int32_t i = 0; i < pool::preset_count ( ) && known < 4; i++ ) {

			if ( pool::preset_at ( i ).coin != chosen )
				continue;

			if ( base::same_text ( pool::preset_at ( i ).address, settings.pools [ kind ].address ) )
				current = i;

			widths [ known ] = surface.measure ( pool::preset_at ( i ).name, render::FONT_STRONG ) + 54.0f;
			total_width += widths [ known ] + ( known ? 8.0f : 0.0f );
			matches [ known++ ] = i;

		}

		if ( known ) {

			render::box spot = row ( L"Pick one", L"Pools Alloy knows how to talk to. Picking one fills in its address and a backup.", total_width, 30.0f );
			float left = spot.x;

			for ( int32_t i = 0; i < known; i++ ) {

				button ( surface, render::box { left, spot.y, widths [ i ], spot.h }, pool::preset_at ( matches [ i ] ).name, ICON_ARROW, CONTROL_PRESET, matches [ i ], matches [ i ] == current, true, now );
				left += widths [ i ] + 8.0f;

			}

		}

		field ( surface, row ( L"Or type its address", L"The pool's address as host:port. Start it with stratum+ssl:// for an encrypted link.", 420.0f, 30.0f ), SETTING_ADDRESS, false, now );
		y += 10.0f;
		note ( current >= 0 ? pool::preset_at ( current ).tip : L"Any Stratum pool for this coin works. Its website lists the address to use.", colors.muted );

		bool ready = typed && !flagged && fields [ SETTING_ADDRESS ].length > 0 && ( settings.cpu || ( settings.gpu && view.adapter_count > 0 ) );

		y += 28.0f;
		button ( surface, render::box { x, y, 156.0f, 34.0f }, L"Start mining", ICON_PLAY, CONTROL_ACTION, COMMAND_FINISH_SETUP * 256 + 1, true, ready && !view.running, now );
		button ( surface, render::box { x + 168.0f, y, 120.0f, 34.0f }, L"Skip for now", ICON_ARROW, CONTROL_ACTION, COMMAND_FINISH_SETUP * 256, false, true, now );
		y += 34.0f + 14.0f;

		if ( !typed )
			note ( L"Start mining unlocks once the wallet is in.", colors.faint );

		y += 28.0f;
		scroll_limit [ SCROLL_SETUP ] = base::max ( 0.0f, y - area.bottom ( ) );
		hit_offset = 0.0f;
		hit_top = -1e9f;
		hit_bottom = 1e9f;
		hit_scroller = -1;
		surface.pop_shift ( );

	}

}
