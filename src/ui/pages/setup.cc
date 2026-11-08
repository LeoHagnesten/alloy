#include "ui/shell.hh"
#include "chain/address.hh"
#include "pool/presets.hh"
#include "base/math.hh"
#include "base/strings.hh"
#include "base/text.hh"

namespace ui {

	static constexpr float setup_row = 48.0f;

	void shell::paint_setup ( render::surface& surface, const snapshot& view, render::box area, double now ) {

		static const wchar_t* const coins [ chain::ALGORITHM_COUNT ] = { L"Bitcoin", L"Litecoin", L"Monero", L"Kaspa" };
		static const wchar_t* const coin_notes [ chain::ALGORITHM_COUNT ] = {

			L"Bitcoin on a PC is a lottery ticket. The solo pools below pay the whole block reward if this computer ever finds a block, which is very unlikely next to dedicated machines.",
			L"Litecoin's scrypt suits the graphics card. litecoinpool.org needs a free account and pays Dogecoin on top of Litecoin.",
			L"Monero was made for processors, so this is where a PC earns the most. Pools pay out once your balance passes their minimum.",
			L"Kaspa runs on the graphics card here. Dedicated machines took over its network, so a PC earns very little; Kaspa pools need your kaspa: address.",

		};
		static const wchar_t* const wallet_labels [ chain::ALGORITHM_COUNT ] = { L"Bitcoin address", L"Username and worker", L"Monero address", L"Kaspa address" };
		static const wchar_t* const wallet_tips [ chain::ALGORITHM_COUNT ] = {

			L"The Bitcoin address that gets the reward. A dot and a name for this PC can follow it.",
			L"Your litecoinpool.org username, a dot, then any name for this PC, like leo.desktop.",
			L"The Monero address that gets paid. It starts with 4 or 8 and is 95 characters long.",
			L"The Kaspa address that gets paid, starting with kaspa: and then about 61 letters and digits.",

		};

		float offset = scroll_offset ( SCROLL_SETUP, now );
		float x = area.x + 28.0f;
		float wide = base::min ( area.w - 56.0f, 720.0f );
		float y = area.y + 6.0f;
		uint8_t kind = settings.algorithm % chain::ALGORITHM_COUNT;
		base::text tip;

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
		segmented ( surface, row ( L"Coin", coin_notes [ kind ], 400.0f, 30.0f ), coins, chain::ALGORITHM_COUNT, kind, CONTROL_ALGORITHM, 0, !view.running, now );
		y += 10.0f;
		note ( coin_notes [ kind ], colors.muted );

		section ( L"WHERE THE MONEY GOES" );

		chain::address_check check = chain::check_worker ( kind, fields [ SETTING_WORKER ].text );
		bool flagged = check.verdict == chain::ADDRESS_BROKEN || check.verdict == chain::ADDRESS_OTHER_COIN;
		bool typed = fields [ SETTING_WORKER ].length > 0 && !base::same_text ( fields [ SETTING_WORKER ].text, L"alloy.worker" );

		field ( surface, row ( wallet_labels [ kind ], wallet_tips [ kind ], 420.0f, 30.0f ), SETTING_WORKER, false, now, flagged );
		y += 10.0f;

		if ( check.verdict == chain::ADDRESS_BROKEN )
			note ( L"That looks like an address, but its checksum is wrong. Check it for a typo.", colors.bad );
		else if ( check.verdict == chain::ADDRESS_OTHER_COIN ) {

			tip.add ( L"That's a " ).add ( check.coin ).add ( L" address, which " ).add ( coins [ kind ] ).add ( L" pools can't pay." );
			note ( tip.get ( ), colors.bad );

		} else if ( check.verdict == chain::ADDRESS_VALID ) {

			tip.add ( L"A valid " ).add ( check.coin ).add ( L" address." );
			note ( tip.get ( ), colors.good );

		} else
			note ( wallet_tips [ kind ], colors.muted );

		section ( L"POOL" );

		int32_t matches [ 4 ];
		int32_t known = 0;
		int32_t current = -1;
		float widths [ 4 ] = { };
		float total_width = 0.0f;

		for ( int32_t i = 0; i < pool::preset_count ( ) && known < 4; i++ ) {

			if ( pool::preset_at ( i ).algorithm != kind )
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
