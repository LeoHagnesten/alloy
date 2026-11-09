#ifdef ALLOY_DEBUG

#include "app/commands.hh"
#include "app/session.hh"
#include "app/settings.hh"
#include "pool/test_pool.hh"
#include "render/surface.hh"
#include "ui/shell.hh"
#include "base/bits.hh"
#include "base/system/clock.hh"
#include "base/system/console.hh"
#include "base/system/files.hh"
#include "base/formats/gif.hh"
#include "base/math.hh"
#include "base/formats/png.hh"
#include "base/strings.hh"
#include "base/text.hh"

namespace app {

	struct studio {

		render::surface sharp;
		render::surface video;
		session* run = nullptr;
		ui::shell* view = nullptr;
		ui::preferences settings;
		const wchar_t* folder = L"";
		double clock = 1000.0;
		float cursor_x = -100.0f;
		float cursor_y = -100.0f;
		bool cursor = false;
		bool light = false;
		double sample_real = 0.0;
		double sample_shown = -1.0;
		base::list < uint8_t > pixels;

	};

	static bool save ( const wchar_t* folder, const wchar_t* name, const base::list < uint8_t >& bytes ) {

		wchar_t path [ base::path_room ];
		base::text note;

		if ( !base::join_path ( path, base::path_room, folder, name ) || !base::write_file ( path, bytes.data ( ), static_cast < size_t > ( bytes.size ( ) ) ) )
			return false;

		note.add ( L"  wrote " ).add ( path );
		base::print_line ( note.get ( ) );

		return true;

	}

	static void handle ( studio& self, ui::command command ) {

		if ( command == ui::COMMAND_TOGGLE ) {

			if ( self.run->running ( ) )
				self.run->stop ( );
			else
				self.run->start ( self.settings );

		} else if ( command == ui::COMMAND_PREFERENCES || command == ui::COMMAND_RESTART ) {

			self.run->apply ( self.settings );
			self.light = self.settings.theme == ui::THEME_LIGHT;
			self.view->set_light ( self.light );

		} else if ( command == ui::COMMAND_CLEAR_LOG )
			self.run->clear_log ( );

	}

	static void draw_cursor ( render::surface& surface, float x, float y ) {

		float arrow [ ] = { x, y, x, y + 17.0f, x + 4.2f, y + 13.2f, x + 7.0f, y + 19.5f, x + 9.6f, y + 18.4f, x + 6.9f, y + 12.2f, x + 12.2f, y + 12.2f };
		float outline [ ] = { x, y, x, y + 17.0f, x + 4.2f, y + 13.2f, x + 7.0f, y + 19.5f, x + 9.6f, y + 18.4f, x + 6.9f, y + 12.2f, x + 12.2f, y + 12.2f, x, y };

		surface.polygon ( arrow, 7, render::rgb ( 0x101010 ) );
		surface.polyline ( outline, 8, render::rgb ( 0xFFFFFF ), 1.2f );

	}

	static bool render_into ( studio& self, render::surface& surface ) {

		self.run->tick ( base::seconds ( ), self.settings );

		ui::snapshot& shown = self.run->edit ( );

		if ( shown.history_time != self.sample_shown )
			self.sample_real = shown.history_time;

		shown.history_time = self.clock - ( base::seconds ( ) - self.sample_real );
		self.sample_shown = shown.history_time;
		self.run->edit ( ).light = self.light;
		self.view->set_light ( self.light );

		if ( !surface.begin ( self.view->background ( ) ) )
			return false;

		self.view->compose ( surface, self.run->view ( ), self.clock );

		if ( self.cursor )
			draw_cursor ( surface, self.cursor_x, self.cursor_y );

		return surface.end ( ) && surface.capture ( self.pixels );

	}

	static void shoot ( studio& self, const wchar_t* name ) {

		base::list < uint8_t > png;

		render_into ( self, self.sharp );
		self.clock += 3.0;
		render_into ( self, self.sharp );

		if ( base::encode_png ( self.pixels.data ( ), self.sharp.pixel_width ( ), self.sharp.pixel_height ( ), png ) )
			save ( self.folder, name, png );

	}

	static void settle ( studio& self, double seconds ) {

		double until = base::seconds ( ) + seconds;

		while ( base::seconds ( ) < until ) {

			self.run->tick ( base::seconds ( ), self.settings );
			base::sleep ( 50 );

		}

	}

	struct take {

