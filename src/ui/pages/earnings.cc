#include "ui/shell.hh"
#include "chain/coinbase.hh"
#include "base/math.hh"
#include "base/text.hh"

namespace ui {

	static const wchar_t* const currency_marks [ ] = { L"$", L"\x20AC", L"\x00A3", L" kr" };

	static int32_t places_for ( double value, int32_t significant ) {

		int32_t places = significant - 1;

		while ( value > 0.0 && value < 1.0 && places < 14 ) {

			value *= 10.0;
			places++;

		}

		return places;

	}

	template < typename writer >
	static void coin_amount ( writer& out, double value, const wchar_t* symbol ) {

		bool satoshis = symbol [ 0 ] == L'B' || symbol [ 0 ] == L'L';

		if ( value > 0.0 && value < 0.0001 && satoshis ) {

			double small = value * 1e8;

			out.trimmed ( small, small >= 100.0 ? 0 : places_for ( small, 3 ) ).add ( L" sat" );

			return;

		}

		int32_t places = value >= 100.0 ? 2 : ( value >= 1.0 ? 4 : base::max ( 8, places_for ( value, 3 ) ) );

		out.trimmed ( value, base::min ( places, 14 ) ).put ( L' ' ).add ( symbol );

	}

	template < typename writer >
	static void money ( writer& out, double value, uint8_t currency ) {

		int32_t places = value < 0.1 ? 4 : 2;

		if ( currency % 4 == 3 )
			out.decimal ( value, places ).add ( currency_marks [ 3 ] );
		else
			out.add ( currency_marks [ currency % 4 ] ).decimal ( value, places );

	}

	template < typename writer >
	static void long_time ( writer& out, double seconds ) {

		if ( seconds >= 86400.0 * 730.0 )
			out.compact ( seconds / ( 86400.0 * 365.25 ) ).add ( L" years" );
		else if ( seconds >= 86400.0 * 3.0 )
			out.decimal ( seconds / 86400.0, 0 ).add ( L" days" );
		else
			out.duration ( seconds );

	}

