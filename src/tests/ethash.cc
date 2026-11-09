#ifdef ALLOY_DEBUG

#include "tests/harness.hh"
#include "hash/ethash.hh"
#include "base/system/clock.hh"
#include "base/memory.hh"
#include "base/strings.hh"
#include "base/text.hh"

namespace tests {

	void ethash_tests ( ) {

		section ( L"ethash" );

		check ( hash::ethash_cache_size ( 0 ) == 16776896ull && hash::ethash_cache_size ( 1 ) == 16907456ull && hash::ethash_dataset_size ( 0 ) == 1073739904ull && hash::ethash_dataset_size ( 1 ) == 1082130304ull, L"cache and dataset sizes for the first epochs match the reference tables" );

		uint8_t seed [ 32 ];
		uint8_t later [ 32 ];

		hash::ethash_seed ( 1, seed );
		hash::ethash_seed ( 428, later );
		check ( hex_is ( seed, 32, "290decd9548b62a8d60345a988386fc84ba6bc95484008f6362f93160ef3e563" ) && hash::ethash_seed_epoch ( later ) == 428, L"the epoch 1 seed is Keccak-256 of 32 zero bytes, and a seed maps back to its epoch" );

		hash::ethash_cache* cache = new hash::ethash_cache;
		uint8_t header [ 32 ];
		uint8_t mix [ 32 ];
		uint8_t result [ 32 ];
		double began = base::seconds ( );

		hash::ethash_seed ( 0, seed );
		base::read_hex ( "372eca2454ead349c3df0ab5d00b0b706b23e49d469387db91811cee0358fc6d", header, 32 );

		bool built = cache->build ( 0, seed );
		double seconds = base::seconds ( ) - began;

		if ( built )
			cache->light ( header, 0x495732e0ed7a801cull, mix, result );

		base::text note;

		note.add ( L"light Ethash of block 22 gives its mix digest and a result under its difficulty, the 16 MB cache built in " ).decimal ( seconds, 2 ).add ( L" s" );
		check ( built && hex_is ( mix, 32, "2f74cdeb198af0b9abe65d22d372e22fb2d474371774a9583c1cc427a07939f5" ) && result [ 0 ] == 0 && result [ 1 ] == 0, note.get ( ) );
		delete cache;

	}

}

#endif