		studio& self;
		base::gif_writer gif;
		double start = 0.0;
		float from_x = 0.0f;
		float from_y = 0.0f;
		float to_x = 0.0f;
		float to_y = 0.0f;
		double glide_from = 0.0;
		double glide_length = 0.0;

		explicit take ( studio& owner ) : self ( owner ) { }

		void glide_to ( ui::control id, int32_t index, double length, float nudge_x = 0.0f ) {

			render::box target;

			if ( !self.view->locate ( id, index, target ) )
				return;

			glide ( target.middle_x ( ) + nudge_x, target.middle_y ( ), length );

		}

		void glide ( float x, float y, double length ) {

			from_x = self.cursor_x;
			from_y = self.cursor_y;
			to_x = x;
			to_y = y;
			glide_from = self.clock;
			glide_length = length;

		}

		void step ( ) {

			if ( glide_length <= 0.0 )
				return;

			float t = base::ease_in_out ( static_cast < float > ( ( self.clock - glide_from ) / glide_length ) );

			self.cursor_x = base::mix ( from_x, to_x, t );
			self.cursor_y = base::mix ( from_y, to_y, t );
			handle ( self, self.view->mouse_move ( self.cursor_x, self.cursor_y, self.clock ) );

			if ( t >= 1.0f )
				glide_length = 0.0;

		}

		void press ( ) {

			handle ( self, self.view->mouse_down ( self.cursor_x, self.cursor_y, false, self.clock ) );

		}

		void release ( ) {

			handle ( self, self.view->mouse_up ( self.cursor_x, self.cursor_y, self.clock ) );

		}

	};

	static void learn ( take& clip ) {

		studio& self = clip.self;

		if ( !clip.gif.open ( self.video.pixel_width ( ), self.video.pixel_height ( ) ) )
			return;

		for ( int32_t i = 0; i < ui::PAGE_COUNT; i++ ) {

			self.view->open_page ( static_cast < ui::page > ( i ), self.clock );
			self.clock += 2.0;
			render_into ( self, self.video );
			clip.gif.learn ( self.pixels.data ( ) );

		}

		self.view->open_page ( ui::PAGE_OVERVIEW, self.clock );
		self.clock += 2.0;
		clip.gif.seal ( );

	}

	template < typename script >
	static void record ( take& clip, const wchar_t* name, double length, script&& action ) {

		studio& self = clip.self;
		base::list < uint8_t > bytes;
		int32_t frames = static_cast < int32_t > ( length * 50.0 );

		clip.start = self.clock;

		for ( int32_t i = 0; i < frames; i++ ) {

			double t = static_cast < double > ( i ) / 50.0;
			double real = base::seconds ( );

			self.clock = clip.start + t;
			action ( t );
			clip.step ( );
			render_into ( self, self.video );
			clip.gif.frame ( self.pixels.data ( ), 2 );

			double spent = base::seconds ( ) - real;

			if ( spent < 0.02 )
				base::sleep ( static_cast < uint32_t > ( ( 0.02 - spent ) * 1000.0 ) );

		}

		if ( clip.gif.finish ( bytes ) )
			save ( self.folder, name, bytes );

	}

	static bool once ( double t, double at, bool& done ) {

		if ( done || t < at )
			return false;

		done = true;

		return true;

	}

