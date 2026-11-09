#pragma once

#include "base/base.hh"

namespace hash {

	static constexpr uint32_t ethash_accesses = 64;
	static constexpr uint32_t ethash_parents = 256;
	static constexpr uint32_t ethash_epoch_limit = 2048;

	uint64_t ethash_cache_size ( uint32_t epoch );

	uint64_t ethash_dataset_size ( uint32_t epoch );

	void ethash_seed ( uint32_t epoch, uint8_t* out );

	int32_t ethash_seed_epoch ( const uint8_t* seed );

	inline uint32_t ethash_fnv ( uint32_t a, uint32_t b ) {

		return a * 0x01000193u ^ b;

	}

	class ethash_cache {

	public:

		ethash_cache ( ) = default;

		ethash_cache ( const ethash_cache& ) = delete;

		ethash_cache& operator = ( const ethash_cache& ) = delete;

		~ethash_cache ( );

		bool build ( uint32_t epoch, const uint8_t* seed, volatile uint32_t* stop = nullptr );

		void item ( uint32_t index, uint32_t* out ) const;

		void light ( const uint8_t* header, uint64_t nonce, uint8_t* mix, uint8_t* result ) const;

		const uint32_t* words ( ) const {

			return cells;

		}

		uint64_t bytes ( ) const {

			return cache_bytes;

		}

		uint64_t dataset_bytes ( ) const {

			return full_bytes;

		}

		uint32_t epoch ( ) const {

			return built_epoch;

		}

		bool ready ( ) const {

			return cells != nullptr;

		}

	private:

		uint32_t* cells = nullptr;
		uint64_t cache_bytes = 0;
		uint64_t full_bytes = 0;
		uint32_t built_epoch = 0;

	};

}
