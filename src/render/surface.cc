#include "render/surface.hh"
#include "base/system/com.hh"
#include "base/math.hh"
#include "base/memory.hh"
#include "base/strings.hh"
#include <windows.h>
#include <d3d11.h>
#include <dxgi1_3.h>
#include <d2d1_1.h>
#include <dwrite_1.h>
#include <dcomp.h>

namespace render {

	struct font_spec {

		int32_t family;
		DWRITE_FONT_WEIGHT weight;
		float size;
		float tracking;

	};

	static const font_spec specs [ FONT_COUNT ] = {

		{ 0, DWRITE_FONT_WEIGHT_NORMAL, 13.0f, 0.0f },
		{ 0, DWRITE_FONT_WEIGHT_SEMI_BOLD, 13.0f, 0.0f },
		{ 0, DWRITE_FONT_WEIGHT_NORMAL, 12.0f, 0.0f },
		{ 0, DWRITE_FONT_WEIGHT_SEMI_BOLD, 12.0f, 0.0f },
		{ 0, DWRITE_FONT_WEIGHT_SEMI_BOLD, 11.0f, 0.6f },
		{ 1, DWRITE_FONT_WEIGHT_SEMI_BOLD, 21.0f, 0.0f },
		{ 1, DWRITE_FONT_WEIGHT_SEMI_BOLD, 17.0f, 0.0f },
		{ 2, DWRITE_FONT_WEIGHT_NORMAL, 12.0f, 0.0f },

	};

	using base::drop;

	static D2D1_RECT_F rect_of ( box area ) {

		return D2D1_RECT_F { area.x, area.y, area.x + area.w, area.y + area.h };

	}

	surface::~surface ( ) {

		close ( );

	}

	bool surface::make_device ( ) {

		static const D3D_FEATURE_LEVEL levels [ ] = { D3D_FEATURE_LEVEL_11_1, D3D_FEATURE_LEVEL_11_0, D3D_FEATURE_LEVEL_10_1, D3D_FEATURE_LEVEL_10_0 };

		if ( FAILED ( D3D11CreateDevice ( nullptr, D3D_DRIVER_TYPE_HARDWARE, nullptr, D3D11_CREATE_DEVICE_BGRA_SUPPORT, levels, 4, D3D11_SDK_VERSION, &device, nullptr, &context ) ) ) {

			if ( FAILED ( D3D11CreateDevice ( nullptr, D3D_DRIVER_TYPE_WARP, nullptr, D3D11_CREATE_DEVICE_BGRA_SUPPORT, levels, 4, D3D11_SDK_VERSION, &device, nullptr, &context ) ) )
				return false;

		}

		IDXGIDevice1* dxgi = nullptr;

		if ( FAILED ( device->QueryInterface ( __uuidof ( IDXGIDevice1 ), reinterpret_cast < void** > ( &dxgi ) ) ) )
			return false;

		dxgi->SetMaximumFrameLatency ( 1 );

		D2D1_FACTORY_OPTIONS options = { D2D1_DEBUG_LEVEL_NONE };
		bool ok = SUCCEEDED ( D2D1CreateFactory ( D2D1_FACTORY_TYPE_SINGLE_THREADED, __uuidof ( ID2D1Factory1 ), &options, reinterpret_cast < void** > ( &factory ) ) );

		ok = ok && SUCCEEDED ( factory->CreateDevice ( dxgi, &device2d ) );
		ok = ok && SUCCEEDED ( device2d->CreateDeviceContext ( D2D1_DEVICE_CONTEXT_OPTIONS_NONE, &canvas ) );
		dxgi->Release ( );

		if ( !ok )
			return false;

		D2D1_COLOR_F black = { 0.0f, 0.0f, 0.0f, 1.0f };
		D2D1_STROKE_STYLE_PROPERTIES style = { D2D1_CAP_STYLE_ROUND, D2D1_CAP_STYLE_ROUND, D2D1_CAP_STYLE_ROUND, D2D1_LINE_JOIN_ROUND, 10.0f, D2D1_DASH_STYLE_SOLID, 0.0f };

		ok = SUCCEEDED ( canvas->CreateSolidColorBrush ( black, &brush ) );
		ok = ok && SUCCEEDED ( factory->CreateStrokeStyle ( style, nullptr, 0, &rounded ) );
		ok = ok && SUCCEEDED ( DWriteCreateFactory ( DWRITE_FACTORY_TYPE_SHARED, __uuidof ( IDWriteFactory ), reinterpret_cast < IUnknown** > ( &writer ) ) );

		if ( !ok )
			return false;

		IDWriteRenderingParams* defaults = nullptr;
		IDWriteRenderingParams* params = nullptr;

		if ( SUCCEEDED ( writer->CreateRenderingParams ( &defaults ) ) ) {

			if ( SUCCEEDED ( writer->CreateCustomRenderingParams ( defaults->GetGamma ( ), defaults->GetEnhancedContrast ( ), 0.0f, DWRITE_PIXEL_GEOMETRY_FLAT, DWRITE_RENDERING_MODE_NATURAL_SYMMETRIC, &params ) ) )
				canvas->SetTextRenderingParams ( params );

			drop ( params );
			drop ( defaults );

		}

		canvas->SetTextAntialiasMode ( D2D1_TEXT_ANTIALIAS_MODE_GRAYSCALE );
		canvas->SetAntialiasMode ( D2D1_ANTIALIAS_MODE_PER_PRIMITIVE );

		return make_fonts ( );

	}

