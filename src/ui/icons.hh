#pragma once

#include "render/surface.hh"

namespace ui {

	enum icon : uint8_t {

		ICON_OVERVIEW,
		ICON_ACTIVITY,
		ICON_SETTINGS,
		ICON_PLAY,
		ICON_STOP,
		ICON_MINIMIZE,
		ICON_MAXIMIZE,
		ICON_RESTORE,
		ICON_CLOSE,
		ICON_MINUS,
		ICON_PLUS,
		ICON_CLEAR,
		ICON_PROCESSOR,
		ICON_GRAPHICS,
		ICON_EYE,
		ICON_HIDDEN,
		ICON_ARROW,

	};

	void draw_icon ( render::surface& surface, icon kind, float x, float y, float size, render::color tint );

	void draw_mark ( render::surface& surface, float x, float y, float size );

}
