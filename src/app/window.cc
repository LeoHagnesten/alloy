#include "app/commands.hh"
#include "app/session.hh"
#include "app/settings.hh"
#include "ui/shell.hh"
#include "render/surface.hh"
#include "base/clipboard.hh"
#include "base/clock.hh"
#include "base/console.hh"
#include "base/files.hh"
#include "base/png.hh"
#include "base/system.hh"
#include "base/text.hh"
#include <windows.h>
#include <dwmapi.h>

namespace app {

	static constexpr float default_width = 1240.0f;
	static constexpr float default_height = 800.0f;
	static constexpr float minimum_width = 980.0f;
	static constexpr float minimum_height = 640.0f;

	struct host {

		HWND window = nullptr;
		render::surface surface;
		session* run = nullptr;
		ui::preferences settings;
		ui::shell* view = nullptr;
		float scale = 1.0f;
		bool light = false;
		bool dirty = true;
		bool tracking = false;
		bool frame_tracking = false;
		double save_at = 0.0;
		base::list < double >* frames = nullptr;

	};

	static float dips ( host& self, LPARAM lparam, bool vertical ) {

		int32_t value = vertical ? static_cast < int16_t > ( HIWORD ( lparam ) ) : static_cast < int16_t > ( LOWORD ( lparam ) );

		return static_cast < float > ( value ) / self.scale;

	}

	static void apply_theme ( host& self ) {

		bool light = self.settings.theme == ui::THEME_SYSTEM ? base::system_prefers_light ( ) : self.settings.theme == ui::THEME_LIGHT;
		BOOL dark = light ? FALSE : TRUE;

		self.light = light;
		self.view->set_light ( light );
		DwmSetWindowAttribute ( self.window, DWMWA_USE_IMMERSIVE_DARK_MODE, &dark, sizeof ( dark ) );
		self.dirty = true;

	}

	static void handle ( host& self, ui::command command ) {

		switch ( command ) {

			case ui::COMMAND_TOGGLE:
				if ( self.run->running ( ) )
					self.run->stop ( );
				else
					self.run->start ( self.settings );

				break;

			case ui::COMMAND_PREFERENCES:
				self.run->apply ( self.settings );
				apply_theme ( self );
				self.save_at = base::seconds ( ) + 0.6;
				break;

			case ui::COMMAND_RESTART:
				if ( self.run->running ( ) )
					self.run->start ( self.settings );

				self.save_at = base::seconds ( ) + 0.6;
				break;

			case ui::COMMAND_CLEAR_LOG:
				self.run->clear_log ( );
				break;

			default:
				break;

		}

		self.dirty = true;

	}

	static bool on_screen ( host& self ) {

		BOOL cloaked = FALSE;

		if ( IsIconic ( self.window ) || !IsWindowVisible ( self.window ) )
			return false;

		DwmGetWindowAttribute ( self.window, DWMWA_CLOAKED, &cloaked, sizeof ( cloaked ) );

		return !cloaked;

	}

	static void paint ( host& self, bool first = false ) {

		if ( !first && !on_screen ( self ) )
			return;

		double now = base::seconds ( );

		self.run->edit ( ).light = self.light;

		if ( self.frames )
			self.frames->push ( now );

		if ( self.view->paint ( self.surface, self.run->view ( ), now ) )
			return;

		RECT client;

		GetClientRect ( self.window, &client );
		self.surface.open_window ( self.window, client.right, client.bottom, self.scale );

	}

	static int32_t frame_thickness ( host& self ) {

		UINT dpi = GetDpiForWindow ( self.window );

		return GetSystemMetricsForDpi ( SM_CYFRAME, dpi ) + GetSystemMetricsForDpi ( SM_CXPADDEDBORDER, dpi );

	}

