#pragma once

#include "chain/algorithm.hh"

namespace app {

	static constexpr int32_t day_room = 400;

	struct day_record {

		uint32_t day = 0;
		uint64_t hashes = 0;
		uint32_t accepted = 0;
		uint32_t rejected = 0;
		uint32_t seconds = 0;
		double earned [ chain::ALGORITHM_COUNT ] = { };

	};

	class ledger {

	public:

		bool load ( const wchar_t* path = nullptr );

		bool save ( const wchar_t* path = nullptr );

		void add_time ( uint32_t day, uint32_t seconds, uint64_t hashes );

		void add_share ( uint32_t day, bool accepted, uint8_t algorithm, double value );

		day_record& on ( uint32_t day );

		const day_record& lifetime ( ) const {

			return total;

		}

		int32_t count ( ) const {

			return used;

		}

		const day_record& at ( int32_t index ) const {

			return days [ index ];

		}

		bool dirty ( ) const {

			return changed;

		}

	private:

		day_record days [ day_room ];
		day_record total;
		int32_t used = 0;
		bool changed = false;

	};

}
