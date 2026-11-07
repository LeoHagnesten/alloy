#pragma once

#include "randomx/compiler.hh"
#include "randomx/superscalar.hh"
#include "hash/argon2.hh"

namespace randomx {

	static constexpr uint32_t cache_blocks = 262144;
	static constexpr uint64_t cache_bytes = static_cast < uint64_t > ( cache_blocks ) * 1024;
	static constexpr uint64_t dataset_base = 2147483648ull;
	static constexpr uint64_t dataset_extra = 33554368ull;
	static constexpr uint64_t dataset_items = ( dataset_base + dataset_extra ) / 64;
	static constexpr int32_t cache_accesses = 8;
	static constexpr int32_t key_room = 60;

	class cache {

	public:

		cache ( ) = default;

		cache ( const cache& ) = delete;

		cache& operator = ( const cache& ) = delete;

		~cache ( );

		bool build ( const void* key, size_t size );

		bool matches ( const void* key, size_t size ) const;

		void item ( uint64_t number, uint64_t* out ) const;

		const uint64_t* words ( ) const {

			return reinterpret_cast < const uint64_t* > ( blocks );

		}

		bool ready ( ) const {

			return blocks != nullptr;

		}

		const scalar_compiler& compiled ( ) const {

			return native;

		}

		scalar_program* programs = nullptr;
		bool interpret = false;

	private:

		scalar_compiler native;
		hash::argon2_block* blocks = nullptr;
		uint8_t key [ key_room ] = { };
		size_t key_size = 0;

	};

	class dataset {

	public:

		dataset ( ) = default;

		dataset ( const dataset& ) = delete;

		dataset& operator = ( const dataset& ) = delete;

		~dataset ( );

		bool build ( const cache& source, uint32_t threads, volatile uint32_t* progress, volatile uint32_t* cancel );

		void release ( );

		const uint8_t* data ( ) const {

			return memory;

		}

	private:

		uint8_t* memory = nullptr;

	};

}