	bool surface::make_fonts ( ) {

		IDWriteFontCollection* fonts = nullptr;

		if ( FAILED ( writer->GetSystemFontCollection ( &fonts, FALSE ) ) )
			return false;

		auto has = [ & ] ( const wchar_t* name ) {

			UINT32 index = 0;
			BOOL exists = FALSE;

			return SUCCEEDED ( fonts->FindFamilyName ( name, &index, &exists ) ) && exists;

		};

		const wchar_t* families [ 3 ] = { };

		families [ 0 ] = has ( L"Segoe UI Variable Text" ) ? L"Segoe UI Variable Text" : L"Segoe UI";
		families [ 1 ] = has ( L"Segoe UI Variable Display" ) ? L"Segoe UI Variable Display" : families [ 0 ];
		families [ 2 ] = has ( L"Cascadia Mono" ) ? L"Cascadia Mono" : L"Consolas";
		fonts->Release ( );

		DWRITE_TRIMMING trimming = { DWRITE_TRIMMING_GRANULARITY_CHARACTER, 0, 0 };

		for ( int32_t i = 0; i < FONT_COUNT; i++ ) {

			if ( FAILED ( writer->CreateTextFormat ( families [ specs [ i ].family ], nullptr, specs [ i ].weight, DWRITE_FONT_STYLE_NORMAL, DWRITE_FONT_STRETCH_NORMAL, specs [ i ].size, L"en-us", &formats [ i ] ) ) )
				return false;

			formats [ i ]->SetWordWrapping ( DWRITE_WORD_WRAPPING_NO_WRAP );

			if ( SUCCEEDED ( writer->CreateEllipsisTrimmingSign ( formats [ i ], &ellipses [ i ] ) ) )
				formats [ i ]->SetTrimming ( &trimming, ellipses [ i ] );

		}

		DWRITE_FONT_FEATURE tabular = { DWRITE_FONT_FEATURE_TAG_TABULAR_FIGURES, 1 };

		if ( SUCCEEDED ( writer->CreateTypography ( &figures ) ) )
			figures->AddFontFeature ( tabular );

		shapes = new shaped [ shape_room ];

		return true;

	}

	bool surface::bind_target ( ) {

		canvas->SetTarget ( nullptr );
		drop ( target );

		D2D1_BITMAP_PROPERTIES1 properties = { };

		properties.pixelFormat = { DXGI_FORMAT_B8G8R8A8_UNORM, chain ? D2D1_ALPHA_MODE_IGNORE : D2D1_ALPHA_MODE_PREMULTIPLIED };
		properties.dpiX = 96.0f * zoom;
		properties.dpiY = 96.0f * zoom;
		properties.bitmapOptions = chain ? D2D1_BITMAP_OPTIONS_TARGET | D2D1_BITMAP_OPTIONS_CANNOT_DRAW : D2D1_BITMAP_OPTIONS_TARGET;

		if ( chain ) {

			IDXGISurface* buffer = nullptr;

			if ( FAILED ( chain->GetBuffer ( 0, __uuidof ( IDXGISurface ), reinterpret_cast < void** > ( &buffer ) ) ) )
				return false;

			HRESULT result = canvas->CreateBitmapFromDxgiSurface ( buffer, &properties, &target );

			buffer->Release ( );

			if ( FAILED ( result ) )
				return false;

		} else {

			D2D1_SIZE_U size = { static_cast < UINT32 > ( pixels_wide ), static_cast < UINT32 > ( pixels_tall ) };

			if ( FAILED ( canvas->CreateBitmap ( size, nullptr, 0, &properties, &target ) ) )
				return false;

		}

		canvas->SetTarget ( target );
		canvas->SetDpi ( 96.0f * zoom, 96.0f * zoom );

		return true;

	}

