#include "hash/ethash.hh"
#include "hash/keccak.hh"
#include "base/bits.hh"
#include "base/memory.hh"
#include "base/system/threads.hh"

namespace hash {

	static bool prime ( uint64_t value ) {

		if ( value < 2 )
			return false;

		if ( value % 2 == 0 )
			return value == 2;

		for ( uint64_t divisor = 3; divisor * divisor <= value; divisor += 2 ) {

			if ( value % divisor == 0 )
				return false;

		}

		return true;

	}

	uint64_t ethash_cache_size ( uint32_t epoch ) {

		uint64_t size = ( 1ull << 24 ) + ( 1ull << 17 ) * epoch - 64;

		while ( !prime ( size / 64 ) )
			size -= 128;

		return size;

	}

	uint64_t ethash_dataset_size ( uint32_t epoch ) {

		uint64_t size = ( 1ull << 30 ) + ( 1ull << 23 ) * epoch - 128;

		while ( !prime ( size / 128 ) )
			size -= 256;

		return size;

	}

	void ethash_seed ( uint32_t epoch, uint8_t* out ) {

		base::zero ( out, 32 );

		for ( uint32_t i = 0; i < epoch; i++ )
			keccak_256 ( out, 32, out );

	}

	int32_t ethash_seed_epoch ( const uint8_t* seed ) {

		uint8_t walk [ 32 ] = { };

		for ( uint32_t i = 0; i < ethash_epoch_limit; i++ ) {

			if ( base::same ( walk, seed, 32 ) )
				return static_cast < int32_t > ( i );

			keccak_256 ( walk, 32, walk );

		}

		return -1;

	}

	ethash_cache::~ethash_cache ( ) {

		base::release_pages ( cells );

	}

	bool ethash_cache::build ( uint32_t epoch, const uint8_t* seed, volatile uint32_t* stop ) {

		base::release_pages ( cells );
		cells = nullptr;
		cache_bytes = ethash_cache_size ( epoch );
		full_bytes = ethash_dataset_size ( epoch );

		uint8_t* bytes = static_cast < uint8_t* > ( base::allocate_pages ( static_cast < size_t > ( cache_bytes ) ) );

		if ( !bytes )
			return false;

		uint32_t count = static_cast < uint32_t > ( cache_bytes / 64 );

		keccak_512 ( seed, 32, bytes );

		for ( uint32_t i = 1; i < count; i++ )
			keccak_512 ( bytes + ( i - 1 ) * 64ull, 64, bytes + i * 64ull );

		const uint32_t* view = reinterpret_cast < const uint32_t* > ( bytes );

		for ( int32_t round = 0; round < 3; round++ ) {

			for ( uint32_t i = 0; i < count; i++ ) {

				if ( stop && ( i & 0xFFFF ) == 0 && base::atomic_load ( *stop ) ) {

					base::release_pages ( bytes );

					return false;

				}

				uint8_t mixed [ 64 ];
				const uint8_t* previous = bytes + ( ( i + count - 1 ) % count ) * 64ull;
				const uint8_t* other = bytes + ( view [ i * 16ull ] % count ) * 64ull;

				for ( int32_t k = 0; k < 64; k++ )
					mixed [ k ] = previous [ k ] ^ other [ k ];

				keccak_512 ( mixed, 64, bytes + i * 64ull );

			}

		}

		cells = reinterpret_cast < uint32_t* > ( bytes );
		built_epoch = epoch;

		return true;

	}

	void ethash_cache::item ( uint32_t index, uint32_t* out ) const {

		uint32_t count = static_cast < uint32_t > ( cache_bytes / 64 );
		uint32_t mix [ 16 ];

		base::copy ( mix, cells + ( index % count ) * 16ull, 64 );
		mix [ 0 ] ^= index;
		keccak_512 ( mix, 64, reinterpret_cast < uint8_t* > ( mix ) );

		for ( uint32_t j = 0; j < ethash_parents; j++ ) {

			const uint32_t* parent = cells + ( ethash_fnv ( index ^ j, mix [ j % 16 ] ) % count ) * 16ull;

			for ( int32_t k = 0; k < 16; k++ )
				mix [ k ] = ethash_fnv ( mix [ k ], parent [ k ] );

		}

		keccak_512 ( mix, 64, reinterpret_cast < uint8_t* > ( out ) );

	}

	void ethash_cache::light ( const uint8_t* header, uint64_t nonce, uint8_t* mix_out, uint8_t* result ) const {

		uint8_t start [ 40 ];
		uint32_t seed [ 16 ];
		uint32_t mix [ 32 ];
		uint32_t rows = static_cast < uint32_t > ( full_bytes / 128 );

		base::copy ( start, header, 32 );
		base::store_little64 ( start + 32, nonce );
		keccak_512 ( start, 40, reinterpret_cast < uint8_t* > ( seed ) );
		base::copy ( mix, seed, 64 );
		base::copy ( mix + 16, seed, 64 );

		for ( uint32_t i = 0; i < ethash_accesses; i++ ) {

			uint32_t row = ethash_fnv ( i ^ seed [ 0 ], mix [ i % 32 ] ) % rows;
			uint32_t first [ 16 ];
			uint32_t second [ 16 ];

			item ( row * 2, first );
			item ( row * 2 + 1, second );

			for ( int32_t k = 0; k < 16; k++ ) {

				mix [ k ] = ethash_fnv ( mix [ k ], first [ k ] );
				mix [ 16 + k ] = ethash_fnv ( mix [ 16 + k ], second [ k ] );

			}

		}

		uint32_t folded [ 8 ];
		uint8_t final [ 96 ];

		for ( int32_t i = 0; i < 8; i++ )
			folded [ i ] = ethash_fnv ( ethash_fnv ( ethash_fnv ( mix [ i * 4 ], mix [ i * 4 + 1 ] ), mix [ i * 4 + 2 ] ), mix [ i * 4 + 3 ] );

		base::copy ( mix_out, folded, 32 );
		base::copy ( final, seed, 64 );
		base::copy ( final + 64, folded, 32 );
		keccak_256 ( final, 96, result );

	}

}
