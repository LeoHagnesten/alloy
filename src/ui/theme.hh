#pragma once

#include "render/surface.hh"

namespace ui {

	static constexpr float corner = 4.0f;

	struct palette {

		render::color content;
		render::color chrome;
		render::color control;
		render::color control_hover;
		render::color control_line;
		render::color divider;
		render::color text;
		render::color muted;
		render::color faint;
		render::color accent;
		render::color accent_hover;
		render::color accent_text;
		render::color accent_soft;
		render::color good;
		render::color warning;
		render::color bad;
		render::color close_hover;
		render::color selection;
		render::color tip;

	};

	inline palette dark_palette ( ) {

		return palette {

			render::rgb ( 0x121417 ),
			render::rgb ( 0x0D0F11 ),
			render::rgb ( 0x1B1E22 ),
			render::rgb ( 0x23272C ),
			render::rgb ( 0x2C3137 ),
			render::rgb ( 0x22262B ),
			render::rgb ( 0xE5E8EA ),
			render::rgb ( 0x949BA3 ),
			render::rgb ( 0x5E656D ),
			render::rgb ( 0x3DB39A ),
			render::rgb ( 0x4FC4AA ),
			render::rgb ( 0x03140F ),
			render::rgb ( 0x3DB39A, 0.14f ),
			render::rgb ( 0x4DBB7C ),
			render::rgb ( 0xD9A441 ),
			render::rgb ( 0xE2614F ),
			render::rgb ( 0xC42B1C ),
			render::rgb ( 0x3DB39A, 0.32f ),
			render::rgb ( 0x23272C ),

		};

	}

	inline palette light_palette ( ) {

		return palette {

			render::rgb ( 0xFAFBFB ),
			render::rgb ( 0xF0F2F3 ),
			render::rgb ( 0xFFFFFF ),
			render::rgb ( 0xEDEFF1 ),
			render::rgb ( 0xD3D8DC ),
			render::rgb ( 0xE2E5E8 ),
			render::rgb ( 0x15181B ),
			render::rgb ( 0x58616A ),
			render::rgb ( 0x9AA2A9 ),
			render::rgb ( 0x0F8A72 ),
			render::rgb ( 0x0C7963 ),
			render::rgb ( 0xFFFFFF ),
			render::rgb ( 0x0F8A72, 0.10f ),
			render::rgb ( 0x1F9657 ),
			render::rgb ( 0xB1741B ),
			render::rgb ( 0xC63D2E ),
			render::rgb ( 0xC42B1C ),
			render::rgb ( 0x0F8A72, 0.24f ),
			render::rgb ( 0xFFFFFF ),

		};

	}

}