	void shell::paint_earnings ( render::surface& surface, const snapshot& view, render::box area, double now ) {

		float offset = scroll_offset ( SCROLL_EARNINGS, now );
		float x = area.x + 28.0f;
		float wide = area.w - 56.0f;
		float y = area.y;
		uint8_t kind = view.running ? view.algorithm : settings.algorithm;
		uint8_t flavour = view.running ? view.variant : settings.variant;
		const wchar_t* symbol = chain::coin_symbol ( kind, flavour );
		bool priced = view.price_known && view.price > 0.0;
		const day_row* today = nullptr;

		for ( int32_t i = 0; i < view.day_count; i++ ) {

			if ( view.days [ i ].day == view.today )
				today = &view.days [ i ];

		}

		scroll_area [ SCROLL_EARNINGS ] = area;
		surface.push_shift ( 0.0f, -offset );
		hit_offset = -offset;
		hit_top = area.y;
		hit_bottom = area.bottom ( );
		hit_scroller = SCROLL_EARNINGS;

		struct cell {

			const wchar_t* caption;
			const wchar_t* tip;
			base::writer < wchar_t, 64 > value;
			base::writer < wchar_t, 96 > detail;
			bool quiet = false;

		};

		cell cells [ 5 ];

		cells [ 0 ].caption = L"THIS RUN";
		cells [ 1 ].caption = L"TODAY";
		cells [ 2 ].caption = L"ALL TIME";
		cells [ 3 ].caption = L"PER DAY";
		cells [ 4 ].caption = L"POOL BALANCE";
		cells [ 0 ].tip = L"Every accepted share counted as its slice of the block reward at the network difficulty when it was found. Before the pool's fee.";
		cells [ 1 ].tip = L"The same estimate for everything mined today. Alloy keeps a record of every day in its data folder.";
		cells [ 2 ].tip = L"Every share Alloy has had accepted on this computer, added up.";
		cells [ 3 ].tip = L"What the current hashrate earns in a day at today's difficulty and block reward.";
		cells [ 4 ].tip = L"What the pool owes you and hasn't paid out yet, read from its public API.";

		double amounts [ 4 ] = { view.run_earned, today ? today->earned [ kind % chain::ALGORITHM_COUNT ] : 0.0, view.lifetime.earned [ kind % chain::ALGORITHM_COUNT ], view.per_day };

		for ( int32_t i = 0; i < 4; i++ ) {

			if ( i == 3 && !view.network_known ) {

				cells [ i ].value.add ( L"\x2014" );
				cells [ i ].detail.add ( L"network unknown" );
				cells [ i ].quiet = true;

				continue;

			}

			coin_amount ( cells [ i ].value, amounts [ i ], symbol );

			if ( priced ) {

				cells [ i ].detail.add ( L"about " );
				money ( cells [ i ].detail, amounts [ i ] * view.price, settings.currency );

			} else if ( i == 0 )
				cells [ i ].detail.number ( view.accepted ).add ( view.accepted == 1 ? L" share" : L" shares" );
			else if ( i == 1 )
				cells [ i ].detail.number ( today ? today->accepted : 0u ).add ( L" shares, " ).duration ( today ? today->seconds : 0u ).add ( L" mined" );
			else if ( i == 2 )
				cells [ i ].detail.number ( view.lifetime.accepted ).add ( L" shares in " ).duration ( view.lifetime.seconds );
			else
				cells [ i ].detail.add ( L"at " ).scaled ( view.rate, L"H/s" );

		}

		if ( view.balance_known && view.balance_due > 0.0 ) {

			coin_amount ( cells [ 4 ].value, view.balance_due, symbol );

			if ( priced ) {

				cells [ 4 ].detail.add ( L"about " );
				money ( cells [ 4 ].detail, view.balance_due * view.price, settings.currency );

			} else
				cells [ 4 ].detail.add ( view.balance_source );

		} else if ( view.balance_known ) {

			cells [ 4 ].value.add ( view.pool_best > 0.0 ? L"Solo" : L"0" );
			cells [ 4 ].detail.add ( view.balance_source );

		} else {

			cells [ 4 ].value.add ( L"\x2014" );
			cells [ 4 ].detail.add ( settings.lookups ? L"pool has no public API" : L"online lookups off" );
			cells [ 4 ].quiet = true;

		}

		float part = wide / 5.0f;

		for ( int32_t i = 0; i < 5; i++ ) {

			float left = x + part * static_cast < float > ( i ) + ( i ? 20.0f : 0.0f );
			float inner = part - ( i ? 20.0f : 0.0f ) - 12.0f;

			if ( i )
				surface.column ( x + part * static_cast < float > ( i ), y + 20.0f, y + 76.0f, colors.divider );

			add_hint ( render::box { left, y + 14.0f, inner, 70.0f }, cells [ i ].tip );
			caption ( surface, render::box { left, y + 18.0f, inner, 16.0f }, cells [ i ].caption );
			surface.text ( cells [ i ].value.get ( ), render::FONT_FIGURE, render::box { left, y + 36.0f, inner, 30.0f }, cells [ i ].quiet ? colors.faint : colors.text );
			surface.text ( cells [ i ].detail.get ( ), render::FONT_SMALL, render::box { left, y + 66.0f, inner, 16.0f }, colors.muted );

		}

		y += 92.0f;
		surface.rule ( area.x, area.right ( ), y - 1.0f, colors.divider );
		y += 22.0f;

		float split = base::floor ( wide * 0.5f );
		float left_x = x;
		float left_w = split - 28.0f;
		float right_x = x + split + 28.0f;
		float right_w = wide - split - 28.0f;
		float left_y = y;
		float right_y = y;
		base::text value;

		caption ( surface, render::box { left_x, left_y, left_w, 16.0f }, L"NETWORK" );
		left_y += 24.0f;
		value.add ( chain::algorithm_name ( kind ) ).add ( L", " ).add ( symbol );
		property ( surface, left_x, left_y, left_w, L"Coin", value.get ( ) );

		if ( view.network_known ) {

			value.clear ( );
			value.compact ( view.network_difficulty );
			property ( surface, left_x, left_y, left_w, L"Network difficulty", value.get ( ) );

			if ( view.network_height ) {

				value.clear ( );
				value.grouped ( view.network_height );
				property ( surface, left_x, left_y, left_w, L"Block height", value.get ( ) );

			}

			value.clear ( );
			coin_amount ( value, view.block_reward, symbol );
			property ( surface, left_x, left_y, left_w, L"Block reward", value.get ( ) );
			value.clear ( );
			value.scaled ( view.network_rate, L"H/s" );
			property ( surface, left_x, left_y, left_w, L"Network hashrate", value.get ( ) );
			value.clear ( );

			if ( view.rate > 0.0 && view.network_rate > 0.0 && view.rate / view.network_rate < 0.001 )
				value.add ( L"1 in " ).compact ( view.network_rate / view.rate );
			else if ( view.rate > 0.0 && view.network_rate > 0.0 )
				value.decimal ( view.rate / view.network_rate * 100.0, 2 ).put ( L'%' );
			else
				value.add ( L"\x2014" );

			property ( surface, left_x, left_y, left_w, L"Your part of it", value.get ( ) );
			value.clear ( );

			if ( view.block_seconds > 0.0 )
				long_time ( value, view.block_seconds );
			else
				value.add ( L"\x2014" );

			property ( surface, left_x, left_y, left_w, L"Time to a block alone", value.get ( ) );

		} else {

			const wchar_t* why = kind == chain::ALGORITHM_RANDOMX ? ( flavour <= 1 ? ( settings.lookups ? L"Waiting for Monero's network stats" : L"Turn on online lookups in Settings" ) : L"Only Monero's network is looked up" ) : ( kind == chain::ALGORITHM_KHEAVYHASH ? L"Kaspa pools don't send the block reward, so only shares are counted" : L"Known once the pool sends a job" );

			surface.text ( why, render::FONT_BODY, render::box { left_x, left_y, left_w, 26.0f }, colors.faint );
			left_y += 26.0f;

		}

		value.clear ( );

		if ( view.shares_per_hour > 0.0 )
			value.decimal ( view.shares_per_hour, view.shares_per_hour < 10.0 ? 1 : 0 );
		else
			value.add ( L"\x2014" );

		property ( surface, left_x, left_y, left_w, L"Shares per hour", value.get ( ) );

		caption ( surface, render::box { right_x, right_y, right_w, 16.0f }, L"POOL ACCOUNT" );
		right_y += 24.0f;

		if ( view.balance_known ) {

			property ( surface, right_x, right_y, right_w, L"Read from", view.balance_source );

			if ( view.pool_best <= 0.0 ) {

				value.clear ( );
				coin_amount ( value, view.balance_due, symbol );
				property ( surface, right_x, right_y, right_w, L"Waiting to be paid", value.get ( ) );
				value.clear ( );
				coin_amount ( value, view.balance_paid, symbol );
				property ( surface, right_x, right_y, right_w, L"Paid so far", value.get ( ) );

			} else {

				value.clear ( );
				value.compact ( view.pool_best );
				property ( surface, right_x, right_y, right_w, L"Best share ever", value.get ( ) );

			}

			value.clear ( );
			value.scaled ( view.pool_rate, L"H/s" );
			property ( surface, right_x, right_y, right_w, L"The pool sees", value.get ( ) );
			value.clear ( );
			value.grouped ( view.pool_shares );
			property ( surface, right_x, right_y, right_w, L"Shares it counted", value.get ( ) );

		} else {

			surface.paragraph ( view.lookup_note [ 0 ] ? view.lookup_note : L"Alloy reads balances from SupportXMR and CKPool. Other pools show their balance on their own website.", render::FONT_BODY, render::box { right_x, right_y, right_w, 46.0f }, colors.faint );
			right_y += 52.0f;

		}

		if ( priced ) {

			value.clear ( );
			money ( value, view.price, settings.currency );
			value.add ( L" per " ).add ( symbol );
			property ( surface, right_x, right_y, right_w, L"Price", value.get ( ) );

		}

		y = base::max ( left_y, right_y ) + 22.0f;
		surface.rule ( area.x, area.right ( ), y - 10.0f, colors.divider );
		caption ( surface, render::box { x, y, wide, 16.0f }, L"BY DAY" );
		button ( surface, render::box { x + wide - 104.0f, y - 8.0f, 104.0f, 28.0f }, L"Export", ICON_SAVE, CONTROL_ACTION, COMMAND_EXPORT_SHARES * 256 + 1, false, view.day_count > 0, now );
		y += 30.0f;

		static const wchar_t* const titles [ ] = { L"Day", L"Mined", L"Hashes", L"Shares", L"Earned", L"Value" };
		float columns [ 6 ] = { x, x + wide * 0.2f, x + wide * 0.36f, x + wide * 0.52f, x + wide * 0.66f, x + wide * 0.84f };

		for ( int32_t i = 0; i < ( priced ? 6 : 5 ); i++ )
			surface.text ( titles [ i ], render::FONT_SMALL, render::box { columns [ i ], y, 140.0f, 22.0f }, colors.faint );

		y += 24.0f;
		surface.rule ( x, x + wide, y - 1.0f, colors.divider );

		if ( !view.day_count ) {

			surface.text ( L"Nothing mined yet. Each day shows up here once Alloy has run on it.", render::FONT_BODY, render::box { x, y + 10.0f, wide, 24.0f }, colors.faint );
			y += 44.0f;

		}

		for ( int32_t i = 0; i < view.day_count; i++ ) {

			const day_row& row = view.days [ i ];
			base::calendar date = base::calendar_of_day ( row.day );
			static const wchar_t* const months [ ] = { L"Jan", L"Feb", L"Mar", L"Apr", L"May", L"Jun", L"Jul", L"Aug", L"Sep", L"Oct", L"Nov", L"Dec" };
			base::text cell;

			if ( row.day == view.today )
				cell.add ( L"Today" );
			else if ( row.day + 1 == view.today )
				cell.add ( L"Yesterday" );
			else
				cell.number ( date.day ).put ( L' ' ).add ( months [ ( date.month + 11 ) % 12 ] ).put ( L' ' ).number ( date.year );

			surface.text ( cell.get ( ), render::FONT_BODY, render::box { columns [ 0 ], y, 140.0f, 30.0f }, colors.text );
			cell.clear ( );
			cell.duration ( row.seconds );
			surface.text ( cell.get ( ), render::FONT_BODY, render::box { columns [ 1 ], y, 140.0f, 30.0f }, colors.muted );
			cell.clear ( );
			cell.compact ( static_cast < double > ( row.hashes ) );
			surface.text ( cell.get ( ), render::FONT_BODY, render::box { columns [ 2 ], y, 140.0f, 30.0f }, colors.muted );
			cell.clear ( );
			cell.number ( row.accepted );

			if ( row.rejected )
				cell.add ( L", " ).number ( row.rejected ).add ( L" rejected" );

			surface.text ( cell.get ( ), render::FONT_BODY, render::box { columns [ 3 ], y, 160.0f, 30.0f }, colors.muted );

			double earned = row.earned [ kind % chain::ALGORITHM_COUNT ];

			cell.clear ( );
			coin_amount ( cell, earned, symbol );
			surface.text ( cell.get ( ), render::FONT_BODY, render::box { columns [ 4 ], y, 170.0f, 30.0f }, earned > 0.0 ? colors.text : colors.faint );

			if ( priced ) {

				cell.clear ( );
				money ( cell, earned * view.price, settings.currency );
				surface.text ( cell.get ( ), render::FONT_BODY, render::box { columns [ 5 ], y, 140.0f, 30.0f }, colors.muted );

			}

			y += 30.0f;
			surface.rule ( x, x + wide, y - 1.0f, colors.divider );

		}

		y += 16.0f;
		surface.paragraph ( L"Earnings are estimates: each accepted share counts as its slice of the block reward at the network difficulty when it was found, before the pool's fee. Pool balances are what the pool itself reports.", render::FONT_SMALL, render::box { x, y, base::min ( wide, 720.0f ), 40.0f }, colors.faint );
		y += 56.0f;

		scroll_limit [ SCROLL_EARNINGS ] = base::max ( 0.0f, y - area.bottom ( ) );
		hit_offset = 0.0f;
		hit_top = -1e9f;
		hit_bottom = 1e9f;
		hit_scroller = -1;
		surface.pop_shift ( );

	}

}