	uint32_t capture ( const wchar_t* folder, uint32_t warmup, bool clips ) {

		studio* self = new studio;
		pool::test_server* server = new pool::test_server;
		pool::server_settings config;

		freeze_settings ( );
		base::make_folder ( folder );
		self->folder = folder;
		self->run = new session;
		load_settings ( self->settings, self->run->processor ( ).threads, self->run->suggested_threads ( ) );
		self->view = new ui::shell ( self->settings );
		config.port = 0;
		config.difficulty = 0.5;
		config.job_seconds = 30;

		if ( !server->start ( config ) || !self->sharp.open_offscreen ( 1600, 1000, 1.25f ) || !self->video.open_offscreen ( 1280, 800, 1.0f ) ) {

			base::print_line ( L"capture: couldn't start the test pool or the renderer" );

			return 1;

		}

		base::writer < wchar_t, 64 > address;

		address.add ( L"127.0.0.1:" ).number ( server->port ( ) );
		base::copy_text ( self->settings.pool ( ).address, base::count_of ( self->settings.pool ( ).address ), address.get ( ) );
		base::copy_text ( self->settings.pool ( ).worker, base::count_of ( self->settings.pool ( ).worker ), L"alloy.studio" );
		shoot ( *self, L"idle-dark.png" );

		uint8_t picked = self->settings.algorithm;

		self->settings.choose_algorithm ( chain::ALGORITHM_RANDOMX );
		self->view->open_page ( ui::PAGE_SETTINGS, self->clock );
		shoot ( *self, L"coins-randomx-dark.png" );
		self->settings.dataset_megabytes = 1280;
		self->view->wheel ( 700.0f, 500.0f, -480.0f, self->clock );
		self->clock += 1.0;
		render_into ( *self, self->sharp );

		render::box memory;

		if ( self->view->locate ( ui::CONTROL_SLIDER, ui::SETTING_DATASET, memory ) ) {

			self->cursor = true;
			self->cursor_x = 330.0f;
			self->cursor_y = memory.middle_y ( );
			self->view->mouse_move ( self->cursor_x, self->cursor_y, self->clock );
			render_into ( *self, self->sharp );
			self->clock += 1.0;
			shoot ( *self, L"memory-randomx-dark.png" );
			self->view->mouse_leave ( self->clock );
			self->clock += 1.0;
			self->cursor = false;

		}

		self->view->wheel ( 700.0f, 500.0f, 2400.0f, self->clock );
		self->settings.dataset_megabytes = ui::dataset_full;
		self->settings.choose_algorithm ( picked );
		self->settings.advanced = true;
		self->view->wheel ( 700.0f, 500.0f, -560.0f, self->clock );
		shoot ( *self, L"settings-advanced-dark.png" );
		self->view->wheel ( 700.0f, 500.0f, -1500.0f, self->clock );
		shoot ( *self, L"settings-advanced-cores-dark.png" );
		self->view->wheel ( 700.0f, 500.0f, 4000.0f, self->clock );
		self->settings.advanced = false;
		self->view->open_page ( ui::PAGE_OVERVIEW, self->clock );
		self->view->open_setup ( self->clock );
		self->view->reload ( );
		shoot ( *self, L"setup-dark.png" );
		self->view->open_page ( ui::PAGE_OVERVIEW, self->clock );

		base::print_line ( L"  mining against the test pool to fill the chart" );
		self->run->start ( self->settings );
		settle ( *self, static_cast < double > ( warmup ) );

		shoot ( *self, L"overview-dark.png" );

		render::box gear;

		if ( self->view->locate ( ui::CONTROL_CHART_GEAR, 0, gear ) ) {

			self->cursor = true;
			self->cursor_x = gear.x - 260.0f;
			self->cursor_y = gear.y + 90.0f;
			self->view->mouse_move ( self->cursor_x, self->cursor_y, self->clock );
			shoot ( *self, L"graph-gear-dark.png" );
			self->cursor_x = gear.middle_x ( );
			self->cursor_y = gear.middle_y ( );
			self->view->mouse_move ( self->cursor_x, self->cursor_y, self->clock );
			self->view->mouse_down ( self->cursor_x, self->cursor_y, false, self->clock );
			self->view->mouse_up ( self->cursor_x, self->cursor_y, self->clock );
			shoot ( *self, L"graph-options-dark.png" );
			self->light = true;
			shoot ( *self, L"graph-options-light.png" );
			self->light = false;
			self->view->key ( 0x1B, false, false, self->clock );
			self->view->mouse_leave ( self->clock );
			self->cursor = false;
			self->clock += 1.0;

		}

		self->view->open_page ( ui::PAGE_EARNINGS, self->clock );
		shoot ( *self, L"earnings-dark.png" );
		self->settings.pool_fee = 10;
		base::copy_text ( self->settings.power_watts, base::count_of ( self->settings.power_watts ), L"180" );
		base::copy_text ( self->settings.power_price, base::count_of ( self->settings.power_price ), L"0.25" );
		shoot ( *self, L"earnings-power-dark.png" );
		self->settings.pool_fee = 0;
		self->settings.power_watts [ 0 ] = 0;
		self->settings.power_price [ 0 ] = 0;
		self->view->open_page ( ui::PAGE_RIGS, self->clock );
		shoot ( *self, L"rigs-dark.png" );
		self->view->open_page ( ui::PAGE_HELP, self->clock );
		shoot ( *self, L"help-dark.png" );
		self->view->open_page ( ui::PAGE_ACTIVITY, self->clock );
		shoot ( *self, L"shares-dark.png" );
		self->view->open_tab ( 1, self->clock );
		shoot ( *self, L"log-dark.png" );
		self->view->open_tab ( 0, self->clock );
		self->view->open_page ( ui::PAGE_SETTINGS, self->clock );
		shoot ( *self, L"settings-dark.png" );
		self->cursor = true;
		self->cursor_x = 330.0f;
		self->cursor_y = 454.0f;
		self->view->mouse_move ( self->cursor_x, self->cursor_y, self->clock );
		render_into ( *self, self->sharp );
		self->clock += 1.0;
		shoot ( *self, L"hint-dark.png" );
		self->view->mouse_leave ( self->clock );
		self->clock += 1.0;
		self->cursor = false;
		self->view->wheel ( 700.0f, 500.0f, -1200.0f, self->clock );
		shoot ( *self, L"settings-lower-dark.png" );
		self->view->wheel ( 700.0f, 500.0f, 2400.0f, self->clock );
		self->light = true;
		shoot ( *self, L"settings-light.png" );
		self->view->open_page ( ui::PAGE_OVERVIEW, self->clock );
		shoot ( *self, L"overview-light.png" );
		self->light = false;

		if ( clips ) {

			take* live = new take ( *self );

			base::print_line ( L"  recording overview.gif" );
			learn ( *live );
			record ( *live, L"overview.gif", 6.0, [ ] ( double t ) { } );
			delete live;

			take* flowing = new take ( *self );

			self->settings.chart_update = 3;
			learn ( *flowing );
			base::print_line ( L"  recording graph-continuous.gif" );
			record ( *flowing, L"graph-continuous.gif", 4.0, [ ] ( double t ) { } );
			self->settings.chart_update = 0;
			delete flowing;

			take* tour = new take ( *self );
			bool pressed [ 12 ] = { };

			base::print_line ( L"  recording pages.gif" );
			learn ( *tour );
			self->cursor = true;
			self->cursor_x = 760.0f;
			self->cursor_y = 470.0f;
			record ( *tour, L"pages.gif", 9.0, [ & ] ( double t ) {

				if ( once ( t, 0.2, pressed [ 0 ] ) )
					tour->glide_to ( ui::CONTROL_NAV, ui::PAGE_ACTIVITY, 0.7, -20.0f );

				if ( once ( t, 1.0, pressed [ 1 ] ) )
					tour->press ( );

				if ( once ( t, 1.1, pressed [ 2 ] ) )
					tour->release ( );

				if ( once ( t, 2.4, pressed [ 3 ] ) )
					tour->glide_to ( ui::CONTROL_TAB, 1, 0.6 );

				if ( once ( t, 3.1, pressed [ 4 ] ) )
					tour->press ( );

				if ( once ( t, 3.2, pressed [ 5 ] ) )
					tour->release ( );

				if ( once ( t, 4.4, pressed [ 6 ] ) )
					tour->glide_to ( ui::CONTROL_NAV, ui::PAGE_SETTINGS, 0.7, -20.0f );

				if ( once ( t, 5.2, pressed [ 7 ] ) )
					tour->press ( );

				if ( once ( t, 5.3, pressed [ 8 ] ) )
					tour->release ( );

				if ( once ( t, 6.3, pressed [ 9 ] ) )
					tour->glide ( 720.0f, 520.0f, 0.5 );

				if ( once ( t, 7.0, pressed [ 10 ] ) )
					self->view->wheel ( 720.0f, 520.0f, -360.0f, self->clock );

				if ( once ( t, 8.0, pressed [ 11 ] ) )
					self->view->wheel ( 720.0f, 520.0f, 360.0f, self->clock );

			} );

			delete tour;

			take* tweak = new take ( *self );
			bool steps [ 12 ] = { };
			render::box knob;

			base::print_line ( L"  recording settings.gif" );
			learn ( *tweak );
			self->view->open_page ( ui::PAGE_SETTINGS, self->clock );
			self->clock += 2.0;
			render_into ( *self, self->video );
			self->view->locate ( ui::CONTROL_SLIDER, ui::SETTING_CPU_LIMIT, knob );
			self->cursor_x = 760.0f;
			self->cursor_y = 520.0f;
			record ( *tweak, L"settings.gif", 11.0, [ & ] ( double t ) {

				if ( once ( t, 0.2, steps [ 0 ] ) )
					tweak->glide ( 330.0f, knob.middle_y ( ), 0.6 );

				if ( once ( t, 2.2, steps [ 1 ] ) )
					tweak->glide ( knob.right ( ) - 10.0f, knob.middle_y ( ), 0.6 );

				if ( once ( t, 3.0, steps [ 2 ] ) )
					tweak->press ( );

				if ( once ( t, 3.1, steps [ 3 ] ) )
					tweak->glide ( knob.x + knob.w * 0.55f, knob.middle_y ( ), 0.9 );

				if ( once ( t, 4.2, steps [ 4 ] ) )
					tweak->release ( );

				if ( once ( t, 4.6, steps [ 5 ] ) )
					tweak->glide_to ( ui::CONTROL_CHOICE, ui::SETTING_PRIORITY * 16 + 2, 0.6 );

				if ( once ( t, 5.3, steps [ 6 ] ) ) {

					tweak->press ( );
					tweak->release ( );

				}

				if ( once ( t, 5.8, steps [ 7 ] ) )
					tweak->glide_to ( ui::CONTROL_SWITCH, ui::SETTING_PAUSE_BATTERY, 0.8 );

				if ( once ( t, 6.7, steps [ 8 ] ) ) {

					tweak->press ( );
					tweak->release ( );

				}

				if ( once ( t, 7.6, steps [ 9 ] ) )
					handle ( *self, self->view->key ( 'Z', false, true, self->clock ) );

				if ( once ( t, 8.4, steps [ 10 ] ) )
					tweak->glide_to ( ui::CONTROL_REVEAL, 0, 0.7 );

				if ( once ( t, 9.3, steps [ 11 ] ) ) {

					tweak->press ( );
					tweak->release ( );

				}

			} );

			handle ( *self, self->view->blur ( self->clock ) );
			self->cursor = false;
			delete tweak;

		}

		self->run->stop ( );
		self->settings.cpu_limit = 100;

		pool::test_server* coins [ 2 ] = { new pool::test_server, new pool::test_server };
		static const uint8_t coin_kinds [ 2 ] = { chain::ALGORITHM_SCRYPT, chain::ALGORITHM_RANDOMX };

		for ( int32_t i = 0; i < 2; i++ ) {

			pool::server_settings coin;
			ui::pool_profile& profile = self->settings.pools [ coin_kinds [ i ] ];

			coin.algorithm = coin_kinds [ i ];
			coin.port = 0;
			coin.difficulty = coin_kinds [ i ] == chain::ALGORITHM_RANDOMX ? 400.0 : 4.0;
			coin.job_seconds = 30;

			if ( !coins [ i ]->start ( coin ) )
				continue;

			address.clear ( );
			address.add ( L"127.0.0.1:" ).number ( coins [ i ]->port ( ) );
			base::copy_text ( profile.address, base::count_of ( profile.address ), address.get ( ) );
			base::copy_text ( profile.worker, base::count_of ( profile.worker ), L"alloy.studio" );

		}

		if ( clips ) {

			take* switching = new take ( *self );
			bool marks [ 8 ] = { };

			base::print_line ( L"  recording algorithms.gif" );
			learn ( *switching );
			self->cursor = true;
			self->cursor_x = 640.0f;
			self->cursor_y = 520.0f;
			record ( *switching, L"algorithms.gif", 13.0, [ & ] ( double t ) {

				if ( once ( t, 0.2, marks [ 0 ] ) )
					switching->glide_to ( ui::CONTROL_ALGORITHM, chain::ALGORITHM_SCRYPT, 0.8 );

				if ( once ( t, 1.2, marks [ 1 ] ) ) {

					switching->press ( );
					switching->release ( );

				}

				if ( once ( t, 2.0, marks [ 2 ] ) )
					switching->glide_to ( ui::CONTROL_ALGORITHM, chain::ALGORITHM_RANDOMX, 0.5 );

				if ( once ( t, 2.7, marks [ 3 ] ) ) {

					switching->press ( );
					switching->release ( );

				}

				if ( once ( t, 3.5, marks [ 4 ] ) )
					switching->glide_to ( ui::CONTROL_TOGGLE, 0, 0.8 );

				if ( once ( t, 4.5, marks [ 5 ] ) ) {

					switching->press ( );
					switching->release ( );

				}

				if ( once ( t, 5.3, marks [ 6 ] ) )
					switching->glide ( 980.0f, 600.0f, 1.0 );

			} );

			self->cursor = false;
			delete switching;

		} else {

			self->settings.algorithm = chain::ALGORITHM_RANDOMX;
			self->run->start ( self->settings );

		}

		settle ( *self, 10.0 );
		shoot ( *self, L"randomx-dark.png" );
		self->run->stop ( );
		self->settings.algorithm = chain::ALGORITHM_SCRYPT;
		self->run->start ( self->settings );
		settle ( *self, 8.0 );
		shoot ( *self, L"scrypt-dark.png" );
		self->run->stop ( );
		self->settings.algorithm = chain::ALGORITHM_SHA256D;

		for ( pool::test_server* coin : coins ) {

			coin->stop ( );
			delete coin;

		}

		self->settings.mode = ui::MODE_BENCHMARK;
		self->settings.cpu_limit = 100;
		self->run->start ( self->settings );
		settle ( *self, 12.0 );
		self->view->open_page ( ui::PAGE_OVERVIEW, self->clock );
		shoot ( *self, L"benchmark-dark.png" );
		self->run->stop ( );

		self->settings.mode = ui::MODE_GENESIS;

		if ( clips ) {

			take* search = new take ( *self );
			bool started = false;

			base::print_line ( L"  recording genesis.gif" );
			learn ( *search );
			self->cursor = true;
			self->cursor_x = 900.0f;
			self->cursor_y = 300.0f;
			record ( *search, L"genesis.gif", 6.0, [ & ] ( double t ) {

				if ( t < 0.05 )
					search->glide_to ( ui::CONTROL_TOGGLE, 0, 0.7 );

				if ( once ( t, 0.9, started ) ) {

					search->press ( );
					search->release ( );

				}

			} );

			self->cursor = false;
			delete search;

		} else {

			self->run->start ( self->settings );

			while ( self->run->running ( ) )
				settle ( *self, 0.2 );

		}

		settle ( *self, 0.5 );
		shoot ( *self, L"genesis-dark.png" );

		self->run->stop ( );
		server->stop ( );
		delete server;
		delete self->view;
		delete self->run;
		delete self;

		return 0;

	}

