#include "ui/icons.hh"
#include "base/math.hh"

namespace ui {

	void draw_icon ( render::surface& surface, icon kind, float x, float y, float size, render::color tint ) {

		float unit = size / 16.0f;
		float stroke = base::max ( 1.5f * unit, 1.0f );

		auto at = [ & ] ( float value, bool vertical ) {

			return ( vertical ? y : x ) + value * unit;

		};

		auto segment = [ & ] ( float x0, float y0, float x1, float y1 ) {

			surface.line ( at ( x0, false ), at ( y0, true ), at ( x1, false ), at ( y1, true ), tint, stroke );

		};

		switch ( kind ) {

			case ICON_OVERVIEW:
				segment ( 3.5f, 13.0f, 3.5f, 9.0f );
				segment ( 8.0f, 13.0f, 8.0f, 3.5f );
				segment ( 12.5f, 13.0f, 12.5f, 6.5f );
				break;

			case ICON_ACTIVITY: {

				float points [ ] = { at ( 1.5f, false ), at ( 8.5f, true ), at ( 4.5f, false ), at ( 8.5f, true ), at ( 6.5f, false ), at ( 3.5f, true ), at ( 9.5f, false ), at ( 12.5f, true ), at ( 11.5f, false ), at ( 8.5f, true ), at ( 14.5f, false ), at ( 8.5f, true ) };

				surface.polyline ( points, 6, tint, stroke );
				break;

			}

			case ICON_SETTINGS:
				segment ( 2.0f, 5.0f, 4.0f, 5.0f );
				segment ( 8.0f, 5.0f, 14.0f, 5.0f );
				segment ( 2.0f, 11.0f, 8.0f, 11.0f );
				segment ( 12.0f, 11.0f, 14.0f, 11.0f );
				surface.ring ( at ( 6.0f, false ), at ( 5.0f, true ), 2.0f * unit, tint, stroke );
				surface.ring ( at ( 10.0f, false ), at ( 11.0f, true ), 2.0f * unit, tint, stroke );
				break;

			case ICON_PLAY: {

				float points [ ] = { at ( 4.5f, false ), at ( 3.0f, true ), at ( 13.0f, false ), at ( 8.0f, true ), at ( 4.5f, false ), at ( 13.0f, true ) };

				surface.polygon ( points, 3, tint );
				break;

			}

			case ICON_STOP:
				surface.fill ( render::box { at ( 4.0f, false ), at ( 4.0f, true ), 8.0f * unit, 8.0f * unit }, tint, 1.5f * unit );
				break;

			case ICON_MINIMIZE:
				surface.rule ( at ( 3.0f, false ), at ( 13.0f, false ), at ( 8.0f, true ), tint );
				break;

			case ICON_MAXIMIZE: {

				float left = surface.snap ( at ( 3.0f, false ) );
				float top = surface.snap ( at ( 3.0f, true ) );
				float side = surface.snap ( 10.0f * unit );
				float pixel = 1.0f / surface.scale ( );

				surface.outline ( render::box { left, top, side + pixel, side + pixel }, tint, pixel, 1.5f );
				break;

			}

			case ICON_RESTORE: {

				float left = surface.snap ( at ( 3.0f, false ) );
				float top = surface.snap ( at ( 5.0f, true ) );
				float side = surface.snap ( 8.0f * unit );
				float pixel = 1.0f / surface.scale ( );
				float step = surface.snap ( 2.0f * unit );

				surface.outline ( render::box { left, top, side + pixel, side + pixel }, tint, pixel, 1.5f );
				surface.rule ( left + step, left + step + side, top - step, tint );
				surface.column ( left + step + side, top - step, top - step + side + pixel, tint );
				break;

			}

			case ICON_CLOSE: {

				float pixel = 1.0f / surface.scale ( );

				surface.line ( at ( 3.0f, false ), at ( 3.0f, true ), at ( 13.0f, false ), at ( 13.0f, true ), tint, pixel * 1.1f );
				surface.line ( at ( 13.0f, false ), at ( 3.0f, true ), at ( 3.0f, false ), at ( 13.0f, true ), tint, pixel * 1.1f );
				break;

			}

			case ICON_MINUS:
				segment ( 4.0f, 8.0f, 12.0f, 8.0f );
				break;

			case ICON_PLUS:
				segment ( 4.0f, 8.0f, 12.0f, 8.0f );
				segment ( 8.0f, 4.0f, 8.0f, 12.0f );
				break;

			case ICON_CLEAR:
				segment ( 2.5f, 4.5f, 13.5f, 4.5f );
				segment ( 6.5f, 2.5f, 9.5f, 2.5f );
				segment ( 4.0f, 4.5f, 5.0f, 13.5f );
				segment ( 5.0f, 13.5f, 11.0f, 13.5f );
				segment ( 11.0f, 13.5f, 12.0f, 4.5f );
				break;

			case ICON_PROCESSOR: {

				static constexpr float places [ ] = { 6.0f, 10.0f };

				surface.outline ( render::box { at ( 4.0f, false ), at ( 4.0f, true ), 8.0f * unit, 8.0f * unit }, tint, stroke, 1.5f * unit );

				for ( float place : places ) {

					segment ( place, 1.5f, place, 3.0f );
					segment ( place, 13.0f, place, 14.5f );
					segment ( 1.5f, place, 3.0f, place );
					segment ( 13.0f, place, 14.5f, place );

				}

				break;

			}

			case ICON_GRAPHICS:
				surface.outline ( render::box { at ( 1.5f, false ), at ( 3.5f, true ), 13.0f * unit, 9.0f * unit }, tint, stroke, 1.5f * unit );
				surface.ring ( at ( 9.5f, false ), at ( 8.0f, true ), 2.2f * unit, tint, stroke );
				segment ( 4.5f, 6.5f, 4.5f, 9.5f );
				segment ( 3.5f, 12.5f, 3.5f, 14.0f );
				break;

			case ICON_EYE:
			case ICON_HIDDEN: {

				float points [ 34 ];
				int32_t count = 0;

				for ( int32_t i = 0; i <= 8; i++ ) {

					float t = static_cast < float > ( i ) / 8.0f;
					float lean = 2.0f * t - 1.0f;

					points [ count++ ] = at ( 1.5f + 13.0f * t, false );
					points [ count++ ] = at ( 8.0f - 4.6f * ( 1.0f - lean * lean ), true );

				}

				for ( int32_t i = 7; i >= 0; i-- ) {

					float t = static_cast < float > ( i ) / 8.0f;
					float lean = 2.0f * t - 1.0f;

					points [ count++ ] = at ( 1.5f + 13.0f * t, false );
					points [ count++ ] = at ( 8.0f + 4.6f * ( 1.0f - lean * lean ), true );

				}

				surface.polyline ( points, count / 2, tint, stroke );
				surface.ring ( at ( 8.0f, false ), at ( 8.0f, true ), 2.1f * unit, tint, stroke );

				if ( kind == ICON_HIDDEN )
					segment ( 2.5f, 13.5f, 13.5f, 2.5f );

				break;

			}

			case ICON_ARROW:
				segment ( 3.0f, 8.0f, 12.5f, 8.0f );
				segment ( 9.0f, 4.5f, 12.5f, 8.0f );
				segment ( 9.0f, 11.5f, 12.5f, 8.0f );
				break;

			case ICON_EARNINGS:
				surface.ring ( at ( 8.0f, false ), at ( 8.0f, true ), 6.0f * unit, tint, stroke );
				segment ( 8.0f, 4.0f, 8.0f, 12.0f );
				segment ( 10.0f, 6.0f, 6.8f, 6.0f );
				segment ( 6.0f, 8.0f, 10.0f, 8.0f );
				segment ( 9.2f, 10.0f, 6.0f, 10.0f );
				break;

			case ICON_RIGS:
				surface.outline ( render::box { at ( 2.0f, false ), at ( 2.5f, true ), 12.0f * unit, 4.5f * unit }, tint, stroke, 1.0f * unit );
				surface.outline ( render::box { at ( 2.0f, false ), at ( 9.0f, true ), 12.0f * unit, 4.5f * unit }, tint, stroke, 1.0f * unit );
				surface.dot ( at ( 4.8f, false ), at ( 4.75f, true ), 0.9f * unit, tint );
				surface.dot ( at ( 4.8f, false ), at ( 11.25f, true ), 0.9f * unit, tint );
				break;

			case ICON_HELP:
				surface.ring ( at ( 8.0f, false ), at ( 8.0f, true ), 6.0f * unit, tint, stroke );
				surface.arc ( at ( 8.0f, false ), at ( 6.6f, true ), 2.0f * unit, -3.1f, 3.9f, tint, stroke );
				segment ( 8.0f, 8.6f, 8.0f, 9.6f );
				surface.dot ( at ( 8.0f, false ), at ( 11.6f, true ), 0.9f * unit, tint );
				break;

			case ICON_PAUSE:
				surface.fill ( render::box { at ( 4.5f, false ), at ( 3.5f, true ), 2.5f * unit, 9.0f * unit }, tint, 0.8f * unit );
				surface.fill ( render::box { at ( 9.0f, false ), at ( 3.5f, true ), 2.5f * unit, 9.0f * unit }, tint, 0.8f * unit );
				break;

			case ICON_SAVE:
				segment ( 8.0f, 2.5f, 8.0f, 10.0f );
				segment ( 4.5f, 6.8f, 8.0f, 10.3f );
				segment ( 11.5f, 6.8f, 8.0f, 10.3f );
				segment ( 2.5f, 13.5f, 13.5f, 13.5f );
				break;

			case ICON_KEYBOARD:
				surface.outline ( render::box { at ( 1.5f, false ), at ( 4.0f, true ), 13.0f * unit, 8.5f * unit }, tint, stroke, 1.5f * unit );
				segment ( 5.0f, 10.0f, 11.0f, 10.0f );
				surface.dot ( at ( 4.5f, false ), at ( 7.0f, true ), 0.8f * unit, tint );
				surface.dot ( at ( 7.0f, false ), at ( 7.0f, true ), 0.8f * unit, tint );
				surface.dot ( at ( 9.5f, false ), at ( 7.0f, true ), 0.8f * unit, tint );
				surface.dot ( at ( 12.0f, false ), at ( 7.0f, true ), 0.8f * unit, tint );
				break;

		}

	}

