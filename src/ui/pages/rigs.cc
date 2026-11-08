#include "ui/shell.hh"
#include "chain/algorithm.hh"
#include "base/math.hh"
#include "base/strings.hh"
#include "base/text.hh"

namespace ui {

	void shell::paint_rigs ( render::surface& surface, const snapshot& view, render::box area, double now ) {

		float offset = scroll_offset ( SCROLL_RIGS, now );
		float x = area.x + 28.0f;
		float wide = area.w - 56.0f;
		float y = area.y;
		int32_t online = view.running ? 1 : 0;
		uint32_t accepted = view.accepted;
		double total = view.running ? view.rate : 0.0;
		const wchar_t* local_kind = chain::algorithm_key ( view.running ? view.algorithm : settings.algorithm );
		bool mixed = false;

		for ( int32_t i = 0; i < view.rig_count; i++ ) {

			const rig_status& rig = view.rigs [ i ];

			if ( rig.state != RIG_ONLINE )
				continue;

			online += rig.running ? 1 : 0;
			accepted += rig.accepted;

			if ( base::same_text ( rig.algorithm, local_kind ) || ( view.algorithm == chain::ALGORITHM_RANDOMX && base::starts_with ( rig.algorithm, L"rx/" ) ) )
				total += rig.running ? rig.rate : 0.0;
			else if ( rig.running )
				mixed = true;

		}

		scroll_area [ SCROLL_RIGS ] = area;
		surface.push_shift ( 0.0f, -offset );
		hit_offset = -offset;
		hit_top = area.y;
		hit_bottom = area.bottom ( );
		hit_scroller = SCROLL_RIGS;

		const wchar_t* captions [ 3 ] = { L"MINING", L"COMBINED HASHRATE", L"SHARES ACCEPTED" };
		base::writer < wchar_t, 64 > values [ 3 ];
		base::writer < wchar_t, 96 > details [ 3 ];

		values [ 0 ].number ( static_cast < uint64_t > ( online ) ).add ( L" of " ).number ( static_cast < uint64_t > ( view.rig_count + 1 ) );
		details [ 0 ].add ( view.rig_count ? L"rigs, this PC included" : L"add other computers below" );
		values [ 1 ].scaled ( total, L"H/s" );
		details [ 1 ].add ( mixed ? L"rigs on other algorithms left out" : L"same algorithm as this PC" );
		values [ 2 ].number ( accepted );
		details [ 2 ].add ( L"since each rig started" );

		float part = wide / 3.0f;

		for ( int32_t i = 0; i < 3; i++ ) {

			float left = x + part * static_cast < float > ( i ) + ( i ? 20.0f : 0.0f );
			float inner = part - ( i ? 20.0f : 0.0f ) - 12.0f;

			if ( i )
				surface.column ( x + part * static_cast < float > ( i ), y + 20.0f, y + 76.0f, colors.divider );

			caption ( surface, render::box { left, y + 18.0f, inner, 16.0f }, captions [ i ] );
			surface.text ( values [ i ].get ( ), render::FONT_FIGURE, render::box { left, y + 36.0f, inner, 30.0f }, colors.text );
			surface.text ( details [ i ].get ( ), render::FONT_SMALL, render::box { left, y + 66.0f, inner, 16.0f }, colors.muted );

		}

		y += 92.0f;
		surface.rule ( area.x, area.right ( ), y - 1.0f, colors.divider );
		y += 22.0f;
		caption ( surface, render::box { x, y, wide, 16.0f }, L"RIGS" );
		y += 28.0f;

		static const wchar_t* const titles [ ] = { L"Rig", L"State", L"Algorithm", L"Hashrate", L"Shares", L"Uptime" };
		float columns [ 6 ] = { x, x + wide * 0.30f, x + wide * 0.46f, x + wide * 0.60f, x + wide * 0.74f, x + wide * 0.86f };

		for ( int32_t i = 0; i < 6; i++ )
			surface.text ( titles [ i ], render::FONT_SMALL, render::box { columns [ i ], y, 140.0f, 22.0f }, colors.faint );

		y += 24.0f;
		surface.rule ( x, x + wide, y - 1.0f, colors.divider );

		auto paint_row = [ & ] ( const wchar_t* name, const wchar_t* address, uint8_t state, bool running, bool paused, const wchar_t* algorithm, double rate, uint32_t good, uint32_t bad, double uptime, int32_t index ) {

			float row_height = 48.0f;
			const wchar_t* label = L"Waiting";
			render::color tint = colors.faint;
			base::text cell;

			if ( state == RIG_ONLINE ) {

				label = !running ? L"Idle" : ( paused ? L"Paused" : L"Mining" );
				tint = !running ? colors.faint : ( paused ? colors.warning : colors.good );

			} else if ( state == RIG_UNREACHABLE ) {

				label = L"Unreachable";
				tint = colors.bad;

			} else if ( state == RIG_LOCKED ) {

				label = L"Needs token";
				tint = colors.warning;

			}

			surface.text ( name [ 0 ] ? name : address, render::FONT_BODY, render::box { columns [ 0 ], y + 6.0f, columns [ 1 ] - columns [ 0 ] - 12.0f, 20.0f }, colors.text );
			surface.text ( address, render::FONT_SMALL, render::box { columns [ 0 ], y + 25.0f, columns [ 1 ] - columns [ 0 ] - 12.0f, 16.0f }, colors.muted );
			surface.dot ( columns [ 1 ] + 4.0f, y + row_height * 0.5f, 3.5f, tint );
			surface.text ( label, render::FONT_BODY, render::box { columns [ 1 ] + 16.0f, y, 120.0f, row_height }, colors.text );
			surface.text ( state == RIG_ONLINE ? algorithm : L"\x2014", render::FONT_BODY, render::box { columns [ 2 ], y, 120.0f, row_height }, colors.muted );

			if ( state == RIG_ONLINE && running )
				cell.scaled ( rate, L"H/s" );
			else
				cell.add ( L"\x2014" );

			surface.text ( cell.get ( ), render::FONT_BODY, render::box { columns [ 3 ], y, 120.0f, row_height }, colors.text );
			cell.clear ( );

			if ( state == RIG_ONLINE ) {

				cell.number ( good );

				if ( bad )
					cell.add ( L", " ).number ( bad ).add ( L" rejected" );

			} else
				cell.add ( L"\x2014" );

			surface.text ( cell.get ( ), render::FONT_BODY, render::box { columns [ 4 ], y, 120.0f, row_height }, colors.muted );
			cell.clear ( );

			if ( state == RIG_ONLINE && running )
				cell.duration ( uptime );
			else
				cell.add ( L"\x2014" );

			surface.text ( cell.get ( ), render::FONT_BODY, render::box { columns [ 5 ], y, x + wide - columns [ 5 ] - 36.0f, row_height }, colors.muted );

			if ( index >= 0 ) {

				render::box remove = { x + wide - 28.0f, y + 12.0f, 28.0f, 24.0f };
				float level = glow ( CONTROL_ACTION, COMMAND_REMOVE_RIG * 256 + index, now );

				if ( level > 0.0f )
					surface.fill ( remove, render::faded ( colors.control_hover, level ), corner );

				draw_icon ( surface, ICON_CLOSE, remove.x + 6.0f, remove.y + 4.0f, 16.0f, render::blend ( colors.faint, colors.text, level ) );
				add_hit ( remove, CONTROL_ACTION, COMMAND_REMOVE_RIG * 256 + index );
				add_hint ( remove, L"Stop watching this rig. Mining there carries on." );

			}

			y += row_height;
			surface.rule ( x, x + wide, y - 1.0f, colors.divider );

		};

		wchar_t local_name [ 80 ];
		base::writer < wchar_t, 80 > named;

		named.add ( L"This PC" );
		base::copy_text ( local_name, base::count_of ( local_name ), named.get ( ) );
		paint_row ( local_name, view.cpu_name, RIG_ONLINE, view.running, view.paused, local_kind, view.rate, view.accepted, view.rejected, view.uptime, -1 );

		for ( int32_t i = 0; i < view.rig_count; i++ ) {

			const rig_status& rig = view.rigs [ i ];

			paint_row ( rig.name, rig.address, rig.state, rig.running, rig.paused, rig.algorithm, rig.rate, rig.accepted, rig.rejected, rig.uptime, i );

		}

		y += 26.0f;
		caption ( surface, render::box { x, y, wide, 16.0f }, L"WATCH ANOTHER COMPUTER" );
		y += 26.0f;

		render::box entry = { x, y, base::min ( wide - 120.0f, 460.0f ), 30.0f };

		field ( surface, entry, SETTING_RIG, false, now );
		button ( surface, render::box { entry.right ( ) + 10.0f, y, 96.0f, 30.0f }, L"Add", ICON_PLUS, CONTROL_ACTION, COMMAND_ADD_RIG * 256, true, view.rig_count < rig_room, now );
		y += 42.0f;
		surface.paragraph ( L"On the other computer, turn on the stats API and Let other computers connect in Settings, then copy its access token. Type its address and the token here, like 192.168.1.20 0123abcd..., or paste the dashboard link. Rigs report every three seconds.", render::FONT_SMALL, render::box { x, y, base::min ( wide, 640.0f ), 52.0f }, colors.faint );
		y += 70.0f;

		scroll_limit [ SCROLL_RIGS ] = base::max ( 0.0f, y - area.bottom ( ) );
		hit_offset = 0.0f;
		hit_top = -1e9f;
		hit_bottom = 1e9f;
		hit_scroller = -1;
		surface.pop_shift ( );

	}

}
