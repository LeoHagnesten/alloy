#pragma once

#include "base/list.hh"

struct HWND__;
struct ID3D11Device;
struct ID3D11DeviceContext;
struct IDXGISwapChain1;
struct IDCompositionDevice;
struct IDCompositionTarget;
struct IDCompositionVisual;
struct ID2D1Factory1;
struct ID2D1Device;
struct ID2D1DeviceContext;
struct ID2D1Bitmap1;
struct ID2D1SolidColorBrush;
struct ID2D1StrokeStyle;
struct ID2D1PathGeometry;
struct IDWriteFactory;
struct IDWriteTextFormat;
struct IDWriteTextLayout;
struct IDWriteTypography;
struct IDWriteInlineObject;

namespace render {

	struct color {

		float r = 0.0f;
		float g = 0.0f;
		float b = 0.0f;
		float a = 0.0f;

	};

	constexpr color rgb ( uint32_t hex, float alpha = 1.0f ) {

		return color { static_cast < float > ( hex >> 16 & 0xFF ) / 255.0f, static_cast < float > ( hex >> 8 & 0xFF ) / 255.0f, static_cast < float > ( hex & 0xFF ) / 255.0f, alpha };

	}

	inline color faded ( color tint, float amount ) {

		return color { tint.r, tint.g, tint.b, tint.a * amount };

	}

	inline color blend ( color from, color to, float t ) {

		return color { from.r + ( to.r - from.r ) * t, from.g + ( to.g - from.g ) * t, from.b + ( to.b - from.b ) * t, from.a + ( to.a - from.a ) * t };

	}

	struct box {

		float x = 0.0f;
		float y = 0.0f;
		float w = 0.0f;
		float h = 0.0f;

		float right ( ) const {

			return x + w;

		}

		float bottom ( ) const {

			return y + h;

		}

		float middle_x ( ) const {

			return x + w * 0.5f;

		}

		float middle_y ( ) const {

			return y + h * 0.5f;

		}

		bool contains ( float px, float py ) const {

			return px >= x && py >= y && px < x + w && py < y + h;

		}

		box inset ( float amount ) const {

			return box { x + amount, y + amount, w - amount * 2.0f, h - amount * 2.0f };

		}

		box inset ( float across, float down ) const {

			return box { x + across, y + down, w - across * 2.0f, h - down * 2.0f };

		}

	};

	enum font : uint8_t {

		FONT_BODY,
		FONT_STRONG,
		FONT_SMALL,
		FONT_SMALL_STRONG,
		FONT_CAPTION,
		FONT_FIGURE,
		FONT_HEADING,
		FONT_MONO,
		FONT_COUNT,

	};

	enum align : uint8_t {

		ALIGN_LEFT,
		ALIGN_CENTER,
		ALIGN_RIGHT,

	};

	class surface {

	public:

		~surface ( );

		bool open_window ( HWND__* window, int32_t wide, int32_t tall, float scale );

		bool open_offscreen ( int32_t wide, int32_t tall, float scale );

		void close ( );

		bool resize ( int32_t wide, int32_t tall );

		void set_scale ( float value );

		bool ready ( ) const {

			return canvas != nullptr && target != nullptr;

		}

		void* frame_waiter ( ) const {

			return waiter;

		}

		bool begin ( color background );

		bool end ( );

		bool capture ( base::list < uint8_t >& rgba );

		bool begin_layer ( float wide, float tall );

		void end_layer ( );

		void draw_layer ( float x, float y, float growth, float origin_x, float origin_y, float opacity );

		float width ( ) const {

			return static_cast < float > ( pixels_wide ) / zoom;

		}

		float height ( ) const {

			return static_cast < float > ( pixels_tall ) / zoom;

		}

		int32_t pixel_width ( ) const {

			return pixels_wide;

		}

		int32_t pixel_height ( ) const {

			return pixels_tall;

		}

		float scale ( ) const {

			return zoom;

		}

