#pragma once

#include "ui/model.hh"
#include "base/system/threads.hh"

namespace app {

	bool parse_rig ( const wchar_t* text, ui::rig_entry& out );

	class rig_watch {

	public:

		~rig_watch ( ) {

			stop ( );

		}

		void configure ( const ui::rig_entry* rigs, int32_t count );

		void read ( ui::rig_status* out, int32_t& count );

		void stop ( );

	private:

		static unsigned long main ( void* context );

		void run ( );

		void* thread = nullptr;
		volatile uint32_t quit = 0;
		base::lock guard_lock;
		ui::rig_entry wanted [ ui::rig_room ];
		ui::rig_status found [ ui::rig_room ];
		int32_t count = 0;

	};

}