	static bool render_mark ( render::surface& surface, int32_t size, base::list < uint8_t >& png ) {

		base::list < uint8_t > rgba;

		if ( !surface.open_offscreen ( size, size, 1.0f ) || !surface.begin ( render::color { } ) )
			return false;

		ui::draw_mark ( surface, 0.0f, 0.0f, static_cast < float > ( size ) );

		return surface.end ( ) && surface.capture ( rgba ) && base::encode_png ( rgba.data ( ), size, size, png );

	}

	uint32_t export_logo ( const wchar_t* path, uint32_t size ) {

		render::surface surface;
		base::list < uint8_t > png;

		if ( !render_mark ( surface, static_cast < int32_t > ( size ), png ) || !base::write_file ( path, png.data ( ), static_cast < size_t > ( png.size ( ) ) ) )
			return 1;

		return 0;

	}

	uint32_t export_icon ( const wchar_t* path ) {

		static const int32_t sizes [ ] = { 16, 20, 24, 32, 40, 48, 64, 256 };
		static constexpr int32_t count = base::count_of ( sizes );
		render::surface surface;
		base::list < uint8_t > images [ count ];
		base::list < uint8_t > file;
		uint8_t header [ 6 ] = { 0, 0, 1, 0, static_cast < uint8_t > ( count ), 0 };
		uint32_t offset = 6 + 16 * count;

		for ( int32_t i = 0; i < count; i++ ) {

			if ( !render_mark ( surface, sizes [ i ], images [ i ] ) )
				return 1;

		}

		file.append ( header, 6 );

		for ( int32_t i = 0; i < count; i++ ) {

			uint8_t entry [ 16 ] = { static_cast < uint8_t > ( sizes [ i ] >= 256 ? 0 : sizes [ i ] ), static_cast < uint8_t > ( sizes [ i ] >= 256 ? 0 : sizes [ i ] ), 0, 0, 1, 0, 32, 0 };

			base::store_little ( entry + 8, static_cast < uint32_t > ( images [ i ].size ( ) ) );
			base::store_little ( entry + 12, offset );
			file.append ( entry, 16 );
			offset += static_cast < uint32_t > ( images [ i ].size ( ) );

		}

		for ( int32_t i = 0; i < count; i++ )
			file.append ( images [ i ].data ( ), images [ i ].size ( ) );

		return base::write_file ( path, file.data ( ), static_cast < size_t > ( file.size ( ) ) ) ? 0 : 1;

	}

}

#endif