	bool surface::open_window ( HWND__* handle, int32_t wide, int32_t tall, float scale ) {

		close ( );

		window = handle;
		pixels_wide = base::max ( wide, 1 );
		pixels_tall = base::max ( tall, 1 );
		zoom = scale;

		if ( !make_device ( ) )
			return false;

		IDXGIDevice* dxgi = nullptr;
		IDXGIAdapter* adapter = nullptr;
		IDXGIFactory2* dxgi_factory = nullptr;
		bool ok = SUCCEEDED ( device->QueryInterface ( __uuidof ( IDXGIDevice ), reinterpret_cast < void** > ( &dxgi ) ) );

		ok = ok && SUCCEEDED ( dxgi->GetAdapter ( &adapter ) );
		ok = ok && SUCCEEDED ( adapter->GetParent ( __uuidof ( IDXGIFactory2 ), reinterpret_cast < void** > ( &dxgi_factory ) ) );

		DXGI_SWAP_CHAIN_DESC1 description = { };

		description.Width = static_cast < UINT > ( pixels_wide );
		description.Height = static_cast < UINT > ( pixels_tall );
		description.Format = DXGI_FORMAT_B8G8R8A8_UNORM;
		description.SampleDesc.Count = 1;
		description.BufferUsage = DXGI_USAGE_RENDER_TARGET_OUTPUT;
		description.BufferCount = 2;
		description.SwapEffect = DXGI_SWAP_EFFECT_FLIP_SEQUENTIAL;
		description.AlphaMode = DXGI_ALPHA_MODE_IGNORE;
		description.Scaling = DXGI_SCALING_STRETCH;
		description.Flags = DXGI_SWAP_CHAIN_FLAG_FRAME_LATENCY_WAITABLE_OBJECT;

		ok = ok && SUCCEEDED ( dxgi_factory->CreateSwapChainForComposition ( device, &description, nullptr, &chain ) );

		IDXGISwapChain2* paced = nullptr;

		if ( ok && SUCCEEDED ( chain->QueryInterface ( __uuidof ( IDXGISwapChain2 ), reinterpret_cast < void** > ( &paced ) ) ) ) {

			paced->SetMaximumFrameLatency ( 1 );
			waiter = paced->GetFrameLatencyWaitableObject ( );
			paced->Release ( );

		}
		ok = ok && SUCCEEDED ( DCompositionCreateDevice ( dxgi, __uuidof ( IDCompositionDevice ), reinterpret_cast < void** > ( &composition ) ) );
		ok = ok && SUCCEEDED ( composition->CreateTargetForHwnd ( handle, TRUE, &composition_target ) );
		ok = ok && SUCCEEDED ( composition->CreateVisual ( &visual ) );
		ok = ok && SUCCEEDED ( visual->SetContent ( chain ) );
		ok = ok && SUCCEEDED ( composition_target->SetRoot ( visual ) );
		ok = ok && SUCCEEDED ( composition->Commit ( ) );

		drop ( dxgi_factory );
		drop ( adapter );
		drop ( dxgi );

		return ok && bind_target ( );

	}

	bool surface::open_offscreen ( int32_t wide, int32_t tall, float scale ) {

		close ( );

		pixels_wide = base::max ( wide, 1 );
		pixels_tall = base::max ( tall, 1 );
		zoom = scale;

		return make_device ( ) && bind_target ( );

	}