	static LRESULT hit_test ( host& self, HWND window, WPARAM wparam, LPARAM lparam ) {

		LRESULT hit = DefWindowProcW ( window, WM_NCHITTEST, wparam, lparam );

		if ( hit != HTCLIENT )
			return hit;

		POINT point = { static_cast < int16_t > ( LOWORD ( lparam ) ), static_cast < int16_t > ( HIWORD ( lparam ) ) };
		RECT client;
		int32_t border = frame_thickness ( self );

		ScreenToClient ( window, &point );
		GetClientRect ( window, &client );

		if ( !IsZoomed ( window ) && point.y < border ) {

			if ( point.x < border * 2 )
				return HTTOPLEFT;

			if ( point.x >= client.right - border * 2 )
				return HTTOPRIGHT;

			return HTTOP;

		}

		switch ( self.view->part_at ( static_cast < float > ( point.x ) / self.scale, static_cast < float > ( point.y ) / self.scale ) ) {

			case ui::PART_CAPTION:
				return HTCAPTION;

			case ui::PART_MINIMIZE:
				return HTMINBUTTON;

			case ui::PART_MAXIMIZE:
				return HTMAXBUTTON;

			case ui::PART_CLOSE:
				return HTCLOSE;

			default:
				return HTCLIENT;

		}

	}

	static ui::frame_part part_of ( WPARAM code ) {

		if ( code == HTMINBUTTON )
			return ui::PART_MINIMIZE;

		if ( code == HTMAXBUTTON )
			return ui::PART_MAXIMIZE;

		if ( code == HTCLOSE )
			return ui::PART_CLOSE;

		return code == HTCAPTION ? ui::PART_CAPTION : ui::PART_CLIENT;

	}

