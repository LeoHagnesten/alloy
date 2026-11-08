#include "app/commands.hh"
#include "app/journal.hh"
#include "app/tune.hh"
#include "base/system/dialogs.hh"
#include "app/export.hh"
#include "app/session.hh"
#include "app/settings.hh"
#include "ui/shell.hh"
#include "render/surface.hh"
#include "base/system/clipboard.hh"
#include "base/system/clock.hh"
#include "base/system/console.hh"
#include "base/system/files.hh"
#include "base/formats/png.hh"
#include "base/system/hardware.hh"
#include "base/system/processes.hh"
#include "base/text.hh"
#include <winsock2.h>
#include <ws2ipdef.h>
#include <windows.h>
#include <iphlpapi.h>
#include <shellapi.h>
#include <dwmapi.h>

namespace app {

	static constexpr float default_width = 1240.0f;
	static constexpr float default_height = 800.0f;
	static constexpr float minimum_width = 980.0f;
	static constexpr float minimum_height = 640.0f;
	static constexpr UINT tray_message = WM_APP + 1;
	static constexpr UINT network_message = WM_APP + 2;
	static constexpr UINT_PTR menu_show = 1;
	static constexpr UINT_PTR menu_toggle = 2;
	static constexpr UINT_PTR menu_quit = 3;
	static constexpr UINT_PTR menu_hold = 4;
	static constexpr int32_t hotkey_id = 1;

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
		bool tray_shown = false;
		bool told_tray = false;
		bool quitting = false;
		bool login_applied = false;
		bool login_known = false;
		bool hotkey_on = false;
		bool setup = false;
		tuner* tuning = nullptr;
		HANDLE network = nullptr;
		double network_at = 0.0;
		UINT taskbar_created = 0;
		double save_at = 0.0;
		double tip_at = 0.0;
		base::list < double >* frames = nullptr;

	};

	static void tray_text ( host& self, wchar_t* out, int32_t room ) {

		const ui::snapshot& view = self.run->view ( );
		base::text line;

		line.add ( L"Alloy" );

		if ( view.running && view.paused )
			line.add ( L", paused" );
		else if ( view.running )
			line.add ( L", " ).add ( chain::algorithm_name ( view.algorithm ) ).add ( L" at " ).scaled ( view.rate, L"H/s" );
		else
			line.add ( L", not mining" );

		base::copy_text ( out, room, line.get ( ) );

	}

	static void sync_tray ( host& self ) {

		NOTIFYICONDATAW data = { sizeof ( data ) };

		data.hWnd = self.window;
		data.uID = 1;

		bool wanted = self.settings.close_to_tray || self.settings.notify;

		if ( wanted && !self.tray_shown ) {

			data.uFlags = NIF_ICON | NIF_MESSAGE | NIF_TIP;
			data.uCallbackMessage = tray_message;
			data.hIcon = static_cast < HICON > ( LoadImageW ( GetModuleHandleW ( nullptr ), MAKEINTRESOURCEW ( 1 ), IMAGE_ICON, GetSystemMetrics ( SM_CXSMICON ), GetSystemMetrics ( SM_CYSMICON ), LR_DEFAULTCOLOR ) );
			tray_text ( self, data.szTip, base::count_of ( data.szTip ) );
			self.tray_shown = Shell_NotifyIconW ( NIM_ADD, &data ) != FALSE;

		} else if ( !wanted && self.tray_shown ) {

			Shell_NotifyIconW ( NIM_DELETE, &data );
			self.tray_shown = false;

			if ( !IsWindowVisible ( self.window ) )
				ShowWindow ( self.window, SW_SHOW );

		}

	}

	static void refresh_tray ( host& self, double now ) {

		if ( !self.tray_shown || now < self.tip_at )
			return;

		NOTIFYICONDATAW data = { sizeof ( data ) };

		self.tip_at = now + 1.0;
		data.hWnd = self.window;
		data.uID = 1;
		data.uFlags = NIF_TIP;
		tray_text ( self, data.szTip, base::count_of ( data.szTip ) );
		Shell_NotifyIconW ( NIM_MODIFY, &data );

	}

	static void hide_to_tray ( host& self ) {

		ShowWindow ( self.window, SW_HIDE );

		if ( self.told_tray )
			return;

		NOTIFYICONDATAW data = { sizeof ( data ) };

		self.told_tray = true;
		data.hWnd = self.window;
		data.uID = 1;
		data.uFlags = NIF_INFO;
		data.dwInfoFlags = NIIF_INFO | NIIF_NOSOUND;
		base::copy_text ( data.szInfoTitle, base::count_of ( data.szInfoTitle ), L"Alloy is still running" );
		base::copy_text ( data.szInfo, base::count_of ( data.szInfo ), self.run->running ( ) ? L"Mining carries on here. Click the icon to open the window again." : L"Click the icon to open the window again, or quit from its menu." );
		Shell_NotifyIconW ( NIM_MODIFY, &data );

	}

	static void balloon ( host& self, const wchar_t* title, const wchar_t* text ) {

		if ( !self.tray_shown || !self.settings.notify )
			return;

		NOTIFYICONDATAW data = { sizeof ( data ) };

		data.hWnd = self.window;
		data.uID = 1;
		data.uFlags = NIF_INFO;
		data.dwInfoFlags = NIIF_INFO | NIIF_NOSOUND;
		base::copy_text ( data.szInfoTitle, base::count_of ( data.szInfoTitle ), title );
		base::copy_text ( data.szInfo, base::count_of ( data.szInfo ), text );
		Shell_NotifyIconW ( NIM_MODIFY, &data );

	}

	static void sync_hotkey ( host& self ) {

		if ( self.settings.hotkey == self.hotkey_on || settings_frozen ( ) )
			return;

		if ( self.settings.hotkey )
			self.hotkey_on = RegisterHotKey ( self.window, hotkey_id, MOD_CONTROL | MOD_ALT | MOD_NOREPEAT, 'M' ) != FALSE;
		else {

			UnregisterHotKey ( self.window, hotkey_id );
			self.hotkey_on = false;

		}

		if ( self.settings.hotkey && !self.hotkey_on )
			self.run->log ( ui::LEVEL_WARNING, L"Another program already uses Ctrl+Alt+M, so the pause shortcut is off" );

	}

	static void toggle_hold ( host& self ) {

		if ( !self.run->running ( ) ) {

			self.run->start ( self.settings );
			balloon ( self, L"Mining started", L"Press Ctrl+Alt+M again to pause." );
			self.dirty = true;

			return;

		}

		bool hold = !self.run->holding ( );

		self.run->set_held ( hold );
		balloon ( self, hold ? L"Mining paused" : L"Mining resumed", hold ? L"Press Ctrl+Alt+M to carry on." : L"Press Ctrl+Alt+M to pause again." );
		self.dirty = true;

	}

	static void WINAPI network_moved ( void* context, MIB_IPINTERFACE_ROW* row, MIB_NOTIFICATION_TYPE kind ) {

		PostMessageW ( static_cast < HWND > ( context ), network_message, 0, 0 );

	}

	static void show_from_tray ( host& self ) {

		ShowWindow ( self.window, IsIconic ( self.window ) ? SW_RESTORE : SW_SHOW );
		SetForegroundWindow ( self.window );
		self.dirty = true;

	}

	static void sync_login ( host& self ) {

		if ( settings_frozen ( ) || ( self.login_known && self.login_applied == self.settings.launch_at_login ) )
			return;

		if ( base::set_run_at_login ( L"Alloy", self.settings.launch_at_login, L"--minimized" ) ) {

			self.login_applied = self.settings.launch_at_login;
			self.login_known = true;

		}

	}

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

	static void follow_tuning ( host& self ) {

		ui::snapshot& view = self.run->edit ( );
		tune_result result;

		view.tuning = true;

		if ( view.tuning_steps != self.tuning->steps ( ) ) {

			view.tuning_steps = self.tuning->steps ( );
			self.dirty = true;

		}

		if ( !self.tuning->finished ( result ) )
			return;

		delete self.tuning;
		self.tuning = nullptr;
		view.tuning = false;
		view.tuning_steps = 0;
		self.dirty = true;

		if ( !result.finished || result.rate <= 0.0 ) {

			self.run->log ( ui::LEVEL_WARNING, L"Tuning stopped before it finished, so nothing changed" );

			return;

		}

		ui::algorithm_tuning& slot = self.settings.tunings [ result.algorithm % chain::ALGORITHM_COUNT ];
		base::text line;

		slot.threads = result.threads;
		slot.gpu = result.gpu;
		slot.intensity = result.intensity;
		slot.cpu_limit = 100;
		slot.gpu_limit = 100;

		if ( self.settings.algorithm == result.algorithm )
			self.settings.unstash ( );

		line.add ( L"Fastest for " ).add ( chain::algorithm_name ( result.algorithm ) ).add ( L": " ).number ( result.threads ).add ( L" threads" );

		if ( result.gpu )
			line.add ( L" and the graphics card at intensity " ).number ( result.intensity );

		line.add ( L", " ).scaled ( result.rate, L"H/s" ).add ( L". Saved." );
		self.run->log ( ui::LEVEL_GOOD, line.get ( ) );
		balloon ( self, L"Tuning finished", line.get ( ) );
		self.run->apply ( self.settings );
		self.save_at = base::seconds ( ) + 0.6;

	}

	static void refresh_profiles ( host& self ) {

		ui::snapshot& view = self.run->edit ( );

		view.profile_count = list_profiles ( view.profiles, 12 );
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
				sync_tray ( self );
				sync_login ( self );
				sync_hotkey ( self );
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

			case ui::COMMAND_COPY_TOKEN: {

				wchar_t token [ 40 ];

				base::widen ( token, base::count_of ( token ), self.settings.api_token );
				base::write_clipboard ( self.window, token );
				self.run->log ( ui::LEVEL_INFO, L"Copied the access token" );
				break;

			}

			case ui::COMMAND_OPEN_DASHBOARD: {

				base::text address;
				wchar_t token [ 40 ];

				base::widen ( token, base::count_of ( token ), self.settings.api_token );
				address.add ( L"http://127.0.0.1:" ).number ( self.settings.api_port ? self.settings.api_port : 4067u ).put ( L'/' );

				if ( token [ 0 ] )
					address.add ( L"#token=" ).add ( token );

				base::open_in_shell ( address.get ( ) );
				break;

			}

			case ui::COMMAND_EXPORT_SHARES:
			case ui::COMMAND_EXPORT_LOG: {

				bool days = command == ui::COMMAND_EXPORT_SHARES && self.view->argument ( ) == 1;
				bool shares = command == ui::COMMAND_EXPORT_SHARES && !days;
				wchar_t path [ base::path_room ];

				if ( !base::ask_save_path ( self.window, days ? L"alloy-days.csv" : ( shares ? L"alloy-shares.csv" : L"alloy-log.txt" ), shares || days ? L"Comma separated values" : L"Text", shares || days ? L"csv" : L"txt", path, base::path_room ) )
					break;

				bool saved = days ? export_days ( self.run->view ( ), path ) : ( shares ? export_shares ( self.run->view ( ), path ) : export_log ( self.run->view ( ), path ) );

				self.run->log ( saved ? ui::LEVEL_INFO : ui::LEVEL_WARNING, saved ? ( days ? L"Saved the daily history as CSV" : ( shares ? L"Saved the shares as CSV" : L"Saved the log" ) ) : L"Couldn't write that file" );
				break;

			}

			case ui::COMMAND_TUNE: {

				if ( self.tuning ) {

					self.tuning->stop ( );
					delete self.tuning;
					self.tuning = nullptr;
					self.run->edit ( ).tuning = false;
					self.run->log ( ui::LEVEL_INFO, L"Stopped tuning" );

					break;

				}

				if ( self.run->running ( ) )
					self.run->stop ( );

				self.tuning = new tuner;
				self.tuning->begin ( self.settings.algorithm, self.settings );
				self.run->edit ( ).tuning = true;

				base::text line;

				line.add ( L"Finding the fastest settings for " ).add ( chain::algorithm_name ( self.settings.algorithm ) ).add ( self.settings.algorithm == chain::ALGORITHM_RANDOMX ? L". This takes about a minute." : L". This takes under a minute." );
				self.run->log ( ui::LEVEL_INFO, line.get ( ) );
				break;

			}

			case ui::COMMAND_SAVE_PROFILE: {

				const wchar_t* name = self.view->pending_profile ( );
				base::text line;

				if ( !name [ 0 ] ) {

					self.run->log ( ui::LEVEL_WARNING, L"Type a name for the profile first" );
					break;

				}

				bool saved = save_profile ( name, self.settings );

				line.add ( saved ? L"Saved profile " : L"Couldn't save profile " ).add ( name );
				self.run->log ( saved ? ui::LEVEL_INFO : ui::LEVEL_WARNING, line.get ( ) );
				refresh_profiles ( self );
				break;

			}

			case ui::COMMAND_APPLY_PROFILE:
			case ui::COMMAND_DELETE_PROFILE: {

				const ui::snapshot& view = self.run->view ( );
				int32_t at = self.view->argument ( );
				base::text line;

				if ( at < 0 || at >= view.profile_count )
					break;

				wchar_t name [ 48 ];

				base::copy_text ( name, base::count_of ( name ), view.profiles [ at ] );

				if ( command == ui::COMMAND_DELETE_PROFILE ) {

					delete_profile ( name );
					line.add ( L"Deleted profile " ).add ( name );
					self.run->log ( ui::LEVEL_INFO, line.get ( ) );
					refresh_profiles ( self );

					break;

				}

				bool running = self.run->running ( );

				if ( !load_profile ( name, self.settings, self.run->processor ( ).threads ) )
					break;

				line.add ( L"Switched to profile " ).add ( name );
				self.run->log ( ui::LEVEL_INFO, line.get ( ) );
				self.view->reload ( );
				self.run->apply ( self.settings );

				if ( running )
					self.run->start ( self.settings );

				apply_theme ( self );
				sync_tray ( self );
				self.save_at = base::seconds ( ) + 0.6;
				break;

			}

			case ui::COMMAND_LARGE_PAGES: {

				wchar_t program [ base::path_room ];

				if ( !base::own_path ( program, base::path_room ) )
					break;

				if ( base::run_elevated ( program, L"--allow-large-pages" ) ) {

					self.run->log ( ui::LEVEL_GOOD, L"Windows now lets this account use large pages. Sign out and back in, and RandomX picks them up." );
					balloon ( self, L"Large pages allowed", L"Sign out of Windows and back in, then RandomX uses them." );

				} else
					self.run->log ( ui::LEVEL_WARNING, L"Large pages weren't allowed. Windows needs an administrator to approve it." );

				break;

			}

			case ui::COMMAND_ADD_RIG: {

				ui::rig_entry entry;
				int32_t free_slot = -1;

				for ( int32_t i = 0; i < ui::rig_room && free_slot < 0; i++ ) {

					if ( !self.settings.rigs [ i ].address [ 0 ] )
						free_slot = i;

				}

				if ( !parse_rig ( self.view->pending_rig ( ), entry ) ) {

					self.run->log ( ui::LEVEL_WARNING, L"Type the other computer's address first, like 192.168.1.20 or 192.168.1.20:4067" );
					break;

				}

				if ( free_slot < 0 ) {

					self.run->log ( ui::LEVEL_WARNING, L"Alloy watches up to eight rigs. Remove one first." );
					break;

				}

				self.settings.rigs [ free_slot ] = entry;
				self.view->clear_entry ( ui::SETTING_RIG );
				self.save_at = base::seconds ( ) + 0.6;
				break;

			}

			case ui::COMMAND_REMOVE_RIG: {

				int32_t at = self.view->argument ( );
				int32_t seen = 0;

				for ( int32_t i = 0; i < ui::rig_room; i++ ) {

					if ( !self.settings.rigs [ i ].address [ 0 ] )
						continue;

					if ( seen++ == at ) {

						for ( int32_t k = i; k + 1 < ui::rig_room; k++ )
							self.settings.rigs [ k ] = self.settings.rigs [ k + 1 ];

						self.settings.rigs [ ui::rig_room - 1 ] = ui::rig_entry { };

						break;

					}

				}

				self.save_at = base::seconds ( ) + 0.6;
				break;

			}

			case ui::COMMAND_FINISH_SETUP:
				self.view->open_page ( ui::PAGE_OVERVIEW, base::seconds ( ) );
				self.setup = false;
				self.run->apply ( self.settings );
				save_settings ( self.settings );

				if ( self.view->argument ( ) == 1 && !self.run->running ( ) ) {

					self.settings.mode = ui::MODE_POOL;
					self.run->start ( self.settings );

				}

				break;

			case ui::COMMAND_OPEN_LOGS: {

				wchar_t folder [ base::path_room ];

				if ( journal_folder ( folder, base::path_room ) )
					base::open_in_shell ( folder );

				break;

			}

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
				if ( self->settings.close_to_tray && self->tray_shown && !self->quitting ) {

					handle ( *self, self->view->blur ( now ) );
					hide_to_tray ( *self );

					return 0;

				}

				DestroyWindow ( window );
				return 0;

			case WM_DESTROY: {

				NOTIFYICONDATAW data = { sizeof ( data ) };

				data.hWnd = window;
				data.uID = 1;

				if ( self->tray_shown )
					Shell_NotifyIconW ( NIM_DELETE, &data );

				self->tray_shown = false;
				PostQuitMessage ( 0 );

				return 0;

			}

			case WM_HOTKEY:
				if ( wparam == hotkey_id )
					toggle_hold ( *self );

				return 0;

			case WM_POWERBROADCAST:
				if ( wparam == PBT_APMSUSPEND ) {

					self->run->set_sleeping ( true );
					self->run->log ( ui::LEVEL_INFO, L"The computer is going to sleep" );

				} else if ( wparam == PBT_APMRESUMEAUTOMATIC || wparam == PBT_APMRESUMESUSPEND ) {

					if ( self->run->running ( ) )
						self->run->log ( ui::LEVEL_INFO, L"The computer woke up, so Alloy reconnects right away" );

					self->run->set_sleeping ( false );

				}

				self->dirty = true;
				return TRUE;

			case network_message: {

				double moment = base::seconds ( );
				uint8_t link = self->run->view ( ).link;

				if ( moment - self->network_at > 2.0 && link != ui::PHASE_READY && link != ui::PHASE_OFF ) {

					self->network_at = moment;
					self->run->network_changed ( );

				}

				return 0;

			}

			case tray_message: {

				UINT event = LOWORD ( lparam );

				if ( event == WM_LBUTTONUP || event == NIN_SELECT || event == NIN_KEYSELECT ) {

					show_from_tray ( *self );

					return 0;

				}

				if ( event != WM_RBUTTONUP && event != WM_CONTEXTMENU )
					return 0;

				HMENU menu = CreatePopupMenu ( );
				POINT spot;

				AppendMenuW ( menu, MF_STRING, menu_show, L"Open Alloy" );
				AppendMenuW ( menu, MF_STRING, menu_toggle, self->run->running ( ) ? L"Stop mining" : L"Start mining" );

				if ( self->run->running ( ) )
					AppendMenuW ( menu, MF_STRING, menu_hold, self->run->holding ( ) ? L"Resume mining\tCtrl+Alt+M" : L"Pause mining\tCtrl+Alt+M" );
				AppendMenuW ( menu, MF_SEPARATOR, 0, nullptr );
				AppendMenuW ( menu, MF_STRING, menu_quit, L"Quit Alloy" );
				GetCursorPos ( &spot );
				SetForegroundWindow ( window );

				UINT_PTR chosen = TrackPopupMenu ( menu, TPM_RETURNCMD | TPM_RIGHTBUTTON | TPM_NONOTIFY, spot.x, spot.y, 0, window, nullptr );

				DestroyMenu ( menu );
				PostMessageW ( window, WM_NULL, 0, 0 );

				if ( chosen == menu_show )
					show_from_tray ( *self );
				else if ( chosen == menu_toggle )
					handle ( *self, ui::COMMAND_TOGGLE );
				else if ( chosen == menu_hold )
					toggle_hold ( *self );
				else if ( chosen == menu_quit ) {

					self->quitting = true;
					PostMessageW ( window, WM_CLOSE, 0, 0 );

				}

				return 0;

			}

		}

		if ( self->taskbar_created && message == self->taskbar_created ) {

			self->tray_shown = false;
			sync_tray ( *self );

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

		self->taskbar_created = RegisterWindowMessageW ( L"TaskbarCreated" );

		return self;

	}

	static void destroy_host ( host* self ) {

		if ( self->save_at > 0.0 )
			save_settings ( self->settings );

		if ( self->tuning ) {

			self->tuning->stop ( );
			delete self->tuning;
			self->tuning = nullptr;

		}

		self->run->stop ( );

		if ( self->network )
			CancelMibChangeNotify2 ( self->network );

		if ( self->hotkey_on && self->window )
			UnregisterHotKey ( self->window, hotkey_id );

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

			refresh_tray ( *self, now );

			notice item;

			while ( self->run->next_notice ( item ) )
				balloon ( *self, item.title, item.text );

			if ( self->tuning )
				follow_tuning ( *self );

			uint8_t kind = 0;
			bool backup = false;
			char pin [ 65 ];

			if ( self->run->learned_pin ( kind, backup, pin, sizeof ( pin ) ) && pin [ 0 ] ) {

				ui::pool_profile& profile = self->settings.pools [ kind % chain::ALGORITHM_COUNT ];

				base::copy_text ( backup ? profile.backup_pin : profile.pin, 65, pin );
				self->save_at = now + 0.6;

			}

			if ( self->setup && !self->view->in_setup ( ) ) {

				self->setup = false;
				self->save_at = now;

			}

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

	uint32_t open_window ( const launch_options& options ) {

		bool minimized = options.minimized;
		HANDLE single = CreateMutexW ( nullptr, TRUE, L"Local\\AlloyMinerWindow" );

		if ( single && GetLastError ( ) == ERROR_ALREADY_EXISTS ) {

			HWND other = FindWindowW ( L"AlloyWindow", nullptr );

			if ( other && !minimized ) {

				ShowWindow ( other, IsIconic ( other ) ? SW_RESTORE : SW_SHOW );
				SetForegroundWindow ( other );

			}

			CloseHandle ( single );

			return 0;

		}

		open_journal ( );

		host* self = create_host ( );

		if ( options.profile || options.set_count ) {

			apply_launch ( options, self->settings, self->run->processor ( ).threads );
			self->view->reload ( );
			apply_theme ( *self );

		}

		if ( !self->window || !self->surface.ready ( ) ) {

			journal ( ui::LEVEL_ERROR, L"The window couldn't be created, Direct3D 11 or DirectComposition isn't available" );
			destroy_host ( self );
			close_journal ( );

			if ( single )
				CloseHandle ( single );

			return 1;

		}

		if ( !minimized && !options.profile && !options.set_count && !settings_exist ( ) ) {

			self->setup = true;
			self->view->open_setup ( base::seconds ( ) );

		}

		sync_tray ( *self );
		sync_login ( *self );
		self->run->apply ( self->settings );
		self->run->check_kernels ( );
		self->run->start_lookups ( );
		refresh_profiles ( *self );
		sync_hotkey ( *self );
		NotifyIpInterfaceChange ( AF_UNSPEC, &network_moved, self->window, FALSE, &self->network );

		wchar_t report [ base::path_room ];
		bool unclean = false;

		if ( previous_crash ( report, base::path_room, unclean ) ) {

			base::text line;

			if ( report [ 0 ] )
				line.add ( L"Alloy crashed last time. The report is at " ).add ( report );
			else
				line.add ( L"Alloy didn't close cleanly last time, after a power cut or a forced close" );

			self->run->log ( ui::LEVEL_WARNING, line.get ( ) );

		}

		if ( !minimized )
			ShowWindow ( self->window, SW_SHOW );
		else if ( !self->tray_shown )
			ShowWindow ( self->window, SW_SHOWMINNOACTIVE );

		if ( self->settings.autostart )
			self->run->start ( self->settings );

		pump ( self, -1.0 );
		self->window = nullptr;
		destroy_host ( self );
		close_journal ( );

		if ( single )
			CloseHandle ( single );

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