		float snap ( float value ) const;

		void fill ( box area, color tint, float radius = 0.0f );

		void outline ( box area, color tint, float stroke = 1.0f, float radius = 0.0f );

		void line ( float x0, float y0, float x1, float y1, color tint, float stroke = 1.0f );

		void rule ( float x0, float x1, float y, color tint );

		void column ( float x, float y0, float y1, color tint );

		void dot ( float x, float y, float radius, color tint );

		void ring ( float x, float y, float radius, color tint, float stroke );

		void arc ( float x, float y, float radius, float from, float sweep, color tint, float stroke );

		void polyline ( const float* points, int32_t count, color tint, float stroke );

		void polygon ( const float* points, int32_t count, color tint );

		void shaded ( const float* points, int32_t count, color top, color bottom, float y0, float y1 );

		void text ( const wchar_t* value, font role, box area, color tint, align horizontal = ALIGN_LEFT );

		float measure ( const wchar_t* value, font role, int32_t count = -1 );

		void paragraph ( const wchar_t* value, font role, box area, color tint );

		float paragraph_height ( const wchar_t* value, font role, float wide );

		void push_clip ( box area );

		void pop_clip ( );

		void push_shift ( float dx, float dy );

		void pop_shift ( );

		void push_fade ( float amount );

		void pop_fade ( );

	private:

		bool make_device ( );

		bool make_fonts ( );

		bool bind_target ( );

		void apply_shift ( );

		ID2D1SolidColorBrush* paint ( color tint );

		ID2D1PathGeometry* path ( const float* points, int32_t count, bool closed );

		static constexpr int32_t shape_room = 1024;

		enum shape_mode : uint8_t {

			SHAPE_LEFT,
			SHAPE_CENTER,
			SHAPE_RIGHT,
			SHAPE_WRAP,

		};

		struct shaped {

			uint64_t key = 0;
			IDWriteTextLayout* layout = nullptr;
			float left = 0.0f;
			float width = 0.0f;
			float height = 0.0f;
			float line_height = 0.0f;
			float baseline = 0.0f;
			uint32_t used = 0;

		};

		const shaped* shape ( const wchar_t* value, int32_t count, font role, float wide, uint8_t mode );

		void forget_shapes ( );

		HWND__* window = nullptr;
		ID3D11Device* device = nullptr;
		ID3D11DeviceContext* context = nullptr;
		IDXGISwapChain1* chain = nullptr;
		IDCompositionDevice* composition = nullptr;
		IDCompositionTarget* composition_target = nullptr;
		IDCompositionVisual* visual = nullptr;
		ID2D1Factory1* factory = nullptr;
		ID2D1Device* device2d = nullptr;
		ID2D1DeviceContext* canvas = nullptr;
		ID2D1Bitmap1* target = nullptr;
		ID2D1SolidColorBrush* brush = nullptr;
		ID2D1StrokeStyle* rounded = nullptr;
		IDWriteFactory* writer = nullptr;
		IDWriteTextFormat* formats [ FONT_COUNT ] = { };
		IDWriteInlineObject* ellipses [ FONT_COUNT ] = { };
		IDWriteTypography* figures = nullptr;
		shaped* shapes = nullptr;
		uint32_t stamp = 0;
		ID2D1Bitmap1* layer = nullptr;
		int32_t layer_wide = 0;
		int32_t layer_tall = 0;
		int32_t layer_used_wide = 0;
		int32_t layer_used_tall = 0;
		float layer_fade = 1.0f;
		void* waiter = nullptr;
		int32_t pixels_wide = 0;
		int32_t pixels_tall = 0;
		float zoom = 1.0f;
		bool drawing = false;
		float shifts [ 16 ] [ 2 ] = { };
		int32_t shift_depth = 0;
		float fades [ 16 ] = { };
		int32_t fade_depth = 0;
		float fade = 1.0f;
		int32_t clip_depth = 0;

	};

}