	void draw_mark ( render::surface& surface, float x, float y, float size ) {

		float unit = size / 64.0f;

		auto px = [ & ] ( float value ) {

			return x + value * unit;

		};

		auto py = [ & ] ( float value ) {

			return y + value * unit;

		};

		float left [ ] = { px ( 5.0f ), py ( 58.0f ), px ( 20.0f ), py ( 58.0f ), px ( 39.5f ), py ( 6.0f ), px ( 24.5f ), py ( 6.0f ) };
		float right [ ] = { px ( 44.0f ), py ( 58.0f ), px ( 59.0f ), py ( 58.0f ), px ( 39.5f ), py ( 6.0f ), px ( 24.5f ), py ( 6.0f ) };
		float fused [ ] = { px ( 24.5f ), py ( 6.0f ), px ( 39.5f ), py ( 6.0f ), px ( 32.0f ), py ( 26.0f ) };

		surface.shaded ( left, 4, render::rgb ( 0xD3DBE0 ), render::rgb ( 0x7B8892 ), py ( 6.0f ), py ( 58.0f ) );
		surface.shaded ( right, 4, render::rgb ( 0x41C2A4 ), render::rgb ( 0x187361 ), py ( 6.0f ), py ( 58.0f ) );
		surface.shaded ( fused, 3, render::rgb ( 0xB4EFDE ), render::rgb ( 0x6FD3B8 ), py ( 6.0f ), py ( 26.0f ) );

	}

}
