#include "ui/shell.hh"
#include "base/math.hh"
#include "base/text.hh"

namespace ui {

	void shell::paint_activity ( render::surface& surface, const snapshot& view, render::box area, double now ) {

		float x = area.x + 28.0f;
		float wide = area.w - 56.0f;
		float tabs_top = area.y + 10.0f;
		base::text labels [ 2 ];
		float left = x;
		float lefts [ 2 ] = { };
		float widths [ 2 ] = { };

		labels [ 0 ].add ( L"Shares" );
		labels [ 1 ].add ( L"Log" );

		if ( view.share_count )
			labels [ 0 ].add ( L"  " ).number ( static_cast < uint64_t > ( view.share_count ) );

		for ( int32_t i = 0; i < 2; i++ ) {

			float level = i == tab ? 0.0f : glow ( CONTROL_TAB, i, now );

			widths [ i ] = surface.measure ( labels [ i ].get ( ), render::FONT_STRONG );
			lefts [ i ] = left;
			surface.text ( labels [ i ].get ( ), render::FONT_STRONG, render::box { left, tabs_top, widths [ i ] + 4.0f, 30.0f }, i == tab ? colors.text : render::blend ( colors.muted, colors.text, level ) );
			add_hit ( render::box { left - 8.0f, tabs_top, widths [ i ] + 16.0f, 34.0f }, CONTROL_TAB, i );
			left += widths [ i ] + 28.0f;

		}

		tab_left.duration = 0.22f;
		tab_width.duration = 0.22f;

		if ( tab_width.to <= 0.0f ) {

			tab_left.jump ( lefts [ tab ] );
			tab_width.jump ( widths [ tab ] );

		} else {

			tab_left.go ( lefts [ tab ], now );
			tab_width.go ( widths [ tab ], now );

		}

		float line_y = area.y + 44.0f;

		surface.rule ( area.x, area.right ( ), line_y, colors.divider );
		surface.fill ( render::box { tab_left.at ( now ), line_y - 1.0f, tab_width.at ( now ), 2.0f }, colors.accent, 1.0f );

		if ( tab == 1 )
			button ( surface, render::box { area.right ( ) - 28.0f - 96.0f, area.y + 8.0f, 96.0f, 28.0f }, L"Clear", ICON_CLEAR, CONTROL_CLEAR_LOG, 0, false, view.log_count > 0, now );

		render::box list = { area.x, line_y + 1.0f, area.w, area.bottom ( ) - line_y - 1.0f };
		scroller which = tab == 0 ? SCROLL_SHARES : SCROLL_LOG;
		float row_height = tab == 0 ? 32.0f : 28.0f;
		float header = tab == 0 ? 30.0f : 0.0f;
		int32_t rows = tab == 0 ? view.share_count : view.log_count;
		render::box rows_area = { list.x, list.y + header, list.w, list.h - header };

		scroll_area [ SCROLL_SHARES ] = render::box { };
		scroll_area [ SCROLL_LOG ] = render::box { };
		scroll_area [ which ] = rows_area;
		scroll_limit [ which ] = base::max ( 0.0f, static_cast < float > ( rows ) * row_height + 12.0f - rows_area.h );

		if ( scroll_goal [ which ] > scroll_limit [ which ] ) {

			scroll_goal [ which ] = base::round ( scroll_limit [ which ] * zoom ) / zoom;
			scroll_motion [ which ].jump ( scroll_goal [ which ] );

		}

		float columns [ 6 ] = { x, x + 96.0f, x + 220.0f, x + 340.0f, x + 450.0f, x + 570.0f };

		if ( tab == 0 ) {

			static const wchar_t* const titles [ ] = { L"Time", L"Result", L"Difficulty", L"Latency", L"Nonce", L"Detail" };

			for ( int32_t i = 0; i < 6; i++ )
				surface.text ( titles [ i ], render::FONT_SMALL, render::box { columns [ i ], list.y, 120.0f, header }, colors.faint );

			surface.rule ( area.x, area.right ( ), list.y + header - 1.0f, colors.divider );

		}

		if ( !rows ) {

			const wchar_t* empty = tab == 0 ? L"No shares yet. Accepted and rejected shares show up here." : L"Nothing logged yet.";

			surface.text ( empty, render::FONT_BODY, render::box { rows_area.x, rows_area.y + 24.0f, rows_area.w, 24.0f }, colors.faint, render::ALIGN_CENTER );

			return;

		}

		float offset = scroll_offset ( which, now );

		surface.push_clip ( rows_area );
		surface.push_shift ( 0.0f, -offset );

		int32_t first = base::max ( 0, static_cast < int32_t > ( offset / row_height ) - 1 );
		int32_t last = base::min ( rows, static_cast < int32_t > ( ( offset + rows_area.h ) / row_height ) + 2 );

		for ( int32_t i = first; i < last; i++ ) {

			float top = rows_area.y + 4.0f + static_cast < float > ( i ) * row_height;
			base::text cell;

			if ( tab == 0 ) {

				const share_row& item = view.shares [ view.share_count - 1 - i ];

				cell.clock ( item.time.hour, item.time.minute, item.time.second );
				surface.text ( cell.get ( ), render::FONT_MONO, render::box { columns [ 0 ], top, 90.0f, row_height }, colors.muted );
				surface.dot ( columns [ 1 ] + 4.0f, top + row_height * 0.5f, 3.5f, item.accepted ? colors.good : colors.bad );
				surface.text ( item.accepted ? L"Accepted" : L"Rejected", render::FONT_BODY, render::box { columns [ 1 ] + 16.0f, top, 110.0f, row_height }, colors.text );
				cell.clear ( );
				cell.compact ( item.difficulty );
				surface.text ( cell.get ( ), render::FONT_BODY, render::box { columns [ 2 ], top, 110.0f, row_height }, colors.text );
				cell.clear ( );

				if ( item.latency > 0.0 )
					cell.decimal ( item.latency, item.latency < 10.0 ? 1 : 0 ).add ( L" ms" );

				surface.text ( cell.get ( ), render::FONT_BODY, render::box { columns [ 3 ], top, 100.0f, row_height }, colors.muted );
				cell.clear ( );
				cell.hex32 ( item.nonce );
				surface.text ( cell.get ( ), render::FONT_MONO, render::box { columns [ 4 ], top, 110.0f, row_height }, colors.muted );
				surface.text ( item.reason, render::FONT_BODY, render::box { columns [ 5 ], top, area.right ( ) - 28.0f - columns [ 5 ], row_height }, colors.muted );

			} else {

				const log_row& item = view.logs [ view.log_count - 1 - i ];
				render::color tint = item.level == LEVEL_GOOD ? colors.good : ( item.level == LEVEL_WARNING ? colors.warning : ( item.level == LEVEL_ERROR ? colors.bad : colors.faint ) );

				cell.clock ( item.time.hour, item.time.minute, item.time.second );
				surface.text ( cell.get ( ), render::FONT_MONO, render::box { x, top, 90.0f, row_height }, colors.faint );
				surface.dot ( x + 92.0f, top + row_height * 0.5f, 3.0f, tint );
				surface.text ( item.text, render::FONT_BODY, render::box { x + 108.0f, top, wide - 108.0f, row_height }, item.level == LEVEL_ERROR ? colors.bad : colors.text );

			}

			if ( tab == 0 )
				surface.rule ( area.x + 28.0f, area.right ( ) - 28.0f, top + row_height - 1.0f, colors.divider );

		}

		surface.pop_shift ( );
		surface.pop_clip ( );

	}

}