	void surface::close ( ) {

		if ( canvas )
			canvas->SetTarget ( nullptr );

		for ( int32_t i = 0; i < FONT_COUNT; i++ ) {

			drop ( ellipses [ i ] );
			drop ( formats [ i ] );

		}

		if ( waiter )
			CloseHandle ( waiter );

		waiter = nullptr;
		forget_shapes ( );
		delete [ ] shapes;
		shapes = nullptr;
		drop ( layer );
		layer_wide = 0;
		layer_tall = 0;
		drop ( figures );
		drop ( writer );
		drop ( rounded );
		drop ( brush );
		drop ( target );
		drop ( canvas );
		drop ( device2d );
		drop ( factory );
		drop ( visual );
		drop ( composition_target );
		drop ( composition );
		drop ( chain );
		drop ( context );
		drop ( device );
		window = nullptr;

	}

	bool surface::resize ( int32_t wide, int32_t tall ) {

		wide = base::max ( wide, 1 );
		tall = base::max ( tall, 1 );

		if ( !canvas || ( wide == pixels_wide && tall == pixels_tall ) )
			return canvas != nullptr;

		pixels_wide = wide;
		pixels_tall = tall;
		canvas->SetTarget ( nullptr );
		drop ( target );

		if ( chain && FAILED ( chain->ResizeBuffers ( 0, static_cast < UINT > ( wide ), static_cast < UINT > ( tall ), DXGI_FORMAT_UNKNOWN, DXGI_SWAP_CHAIN_FLAG_FRAME_LATENCY_WAITABLE_OBJECT ) ) )
			return false;

		return bind_target ( );

	}

	void surface::set_scale ( float value ) {

		if ( value == zoom || !canvas )
			return;

		zoom = value;
		drop ( layer );
		layer_wide = 0;
		layer_tall = 0;
		bind_target ( );

	}

	bool surface::begin ( color background ) {

		if ( !canvas || !target || drawing )
			return false;

		D2D1_COLOR_F clear = { background.r, background.g, background.b, background.a };

		if ( waiter )
			WaitForSingleObjectEx ( waiter, 100, TRUE );

		canvas->BeginDraw ( );
		drawing = true;
		stamp++;
		shift_depth = 0;
		fade_depth = 0;
		fade = 1.0f;
		clip_depth = 0;
		apply_shift ( );
		canvas->Clear ( clear );

		return true;

	}

	bool surface::end ( ) {

		if ( !drawing )
			return false;

		while ( clip_depth )
			pop_clip ( );

		HRESULT result = canvas->EndDraw ( );

		drawing = false;

		if ( chain && SUCCEEDED ( result ) )
			result = chain->Present ( 1, 0 );

		return SUCCEEDED ( result );

	}

	bool surface::capture ( base::list < uint8_t >& rgba ) {

		if ( !canvas || !target || drawing )
			return false;

		D2D1_BITMAP_PROPERTIES1 properties = { };
		ID2D1Bitmap1* copy = nullptr;
		D2D1_SIZE_U size = { static_cast < UINT32 > ( pixels_wide ), static_cast < UINT32 > ( pixels_tall ) };

		properties.pixelFormat = { DXGI_FORMAT_B8G8R8A8_UNORM, D2D1_ALPHA_MODE_PREMULTIPLIED };
		properties.dpiX = 96.0f * zoom;
		properties.dpiY = 96.0f * zoom;
		properties.bitmapOptions = D2D1_BITMAP_OPTIONS_CPU_READ | D2D1_BITMAP_OPTIONS_CANNOT_DRAW;

		if ( FAILED ( canvas->CreateBitmap ( size, nullptr, 0, &properties, &copy ) ) )
			return false;

		D2D1_MAPPED_RECT mapped = { };
		bool ok = SUCCEEDED ( copy->CopyFromBitmap ( nullptr, target, nullptr ) ) && SUCCEEDED ( copy->Map ( D2D1_MAP_OPTIONS_READ, &mapped ) );

		if ( ok && rgba.set_size ( pixels_wide * pixels_tall * 4 ) ) {

			for ( int32_t y = 0; y < pixels_tall; y++ ) {

				const uint8_t* row = mapped.bits + static_cast < size_t > ( y ) * mapped.pitch;
				uint8_t* out = rgba.data ( ) + static_cast < size_t > ( y ) * pixels_wide * 4;

				for ( int32_t x = 0; x < pixels_wide; x++ ) {

					uint32_t alpha = row [ x * 4 + 3 ];

					for ( int32_t channel = 0; channel < 3; channel++ ) {

						uint32_t value = row [ x * 4 + 2 - channel ];

						out [ x * 4 + channel ] = static_cast < uint8_t > ( alpha ? base::min ( value * 255 / alpha, 255u ) : 0 );

					}

					out [ x * 4 + 3 ] = static_cast < uint8_t > ( alpha );

				}

			}

		}

		if ( ok )
			copy->Unmap ( );

		copy->Release ( );

		return ok;

	}