	static LRESULT __stdcall procedure ( HWND window, UINT message, WPARAM wparam, LPARAM lparam ) {

		if ( message == WM_NCCREATE ) {

			CREATESTRUCTW* creation = reinterpret_cast < CREATESTRUCTW* > ( lparam );

			SetWindowLongPtrW ( window, GWLP_USERDATA, reinterpret_cast < LONG_PTR > ( creation->lpCreateParams ) );
			static_cast < host* > ( creation->lpCreateParams )->window = window;

		}

		host* self = reinterpret_cast < host* > ( GetWindowLongPtrW ( window, GWLP_USERDATA ) );

		if ( !self || !self->view )
			return DefWindowProcW ( window, message, wparam, lparam );

		double now = base::seconds ( );
		bool shift = GetKeyState ( VK_SHIFT ) < 0;
		bool control = GetKeyState ( VK_CONTROL ) < 0;

		switch ( message ) {

			case WM_NCCALCSIZE: {

				if ( !wparam )
					break;

				NCCALCSIZE_PARAMS* params = reinterpret_cast < NCCALCSIZE_PARAMS* > ( lparam );
				LONG top = params->rgrc [ 0 ].top;

				DefWindowProcW ( window, message, wparam, lparam );
				params->rgrc [ 0 ].top = top;

				if ( IsZoomed ( window ) )
					params->rgrc [ 0 ].top += frame_thickness ( *self );

				return 0;

			}

			case WM_NCHITTEST:
				return hit_test ( *self, window, wparam, lparam );

			case WM_NCMOUSEMOVE: {

				ui::frame_part part = part_of ( wparam );

				if ( !self->frame_tracking ) {

					TRACKMOUSEEVENT track = { sizeof ( track ), TME_LEAVE | TME_NONCLIENT, window, 0 };

					TrackMouseEvent ( &track );
					self->frame_tracking = true;

				}

				self->view->hover_frame ( part, now );
				self->view->mouse_move ( -1000.0f, -1000.0f, now );
				self->dirty = true;

				if ( part == ui::PART_MINIMIZE || part == ui::PART_MAXIMIZE || part == ui::PART_CLOSE )
					return 0;

				break;

			}

			case WM_NCMOUSELEAVE:
				self->frame_tracking = false;
				self->view->hover_frame ( ui::PART_CLIENT, now );
				self->dirty = true;
				break;

			case WM_NCLBUTTONDOWN:
			case WM_NCLBUTTONDBLCLK: {

				ui::frame_part part = part_of ( wparam );

				if ( part == ui::PART_MINIMIZE || part == ui::PART_MAXIMIZE || part == ui::PART_CLOSE ) {

					self->view->press_frame ( part );
					self->dirty = true;

					return 0;

				}

				handle ( *self, self->view->blur ( now ) );
				break;

			}

			case WM_NCLBUTTONUP: {

				ui::frame_part part = part_of ( wparam );

				self->view->press_frame ( ui::PART_CLIENT );
				self->dirty = true;

				if ( part == ui::PART_MINIMIZE ) {

					ShowWindow ( window, SW_MINIMIZE );

					return 0;

				}

				if ( part == ui::PART_MAXIMIZE ) {

					ShowWindow ( window, IsZoomed ( window ) ? SW_RESTORE : SW_MAXIMIZE );

					return 0;

				}

				if ( part == ui::PART_CLOSE ) {

					PostMessageW ( window, WM_CLOSE, 0, 0 );

					return 0;

				}

				break;

			}

			case WM_MOUSEMOVE:
				if ( !self->tracking ) {

					TRACKMOUSEEVENT track = { sizeof ( track ), TME_LEAVE, window, 0 };

					TrackMouseEvent ( &track );
					self->tracking = true;

				}

				self->view->hover_frame ( ui::PART_CLIENT, now );
				handle ( *self, self->view->mouse_move ( dips ( *self, lparam, false ), dips ( *self, lparam, true ), now ) );
				return 0;

			case WM_MOUSELEAVE:
				self->tracking = false;
				self->view->mouse_leave ( now );
				self->dirty = true;
				return 0;

			case WM_LBUTTONDOWN:
			case WM_LBUTTONDBLCLK:
				SetCapture ( window );
				handle ( *self, self->view->mouse_down ( dips ( *self, lparam, false ), dips ( *self, lparam, true ), shift, now ) );
				return 0;

			case WM_LBUTTONUP:
				ReleaseCapture ( );
				handle ( *self, self->view->mouse_up ( dips ( *self, lparam, false ), dips ( *self, lparam, true ), now ) );
				return 0;

			case WM_MOUSEWHEEL: {

				POINT point = { static_cast < int16_t > ( LOWORD ( lparam ) ), static_cast < int16_t > ( HIWORD ( lparam ) ) };

				ScreenToClient ( window, &point );
				self->view->wheel ( static_cast < float > ( point.x ) / self->scale, static_cast < float > ( point.y ) / self->scale, static_cast < float > ( static_cast < int16_t > ( HIWORD ( wparam ) ) ), now );
				self->dirty = true;

				return 0;

			}

			case WM_SETCURSOR:
				if ( LOWORD ( lparam ) == HTCLIENT ) {

					POINT point;

					GetCursorPos ( &point );
					ScreenToClient ( window, &point );
					SetCursor ( LoadCursorW ( nullptr, self->view->over_text ( static_cast < float > ( point.x ) / self->scale, static_cast < float > ( point.y ) / self->scale ) ? IDC_IBEAM : IDC_ARROW ) );

					return TRUE;

				}

				break;

			case WM_KEYDOWN: {

				if ( control && ( wparam == 'V' || wparam == 'C' || wparam == 'X' ) && self->view->editing ( ) ) {

					wchar_t text [ 256 ];

					if ( wparam == 'V' && base::read_clipboard ( window, text, 256 ) )
						handle ( *self, self->view->paste ( text, now ) );
					else if ( wparam != 'V' && self->view->copy_selection ( text, 256, wparam == 'X', now ) )
						base::write_clipboard ( window, text );

					self->dirty = true;

					return 0;

				}

				handle ( *self, self->view->key ( static_cast < uint32_t > ( wparam ), shift, control, now ) );
				return 0;

			}

			case WM_CHAR:
				handle ( *self, self->view->character ( static_cast < wchar_t > ( wparam ), now ) );
				return 0;

			case WM_ACTIVATE:
				if ( LOWORD ( wparam ) == WA_INACTIVE )
					handle ( *self, self->view->blur ( now ) );

				self->dirty = true;
				break;

			case WM_SIZE: {

				self->view->set_maximized ( wparam == SIZE_MAXIMIZED );

				if ( wparam != SIZE_MINIMIZED ) {

					self->view->set_size ( LOWORD ( lparam ) / self->scale, HIWORD ( lparam ) / self->scale );
					self->surface.resize ( LOWORD ( lparam ), HIWORD ( lparam ) );
					paint ( *self );

				}

				return 0;

			}

			case WM_DPICHANGED: {

				RECT* suggested = reinterpret_cast < RECT* > ( lparam );

				self->scale = static_cast < float > ( HIWORD ( wparam ) ) / 96.0f;
				self->surface.set_scale ( self->scale );
				SetWindowPos ( window, nullptr, suggested->left, suggested->top, suggested->right - suggested->left, suggested->bottom - suggested->top, SWP_NOZORDER | SWP_NOACTIVATE );
				self->dirty = true;

				return 0;

			}

			case WM_GETMINMAXINFO: {

				MINMAXINFO* limits = reinterpret_cast < MINMAXINFO* > ( lparam );

				limits->ptMinTrackSize.x = static_cast < LONG > ( minimum_width * self->scale );
				limits->ptMinTrackSize.y = static_cast < LONG > ( minimum_height * self->scale );

				return 0;

			}

			case WM_SETTINGCHANGE:
				if ( lparam && self->settings.theme == ui::THEME_SYSTEM )
					apply_theme ( *self );

				break;

			case WM_ERASEBKGND:
				return 1;

			case WM_PAINT: {

				PAINTSTRUCT painting;

				BeginPaint ( window, &painting );
				EndPaint ( window, &painting );
				paint ( *self );

				return 0;

			}

			case WM_CLOSE:
				DestroyWindow ( window );
				return 0;

			case WM_DESTROY:
				PostQuitMessage ( 0 );
				return 0;

		}

		return DefWindowProcW ( window, message, wparam, lparam );

	}

