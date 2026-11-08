#pragma once

#include "base/containers/list.hh"

namespace pool {

	namespace sv2 {

		enum message : uint8_t {

			SETUP_CONNECTION = 0x00,
			SETUP_SUCCESS = 0x01,
			SETUP_ERROR = 0x02,
			RECONNECT = 0x04,
			OPEN_STANDARD_CHANNEL = 0x10,
			OPEN_STANDARD_SUCCESS = 0x11,
			OPEN_CHANNEL_ERROR = 0x12,
			NEW_MINING_JOB = 0x15,
			SUBMIT_SHARES_STANDARD = 0x1A,
			SUBMIT_SUCCESS = 0x1C,
			SUBMIT_ERROR = 0x1D,
			SET_NEW_PREV_HASH = 0x20,
			SET_TARGET = 0x21,

		};

		static constexpr uint16_t channel_message = 0x8000;

	}

	struct sv2_writer {

		base::list < uint8_t > bytes;

		void u8 ( uint8_t value );

		void u16 ( uint16_t value );

		void u32 ( uint32_t value );

		void u64 ( uint64_t value );

		void f32 ( float value );

		void text ( const char* value );

		void raw ( const uint8_t* value, int32_t size );

	};

	struct sv2_reader {

		const uint8_t* data = nullptr;
		int32_t size = 0;
		int32_t at = 0;

		bool u8 ( uint8_t& out );

		bool u16 ( uint16_t& out );

		bool u32 ( uint32_t& out );

		bool u64 ( uint64_t& out );

		bool raw ( uint8_t* out, int32_t count );

		bool text ( char* out, int32_t room );

		bool skip_bytes ( );

	};

	bool split_sv2_address ( const wchar_t* address, wchar_t* host_port, int32_t room, char* key, int32_t key_room );

}