	float surface::snap ( float value ) const {

		return base::round ( value * zoom ) / zoom;

	}

	ID2D1SolidColorBrush* surface::paint ( color tint ) {

		D2D1_COLOR_F value = { tint.r, tint.g, tint.b, tint.a * fade };

		brush->SetColor ( value );

		return brush;

	}

	void surface::apply_shift ( ) {

		D2D1_MATRIX_3X2_F matrix = { };

		matrix._11 = 1.0f;
		matrix._22 = 1.0f;

		if ( shift_depth ) {

			matrix._31 = shifts [ shift_depth - 1 ] [ 0 ];
			matrix._32 = shifts [ shift_depth - 1 ] [ 1 ];

		}

		canvas->SetTransform ( matrix );

	}

	void surface::fill ( box area, color tint, float radius ) {

		if ( radius <= 0.0f ) {

			canvas->FillRectangle ( rect_of ( area ), paint ( tint ) );

			return;

		}

		D2D1_ROUNDED_RECT shape = { rect_of ( area ), radius, radius };

		canvas->FillRoundedRectangle ( shape, paint ( tint ) );

	}

	void surface::outline ( box area, color tint, float stroke, float radius ) {

		box inner = area.inset ( stroke * 0.5f );

		if ( radius <= 0.0f ) {

			canvas->DrawRectangle ( rect_of ( inner ), paint ( tint ), stroke );

			return;

		}

		D2D1_ROUNDED_RECT shape = { rect_of ( inner ), radius - stroke * 0.5f, radius - stroke * 0.5f };

		canvas->DrawRoundedRectangle ( shape, paint ( tint ), stroke );

	}

	void surface::line ( float x0, float y0, float x1, float y1, color tint, float stroke ) {

		canvas->DrawLine ( D2D1_POINT_2F { x0, y0 }, D2D1_POINT_2F { x1, y1 }, paint ( tint ), stroke, rounded );

	}

	void surface::rule ( float x0, float x1, float y, color tint ) {

		float top = snap ( y );

		canvas->FillRectangle ( D2D1_RECT_F { snap ( x0 ), top, snap ( x1 ), top + 1.0f / zoom }, paint ( tint ) );

	}

	void surface::column ( float x, float y0, float y1, color tint ) {

		float left = snap ( x );

		canvas->FillRectangle ( D2D1_RECT_F { left, snap ( y0 ), left + 1.0f / zoom, snap ( y1 ) }, paint ( tint ) );

	}

	void surface::dot ( float x, float y, float radius, color tint ) {

		D2D1_ELLIPSE shape = { D2D1_POINT_2F { x, y }, radius, radius };

		canvas->FillEllipse ( shape, paint ( tint ) );

	}

	void surface::ring ( float x, float y, float radius, color tint, float stroke ) {

		D2D1_ELLIPSE shape = { D2D1_POINT_2F { x, y }, radius, radius };

		canvas->DrawEllipse ( shape, paint ( tint ), stroke );

	}

	void surface::arc ( float x, float y, float radius, float from, float sweep, color tint, float stroke ) {

		ID2D1PathGeometry* geometry = nullptr;
		ID2D1GeometrySink* sink = nullptr;

		if ( FAILED ( factory->CreatePathGeometry ( &geometry ) ) )
			return;

		if ( SUCCEEDED ( geometry->Open ( &sink ) ) ) {

			float to = from + sweep;
			D2D1_ARC_SEGMENT segment = { D2D1_POINT_2F { x + base::cos ( to ) * radius, y + base::sin ( to ) * radius }, D2D1_SIZE_F { radius, radius }, 0.0f, sweep >= 0.0f ? D2D1_SWEEP_DIRECTION_CLOCKWISE : D2D1_SWEEP_DIRECTION_COUNTER_CLOCKWISE, base::abs ( sweep ) > base::pi ? D2D1_ARC_SIZE_LARGE : D2D1_ARC_SIZE_SMALL };

			sink->BeginFigure ( D2D1_POINT_2F { x + base::cos ( from ) * radius, y + base::sin ( from ) * radius }, D2D1_FIGURE_BEGIN_HOLLOW );
			sink->AddArc ( segment );
			sink->EndFigure ( D2D1_FIGURE_END_OPEN );
			sink->Close ( );
			sink->Release ( );
			canvas->DrawGeometry ( geometry, paint ( tint ), stroke, rounded );

		}

		geometry->Release ( );

	}