	static host* create_host ( ) {

		host* self = new host;
		HINSTANCE instance = GetModuleHandleW ( nullptr );
		WNDCLASSEXW kind = { sizeof ( kind ) };

		self->run = new session;
		load_settings ( self->settings, self->run->processor ( ).threads, self->run->suggested_threads ( ) );
		self->view = new ui::shell ( self->settings );

		kind.style = CS_DBLCLKS;
		kind.lpfnWndProc = &procedure;
		kind.hInstance = instance;
		kind.hCursor = LoadCursorW ( nullptr, IDC_ARROW );
		kind.hIcon = LoadIconW ( instance, MAKEINTRESOURCEW ( 1 ) );
		kind.hIconSm = kind.hIcon;
		kind.lpszClassName = L"AlloyWindow";
		RegisterClassExW ( &kind );

		RECT work;
		UINT dpi = GetDpiForSystem ( );

		SystemParametersInfoW ( SPI_GETWORKAREA, 0, &work, 0 );
		self->scale = static_cast < float > ( dpi ) / 96.0f;

		int32_t wide = base::min ( static_cast < int32_t > ( default_width * self->scale ), static_cast < int32_t > ( ( work.right - work.left ) * 0.92f ) );
		int32_t tall = base::min ( static_cast < int32_t > ( default_height * self->scale ), static_cast < int32_t > ( ( work.bottom - work.top ) * 0.92f ) );
		int32_t left = work.left + ( work.right - work.left - wide ) / 2;
		int32_t top = work.top + ( work.bottom - work.top - tall ) / 2;

		CreateWindowExW ( WS_EX_NOREDIRECTIONBITMAP | WS_EX_APPWINDOW, kind.lpszClassName, L"Alloy", WS_OVERLAPPEDWINDOW, left, top, wide, tall, nullptr, nullptr, instance, self );

		if ( !self->window )
			return self;

		self->scale = static_cast < float > ( GetDpiForWindow ( self->window ) ) / 96.0f;

		DWM_WINDOW_CORNER_PREFERENCE corners = DWMWCP_ROUND;

		DwmSetWindowAttribute ( self->window, DWMWA_WINDOW_CORNER_PREFERENCE, &corners, sizeof ( corners ) );
		apply_theme ( *self );
		SetWindowPos ( self->window, nullptr, 0, 0, 0, 0, SWP_FRAMECHANGED | SWP_NOMOVE | SWP_NOSIZE | SWP_NOZORDER );

		RECT client;

		GetClientRect ( self->window, &client );
		self->view->set_size ( client.right / self->scale, client.bottom / self->scale );

		if ( self->surface.open_window ( self->window, client.right, client.bottom, self->scale ) )
			paint ( *self, true );

		return self;

	}

