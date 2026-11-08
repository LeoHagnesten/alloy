#pragma once

#include "ui/model.hh"
#include "base/system/threads.hh"

namespace app {

	static constexpr int32_t trial_room = 16;

	struct trial {

		uint32_t threads = 0;
		bool gpu = false;
		uint32_t intensity = 0;
		double rate = 0.0;

	};

	struct tune_result {

		uint8_t algorithm = 0;
		trial trials [ trial_room ];
		int32_t count = 0;
		uint32_t threads = 0;
		bool gpu = false;
		uint32_t intensity = 22;
		double rate = 0.0;
		bool finished = false;

	};

	bool tune ( uint8_t algorithm, const ui::preferences& start, tune_result& out, volatile uint32_t* cancel, void ( *report ) ( const trial& step, void* context ), void* context );

	class tuner {

	public:

		~tuner ( ) {

			stop ( );

		}

		bool begin ( uint8_t algorithm, const ui::preferences& start );

		void stop ( );

		bool busy ( ) const {

			return thread != nullptr && !base::atomic_load ( done );

		}

		bool finished ( tune_result& out );

		uint32_t steps ( ) const {

			return base::atomic_load ( measured );

		}

	private:

		static unsigned long main ( void* context );

		static void progress ( const trial& step, void* context );

		void* thread = nullptr;
		volatile uint32_t cancel = 0;
		volatile uint32_t done = 0;
		volatile uint32_t measured = 0;
		uint8_t kind = 0;
		ui::preferences* settings = nullptr;
		tune_result result;

	};

}