	ID2D1PathGeometry* surface::path ( const float* points, int32_t count, bool closed ) {

		ID2D1PathGeometry* geometry = nullptr;
		ID2D1GeometrySink* sink = nullptr;

		if ( count < 2 || FAILED ( factory->CreatePathGeometry ( &geometry ) ) )
			return nullptr;

		if ( FAILED ( geometry->Open ( &sink ) ) ) {

			geometry->Release ( );

			return nullptr;

		}

		sink->SetFillMode ( D2D1_FILL_MODE_WINDING );
		sink->BeginFigure ( D2D1_POINT_2F { points [ 0 ], points [ 1 ] }, closed ? D2D1_FIGURE_BEGIN_FILLED : D2D1_FIGURE_BEGIN_HOLLOW );

		for ( int32_t i = 1; i < count; i++ )
			sink->AddLine ( D2D1_POINT_2F { points [ i * 2 ], points [ i * 2 + 1 ] } );

		sink->EndFigure ( closed ? D2D1_FIGURE_END_CLOSED : D2D1_FIGURE_END_OPEN );
		sink->Close ( );
		sink->Release ( );

		return geometry;

	}

	void surface::polyline ( const float* points, int32_t count, color tint, float stroke ) {

		ID2D1PathGeometry* geometry = path ( points, count, false );

		if ( !geometry )
			return;

		canvas->DrawGeometry ( geometry, paint ( tint ), stroke, rounded );
		geometry->Release ( );

	}

	void surface::polygon ( const float* points, int32_t count, color tint ) {

		ID2D1PathGeometry* geometry = path ( points, count, true );

		if ( !geometry )
			return;

		canvas->FillGeometry ( geometry, paint ( tint ) );
		geometry->Release ( );

	}

	void surface::shaded ( const float* points, int32_t count, color top, color bottom, float y0, float y1 ) {

		ID2D1PathGeometry* geometry = path ( points, count, true );
		ID2D1GradientStopCollection* stops = nullptr;
		ID2D1LinearGradientBrush* gradient = nullptr;

		if ( !geometry )
			return;

		D2D1_GRADIENT_STOP ends [ 2 ] = {

			{ 0.0f, D2D1_COLOR_F { top.r, top.g, top.b, top.a * fade } },
			{ 1.0f, D2D1_COLOR_F { bottom.r, bottom.g, bottom.b, bottom.a * fade } },

		};

		D2D1_LINEAR_GRADIENT_BRUSH_PROPERTIES direction = { D2D1_POINT_2F { 0.0f, y0 }, D2D1_POINT_2F { 0.0f, y1 } };

		if ( SUCCEEDED ( canvas->CreateGradientStopCollection ( ends, 2, &stops ) ) && SUCCEEDED ( canvas->CreateLinearGradientBrush ( direction, stops, &gradient ) ) )
			canvas->FillGeometry ( geometry, gradient );

		drop ( gradient );
		drop ( stops );
		geometry->Release ( );

	}