	static void destroy_host ( host* self ) {

		if ( self->save_at > 0.0 )
			save_settings ( self->settings );

		self->run->stop ( );

		if ( self->window && IsWindow ( self->window ) )
			DestroyWindow ( self->window );

		self->surface.close ( );
		delete self->view;
		delete self->run;
		delete self;

	}

	static bool pump ( host* self, double until ) {

		MSG message;

		while ( until < 0.0 || base::seconds ( ) < until ) {

			while ( PeekMessageW ( &message, nullptr, 0, 0, PM_REMOVE ) ) {

				if ( message.message == WM_QUIT )
					return false;

				TranslateMessage ( &message );
				DispatchMessageW ( &message );

			}

			double now = base::seconds ( );

			if ( self->run->tick ( now, self->settings ) )
				self->dirty = true;

			if ( self->save_at > 0.0 && now >= self->save_at ) {

				save_settings ( self->settings );
				self->save_at = 0.0;

			}

			bool visible = on_screen ( *self );

			if ( ( self->dirty || self->view->animating ( now ) ) && visible ) {

				self->dirty = false;
				paint ( *self );

				continue;

			}

			double wait = visible ? base::min ( self->view->wake_after ( now ), 0.25 ) : 0.5;
			DWORD result = MsgWaitForMultipleObjectsEx ( 0, nullptr, static_cast < DWORD > ( wait * 1000.0 ) + 1, QS_ALLINPUT, MWMO_INPUTAVAILABLE );

			if ( result == WAIT_TIMEOUT && visible && self->view->waiting ( ) )
				self->dirty = true;

			if ( !visible )
				self->dirty = true;

		}

		return true;

	}

	uint32_t open_window ( ) {

		host* self = create_host ( );

		if ( !self->window || !self->surface.ready ( ) ) {

			destroy_host ( self );

			return 1;

		}

		ShowWindow ( self->window, SW_SHOW );

		if ( self->settings.autostart )
			self->run->start ( self->settings );

		pump ( self, -1.0 );
		self->window = nullptr;
		destroy_host ( self );

		return 0;

	}

#ifdef ALLOY_DEBUG

