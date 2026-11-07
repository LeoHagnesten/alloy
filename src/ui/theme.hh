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

			render::rgb ( 0xE8EAEC ),
			render::rgb ( 0xDDE0E3 ),
			render::rgb ( 0xF1F2F3 ),
			render::rgb ( 0xE2E5E8 ),
			render::rgb ( 0xC3C9CE ),
			render::rgb ( 0xD1D5D9 ),
			render::rgb ( 0x16191C ),
			render::rgb ( 0x515A62 ),
			render::rgb ( 0x878F97 ),
			render::rgb ( 0x0D7C67 ),
			render::rgb ( 0x0A6D5A ),
			render::rgb ( 0xF3F5F5 ),
			render::rgb ( 0x0D7C67, 0.12f ),
			render::rgb ( 0x1C8650 ),
			render::rgb ( 0xA36811 ),
			render::rgb ( 0xB83626 ),
			render::rgb ( 0xC42B1C ),
			render::rgb ( 0x0D7C67, 0.24f ),
			render::rgb ( 0xF1F2F3 ),

		};

	}

}