	const surface::shaped* surface::shape ( const wchar_t* value, int32_t count, font role, float wide, uint8_t mode ) {

		uint64_t key = 1469598103934665603ull;
		uint32_t width_bits = __builtin_bit_cast ( uint32_t, wide );

		auto mix_in = [ & ] ( uint32_t part ) {

			key = ( key ^ part ) * 1099511628211ull;

		};

		for ( int32_t i = 0; i < count; i++ )
			mix_in ( value [ i ] );

		mix_in ( static_cast < uint32_t > ( count ) );
		mix_in ( role );
		mix_in ( mode );
		mix_in ( width_bits );
		key |= 1;

		uint32_t home = static_cast < uint32_t > ( key >> 32 ) & ( shape_room - 1 );
		shaped* spare = nullptr;

		for ( uint32_t probe = 0; probe < 8; probe++ ) {

			shaped& slot = shapes [ ( home + probe ) & ( shape_room - 1 ) ];

			if ( slot.key == key && slot.layout ) {

				slot.used = stamp;

				return &slot;

			}

			if ( !spare || !slot.layout || ( spare->layout && slot.used < spare->used ) )
				spare = &slot;

		}

		IDWriteTextLayout* item = nullptr;

		if ( FAILED ( writer->CreateTextLayout ( value, static_cast < UINT32 > ( count ), formats [ role ], wide, 4096.0f, &item ) ) )
			return nullptr;

		DWRITE_TEXT_RANGE range = { 0, static_cast < UINT32 > ( count ) };

		if ( figures )
			item->SetTypography ( figures, range );

		if ( specs [ role ].tracking > 0.0f ) {

			IDWriteTextLayout1* spaced = nullptr;

			if ( SUCCEEDED ( item->QueryInterface ( __uuidof ( IDWriteTextLayout1 ), reinterpret_cast < void** > ( &spaced ) ) ) ) {

				spaced->SetCharacterSpacing ( 0.0f, specs [ role ].tracking, 0.0f, range );
				spaced->Release ( );

			}

		}

		if ( mode == SHAPE_WRAP )
			item->SetWordWrapping ( DWRITE_WORD_WRAPPING_WRAP );
		else if ( mode == SHAPE_CENTER )
			item->SetTextAlignment ( DWRITE_TEXT_ALIGNMENT_CENTER );
		else if ( mode == SHAPE_RIGHT )
			item->SetTextAlignment ( DWRITE_TEXT_ALIGNMENT_TRAILING );

		DWRITE_TEXT_METRICS metrics = { };
		DWRITE_LINE_METRICS line = { };
		UINT32 lines = 0;

		item->GetMetrics ( &metrics );
		item->GetLineMetrics ( &line, 1, &lines );
		drop ( spare->layout );
		spare->key = key;
		spare->layout = item;
		spare->left = metrics.left;
		spare->width = metrics.widthIncludingTrailingWhitespace;
		spare->height = metrics.height;
		spare->line_height = line.height;
		spare->baseline = line.baseline;
		spare->used = stamp;

		return spare;

	}

	void surface::forget_shapes ( ) {

		for ( int32_t i = 0; shapes && i < shape_room; i++ ) {

			drop ( shapes [ i ].layout );
			shapes [ i ].key = 0;

		}

	}

	void surface::text ( const wchar_t* value, font role, box area, color tint, align horizontal ) {

		int32_t count = base::length ( value );

		if ( !count || !canvas )
			return;

		uint8_t mode = horizontal == ALIGN_CENTER ? SHAPE_CENTER : ( horizontal == ALIGN_RIGHT ? SHAPE_RIGHT : SHAPE_LEFT );
		const shaped* item = shape ( value, count, role, base::max ( area.w, 1.0f ), mode );

		if ( !item )
			return;

		float top = area.y + ( area.h - item->line_height ) * 0.5f;
		float baseline = top + item->baseline;
		float left = area.x + item->left;
		D2D1_POINT_2F origin = { area.x + snap ( left ) - left, top + snap ( baseline ) - baseline };

		canvas->DrawTextLayout ( origin, item->layout, paint ( tint ), D2D1_DRAW_TEXT_OPTIONS_NO_SNAP );

	}

	float surface::measure ( const wchar_t* value, font role, int32_t count ) {

		if ( count < 0 )
			count = base::length ( value );

		if ( !count || !writer )
			return 0.0f;

		const shaped* item = shape ( value, count, role, 100000.0f, SHAPE_LEFT );

		return item ? item->width : 0.0f;

	}

	void surface::paragraph ( const wchar_t* value, font role, box area, color tint ) {

		int32_t count = base::length ( value );

		if ( !count || !canvas )
			return;

		const shaped* item = shape ( value, count, role, base::max ( area.w, 1.0f ), SHAPE_WRAP );

		if ( !item )
			return;

		float baseline = area.y + item->baseline;
		D2D1_POINT_2F origin = { snap ( area.x ), area.y + snap ( baseline ) - baseline };

		canvas->DrawTextLayout ( origin, item->layout, paint ( tint ), D2D1_DRAW_TEXT_OPTIONS_NO_SNAP );

	}

