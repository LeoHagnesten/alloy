#pragma once

#include "randomx/compiler.hh"
#include "randomx/superscalar.hh"
#include "randomx/variant.hh"
#include "hash/argon2.hh"

namespace randomx {

	static constexpr uint32_t cache_blocks = 262144;
	static constexpr uint64_t cache_bytes = static_cast < uint64_t > ( cache_blocks ) * 1024;
	static constexpr uint64_t dataset_base = 2147483648ull;
	static constexpr uint64_t dataset_extra = 33554368ull;
	static constexpr uint64_t dataset_items = ( dataset_base + dataset_extra ) / 64;
	static constexpr uint32_t dataset_megabytes = 2080;
	static constexpr uint32_t cache_megabytes = 256;
	static constexpr int32_t cache_accesses = 8;
	static constexpr int32_t key_room = 60;

	class cache {

	public:

		cache ( ) = default;

		cache ( const cache& ) = delete;

		cache& operator = ( const cache& ) = delete;

		~cache ( );

		bool build ( const void* key, size_t size, uint8_t kind = VARIANT_RX0 );

		bool matches ( const void* key, size_t size, uint8_t kind = VARIANT_RX0 ) const;

		void item ( uint64_t number, uint64_t* out ) const;

		const uint64_t* words ( ) const {

			return reinterpret_cast < const uint64_t* > ( blocks );

		}

		bool ready ( ) const {

			return blocks != nullptr;

		}

		bool large_pages ( ) const {

			return large;

		}

		uint8_t kind ( ) const {

			return built_kind;

		}

		const scalar_compiler& compiled ( ) const {

			return native;

		}

		scalar_program* programs = nullptr;
		bool interpret = false;

	private:

		scalar_compiler native;
		hash::argon2_block* blocks = nullptr;
		bool large = false;
		uint8_t key [ key_room ] = { };
		size_t key_size = 0;
		uint8_t built_kind = VARIANT_RX0;

	};

	uint64_t items_for ( uint32_t megabytes );

	class dataset {

	public:

		dataset ( ) = default;

		dataset ( const dataset& ) = delete;

		dataset& operator = ( const dataset& ) = delete;

		~dataset ( );

		bool build ( const cache& source, uint64_t items, uint32_t threads, volatile uint32_t* progress, volatile uint32_t* cancel );

		void release ( );

		const uint8_t* data ( ) const {

			return memory;

		}

		uint64_t count ( ) const {

			return held;

		}

		bool complete ( ) const {

			return memory && held == dataset_items;

		}

		bool large_pages ( ) const {

			return large;

		}

	private:

		uint8_t* memory = nullptr;
		uint64_t held = 0;
		bool large = false;

	};

}