	uint32_t smoke_window ( const wchar_t* shot ) {

		freeze_settings ( );

		host* self = create_host ( );
		uint32_t failures = 0;

		auto report = [ & ] ( bool passed, const wchar_t* what ) {

			base::text line;

			line.add ( passed ? L"  pass  " : L"  FAIL  " ).add ( what );
			base::print_line ( line.get ( ) );
			failures += passed ? 0 : 1;

		};

		report ( self->window && self->surface.ready ( ), L"the window opens with a composition swap chain" );

		if ( !self->window || !self->surface.ready ( ) ) {

			destroy_host ( self );

			return 1;

		}

		pump ( self, base::seconds ( ) + 0.5 );

		RECT outer;
		RECT inner;
		POINT corner = { 0, 0 };

		GetWindowRect ( self->window, &outer );
		GetClientRect ( self->window, &inner );
		ClientToScreen ( self->window, &corner );
		report ( corner.y == outer.top && inner.bottom > 0 && corner.x > outer.left, L"the client area reaches the top edge while the side frames stay for resizing" );

		int32_t top = outer.top;
		int32_t right = corner.x + inner.right;
		int32_t button = static_cast < int32_t > ( ui::shell::caption_width * self->scale );
		int32_t middle = top + static_cast < int32_t > ( 20.0f * self->scale );

		auto hit_at = [ & ] ( int32_t x, int32_t y ) {

			return SendMessageW ( self->window, WM_NCHITTEST, 0, MAKELPARAM ( x, y ) );

		};

		report ( hit_at ( corner.x + 400, middle ) == HTCAPTION, L"the empty title bar drags the window" );
		report ( hit_at ( right - button / 2, middle ) == HTCLOSE && hit_at ( right - button * 3 / 2, middle ) == HTMAXBUTTON && hit_at ( right - button * 5 / 2, middle ) == HTMINBUTTON, L"the caption buttons report close, maximize for snap layouts, and minimize" );
		report ( hit_at ( corner.x + 400, top + 2 ) == HTTOP && hit_at ( outer.left + 2, top + 300 ) == HTLEFT, L"the top and side edges resize" );
		report ( hit_at ( corner.x + 600, top + static_cast < int32_t > ( 300.0f * self->scale ) ) == HTCLIENT, L"the page area is client space" );

		base::list < double > hidden_frames;

		self->frames = &hidden_frames;
		self->dirty = true;
		self->view->open_page ( ui::PAGE_SETTINGS, base::seconds ( ) );
		pump ( self, base::seconds ( ) + 0.4 );
		self->frames = nullptr;
		self->view->open_page ( ui::PAGE_OVERVIEW, base::seconds ( ) );
		report ( hidden_frames.size ( ) == 0, L"a hidden window draws nothing, even mid-animation" );

		if ( shot && shot [ 0 ] ) {

			SetWindowPos ( self->window, HWND_TOPMOST, 0, 0, 0, 0, SWP_NOMOVE | SWP_NOSIZE | SWP_NOACTIVATE | SWP_SHOWWINDOW );
			pump ( self, base::seconds ( ) + 0.8 );

			MONITORINFOEXW monitor = { };
			DEVMODEW mode = { };

			monitor.cbSize = sizeof ( monitor );
			mode.dmSize = sizeof ( mode );
			GetMonitorInfoW ( MonitorFromWindow ( self->window, MONITOR_DEFAULTTONEAREST ), &monitor );
			EnumDisplaySettingsW ( monitor.szDevice, ENUM_CURRENT_SETTINGS, &mode );

			double refresh = mode.dmDisplayFrequency > 1 ? mode.dmDisplayFrequency : 60.0;

			auto pacing = [ & ] ( const wchar_t* when ) {

				base::list < double > frames;

				self->frames = &frames;

				for ( int32_t step = 1; step <= 12; step++ ) {

					self->view->open_page ( static_cast < ui::page > ( step % ui::PAGE_COUNT ), base::seconds ( ) );
					pump ( self, base::seconds ( ) + 0.2 );

				}

				self->frames = nullptr;
				self->view->open_page ( ui::PAGE_OVERVIEW, base::seconds ( ) );
				pump ( self, base::seconds ( ) + 0.5 );

				int32_t count = frames.size ( );
				int32_t late = 0;

				for ( int32_t i = 1; i < count; i++ ) {

					if ( frames [ i ] - frames [ i - 1 ] > 1.5 / refresh )
						late++;

				}

				double rate = count > 1 ? ( count - 1 ) / ( frames [ count - 1 ] - frames [ 0 ] ) : 0.0;
				base::text line;

				line.add ( L"animations " ).add ( when ).add ( L" run at " ).decimal ( rate, 1 ).add ( L" fps on this " ).number ( static_cast < uint64_t > ( refresh ) ).add ( L" Hz display, " ).number ( static_cast < uint64_t > ( late ) ).add ( L" of " ).number ( static_cast < uint64_t > ( count ) ).add ( L" frames late" );
				report ( rate > refresh * 0.95 && late * 100 <= count * 2, line.get ( ) );

			};

			pacing ( L"while idle" );
			self->settings.mode = ui::MODE_BENCHMARK;
			self->run->start ( self->settings );
			pump ( self, base::seconds ( ) + 3.0 );
			pacing ( L"while mining on every thread and the GPU" );

			base::list < double > minimized_frames;
			base::list < double > restored_frames;
			uint64_t before = self->run->view ( ).hashes;

			ShowWindow ( self->window, SW_SHOWMINNOACTIVE );
			self->frames = &minimized_frames;
			pump ( self, base::seconds ( ) + 1.2 );
			self->frames = nullptr;

			bool mined = self->run->view ( ).hashes > before;

			ShowWindow ( self->window, SW_SHOWNOACTIVATE );
			SetWindowPos ( self->window, HWND_TOPMOST, 0, 0, 0, 0, SWP_NOMOVE | SWP_NOSIZE | SWP_NOACTIVATE );
			self->frames = &restored_frames;
			pump ( self, base::seconds ( ) + 0.6 );
			self->frames = nullptr;
			report ( minimized_frames.size ( ) == 0 && mined && restored_frames.size ( ) > 0, L"minimized, the window draws no frames while mining carries on, and it redraws once restored" );
			pump ( self, base::seconds ( ) + 0.6 );

			HDC screen = GetDC ( nullptr );
			HDC memory = CreateCompatibleDC ( screen );
			int32_t wide = outer.right - outer.left;
			int32_t tall = outer.bottom - outer.top;
			BITMAPINFO format = { };
			void* bits = nullptr;

			format.bmiHeader.biSize = sizeof ( format.bmiHeader );
			format.bmiHeader.biWidth = wide;
			format.bmiHeader.biHeight = -tall;
			format.bmiHeader.biPlanes = 1;
			format.bmiHeader.biBitCount = 32;

			HBITMAP picture = CreateDIBSection ( memory, &format, DIB_RGB_COLORS, &bits, nullptr, 0 );
			HGDIOBJ previous = SelectObject ( memory, picture );
			bool copied = BitBlt ( memory, 0, 0, wide, tall, screen, outer.left, outer.top, SRCCOPY | CAPTUREBLT ) != 0;
			base::list < uint8_t > rgba;
			base::list < uint8_t > png;

			ShowWindow ( self->window, SW_HIDE );

			if ( copied && rgba.set_size ( wide * tall * 4 ) ) {

				const uint8_t* source = static_cast < const uint8_t* > ( bits );

				for ( int32_t i = 0; i < wide * tall; i++ ) {

					rgba [ i * 4 ] = source [ i * 4 + 2 ];
					rgba [ i * 4 + 1 ] = source [ i * 4 + 1 ];
					rgba [ i * 4 + 2 ] = source [ i * 4 ];
					rgba [ i * 4 + 3 ] = 255;

				}

				copied = base::encode_png ( rgba.data ( ), wide, tall, png ) && base::write_file ( shot, png.data ( ), static_cast < size_t > ( png.size ( ) ) );

			}

			SelectObject ( memory, previous );
			DeleteObject ( picture );
			DeleteDC ( memory );
			ReleaseDC ( nullptr, screen );
			report ( copied, L"a screenshot of the live window was saved" );

		}

		destroy_host ( self );

		return failures ? 1 : 0;

	}

#endif

}