	float surface::paragraph_height ( const wchar_t* value, font role, float wide ) {

		int32_t count = base::length ( value );

		if ( !count || !writer )
			return 0.0f;

		const shaped* item = shape ( value, count, role, base::max ( wide, 1.0f ), SHAPE_WRAP );

		return item ? item->height : 0.0f;

	}

	bool surface::begin_layer ( float wide, float tall ) {

		int32_t across = static_cast < int32_t > ( base::ceil ( wide * zoom ) );
		int32_t down = static_cast < int32_t > ( base::ceil ( tall * zoom ) );

		if ( !drawing || across <= 0 || down <= 0 )
			return false;

		if ( across > layer_wide || down > layer_tall ) {

			D2D1_BITMAP_PROPERTIES1 properties = { };
			D2D1_SIZE_U size = { static_cast < UINT32 > ( base::max ( across, layer_wide ) ), static_cast < UINT32 > ( base::max ( down, layer_tall ) ) };

			properties.pixelFormat = { DXGI_FORMAT_B8G8R8A8_UNORM, D2D1_ALPHA_MODE_PREMULTIPLIED };
			properties.dpiX = 96.0f * zoom;
			properties.dpiY = 96.0f * zoom;
			properties.bitmapOptions = D2D1_BITMAP_OPTIONS_TARGET;
			drop ( layer );

			if ( FAILED ( canvas->CreateBitmap ( size, nullptr, 0, &properties, &layer ) ) )
				return false;

			layer_wide = static_cast < int32_t > ( size.width );
			layer_tall = static_cast < int32_t > ( size.height );

		}

		D2D1_MATRIX_3X2_F identity = { };
		D2D1_COLOR_F clear = { };

		identity._11 = 1.0f;
		identity._22 = 1.0f;
		layer_used_wide = across;
		layer_used_tall = down;
		layer_fade = fade;
		fade = 1.0f;
		canvas->SetTarget ( layer );
		canvas->SetTransform ( identity );
		canvas->Clear ( clear );

		return true;

	}

	void surface::end_layer ( ) {

		canvas->SetTarget ( target );
		fade = layer_fade;
		apply_shift ( );

	}

	void surface::draw_layer ( float x, float y, float growth, float origin_x, float origin_y, float opacity ) {

		if ( !layer || !layer_used_wide )
			return;

		float wide = static_cast < float > ( layer_used_wide ) / zoom;
		float tall = static_cast < float > ( layer_used_tall ) / zoom;
		float left = snap ( x );
		float top = snap ( y );
		D2D1_RECT_F source = { 0.0f, 0.0f, wide, tall };
		D2D1_RECT_F place = { left, top, 0.0f, 0.0f };

		if ( growth != 1.0f ) {

			place.left = origin_x + ( left - origin_x ) * growth;
			place.top = origin_y + ( top - origin_y ) * growth;

		}

		place.right = place.left + wide * growth;
		place.bottom = place.top + tall * growth;
		canvas->DrawBitmap ( layer, &place, opacity * fade, D2D1_INTERPOLATION_MODE_LINEAR, &source, nullptr );

	}

	void surface::push_clip ( box area ) {

		canvas->PushAxisAlignedClip ( rect_of ( area ), D2D1_ANTIALIAS_MODE_ALIASED );
		clip_depth++;

	}

	void surface::pop_clip ( ) {

		if ( !clip_depth )
			return;

		canvas->PopAxisAlignedClip ( );
		clip_depth--;

	}

	void surface::push_shift ( float dx, float dy ) {

		if ( shift_depth >= 16 )
			return;

		float x = shift_depth ? shifts [ shift_depth - 1 ] [ 0 ] : 0.0f;
		float y = shift_depth ? shifts [ shift_depth - 1 ] [ 1 ] : 0.0f;

		shifts [ shift_depth ] [ 0 ] = snap ( x + dx );
		shifts [ shift_depth ] [ 1 ] = y + dy;
		shift_depth++;
		apply_shift ( );

	}

	void surface::pop_shift ( ) {

		if ( !shift_depth )
			return;

		shift_depth--;
		apply_shift ( );

	}

	void surface::push_fade ( float amount ) {

		if ( fade_depth >= 16 )
			return;

		fades [ fade_depth++ ] = fade;
		fade *= amount;

	}

	void surface::pop_fade ( ) {

		if ( fade_depth )
			fade = fades [ --fade_depth ];

	}

}
