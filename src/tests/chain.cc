#ifdef ALLOY_DEBUG

#include "tests/harness.hh"
#include "chain/header.hh"
#include "chain/merkle.hh"
#include "chain/target.hh"
#include "pool/job.hh"
#include "base/bits.hh"
#include "base/math.hh"
#include "base/memory.hh"
#include "base/strings.hh"

namespace tests {

	static const char genesis_coinbase [ ] = "01000000010000000000000000000000000000000000000000000000000000000000000000ffffffff4d04ffff001d0104455468652054696d65732030332f4a616e2f32303039204368616e63656c6c6f72206f6e206272696e6b206f66207365636f6e64206261696c6f757420666f722062616e6b73ffffffff0100f2052a01000000434104678afdb0fe5548271967f1a67130b7105cd6a828e03909a67962e0ea1f61deb649f6bc3f4cef38c4f35504e51ec112de5c384df7ba0b8d578a4c702b6bf11d5fac00000000";

	static void read_display ( const char* text, uint8_t* out ) {

		base::read_hex ( text, out, 32 );
		base::reverse_bytes ( out, 32 );

	}

	void chain_tests ( ) {

		section ( L"bitcoin headers" );

		chain::header source;
		uint8_t full [ 32 ];
		uint8_t fast [ 32 ];

		chain::genesis ( source );
		hash::sha256_twice ( source.bytes, chain::header_size, full );
		check ( hex_is ( full, 32, "6fe28c0ab6f1b372c1a6a246ae63f74f931e8365e15a089c68d6190000000000" ), L"the genesis header hashes to block 000000000019d668..." );
		check ( source.version ( ) == 1 && source.time ( ) == 1231006505 && source.bits ( ) == 0x1D00FFFF && source.nonce ( ) == 2083236893, L"the genesis header's fields read back as version 1, January 3 2009, 1d00ffff and nonce 2083236893" );

		chain::header_hasher hasher;

		hasher.prepare ( source, hash::sha256_block );
		hasher.hash ( source.nonce ( ), fast );
		check ( base::same ( fast, full, 32 ), L"the midstate path gives the same hash as hashing all 80 bytes" );

		bool agrees = true;

		for ( uint32_t nonce = 0; nonce < 64 && agrees; nonce++ ) {

			chain::header copy = source;

			copy.set_nonce ( nonce * 2654435761u );
			hash::sha256_twice ( copy.bytes, chain::header_size, full );
			hasher.hash ( nonce * 2654435761u, fast );
			agrees = base::same ( fast, full, 32 );

		}

		check ( agrees, L"the midstate path agrees with the plain one for 64 scattered nonces" );

		uint8_t target [ 32 ];
		uint8_t wanted [ 32 ];

		chain::target_of ( 0x1D00FFFF, target );
		base::read_hex ( "0000000000000000000000000000000000000000000000000000ffff00000000", wanted, 32 );
		check ( base::same ( target, wanted, 32 ), L"bits 1d00ffff expand to the difficulty 1 target" );

		chain::target_for ( 1.0, target );
		check ( base::same ( target, wanted, 32 ), L"pool difficulty 1 gives the same target" );

		hasher.hash ( source.nonce ( ), fast );
		check ( chain::meets_bits ( fast, source.bits ( ) ), L"the genesis hash meets its own target" );
		check ( chain::meets_bits ( fast, 0x1E00FFFF ) && !chain::meets_bits ( fast, 0x1B00FFFF ), L"the genesis hash passes an easier target and fails one that needs 48 zero bits" );
		check ( chain::leading_zero_bits ( fast ) == 43, L"the genesis hash starts with 43 zero bits" );

		double difficulty = chain::difficulty_of ( chain::top_bits ( fast ) );
		uint8_t easier [ 32 ];
		uint8_t harder [ 32 ];

		chain::target_for ( 2536.0, easier );
		chain::target_for ( 2537.0, harder );
		check ( base::abs ( difficulty - 2536.4 ) < 0.1 && chain::meets ( fast, easier ) && !chain::meets ( fast, harder ), L"the genesis hash is worth difficulty 2536.4 and pool targets bracket it" );
		check ( chain::difficulty_of_bits ( 0x1D00FFFF ) == 1.0 && base::abs ( chain::difficulty_of_bits ( 0x1B0404CB ) - 16307.42 ) < 0.01, L"network bits convert to difficulty 1 and 16307.42" );

		hasher.hash ( source.nonce ( ) + 1, fast );
		check ( !chain::meets_bits ( fast, source.bits ( ) ), L"the next nonce misses difficulty 1" );

		section ( L"merkle trees and stratum jobs" );

		uint8_t coinbase [ 256 ];
		size_t coinbase_size = sizeof ( genesis_coinbase ) / 2;
		uint8_t leaf [ 32 ];

		base::read_hex ( genesis_coinbase, coinbase, coinbase_size );
		hash::sha256_twice ( coinbase, coinbase_size, leaf );
		check ( base::same ( leaf, source.merkle_root ( ), 32 ), L"the genesis coinbase transaction hashes to the header's merkle root" );

		uint8_t ids [ 4 * 32 ];
		uint8_t root [ 32 ];
		uint8_t expected [ 32 ];

		read_display ( "8c14f0db3df150123e6f3dbbf30f8b955a8249b62ac1d1ff16284aefa3d06d87", ids );
		read_display ( "fff2525b8931402dd09222c50775608f75787bd2b87e56995a7bdd30f79702c4", ids + 32 );
		read_display ( "6359f0868171b1d194cbee1af2f16ea598ae8fad666d9b012c8ed2b79a236ec4", ids + 64 );
		read_display ( "e9a66845e05d5abc0ad04ec80f774a7e585c6e8db975962d069a522137b80c1d", ids + 96 );
		read_display ( "f3e94742aca4b5ef85488dc37c06c3282295ffec960994b2c0d5ac2a25a95766", expected );
		chain::merkle_tree ( ids, 4, root );
		check ( base::same ( root, expected, 32 ), L"block 100000's four transactions build its merkle root" );

		uint8_t leaves [ 7 * 32 ];
		uint8_t branches [ 8 * 32 ];
		int32_t depth = 0;
		bool folds = true;

		for ( int32_t count = 1; count <= 7 && folds; count++ ) {

			for ( int32_t i = 0; i < count * 32; i++ )
				leaves [ i ] = static_cast < uint8_t > ( i * 31 + count );

			chain::merkle_tree ( leaves, count, root );
			chain::merkle_branches ( leaves, count, branches, depth );
			chain::merkle_fold ( leaves, branches, depth, expected );
			folds = base::same ( root, expected, 32 );

		}

		check ( folds, L"folding the coinbase through stratum branches matches the full tree for 1 to 7 transactions" );

		pool::job work;
		pool::session_nonce nonce;
		chain::header built;

		base::copy ( work.head, coinbase, 42 );
		work.head_size = 42;
		base::copy ( nonce.first, coinbase + 42, 4 );
		nonce.first_size = 4;
		nonce.second_size = 4;
		base::copy ( work.tail, coinbase + 50, coinbase_size - 50 );
		work.tail_size = static_cast < int32_t > ( coinbase_size - 50 );
		work.version = 1;
		work.bits = 0x1D00FFFF;
		work.time = 0x495FAB29;
		pool::build_header ( work, nonce, coinbase + 46, work.time, built );
		built.set_nonce ( 2083236893 );
		check ( base::same ( built.bytes, source.bytes, chain::header_size ), L"a stratum job cut from the genesis coinbase rebuilds the genesis header byte for byte" );

		uint8_t stratum_previous [ 32 ];
		uint8_t block_one_root [ 32 ];

		base::read_hex ( "0a8ce26f72b3f1b646a2a6c14ff763ae65831e939c085ae10019d66800000000", stratum_previous, 32 );
		read_display ( "0e3e2357e806b6cdb1f70b54c3a3a17b6714ee1f0e68bebb44a74b1efd512098", block_one_root );
		base::copy ( work.previous, stratum_previous, 32 );
		work.version = 1;
		work.bits = 0x1D00FFFF;
		pool::assemble ( work, block_one_root, 1231469665, built );
		built.set_nonce ( 2573394689u );
		hash::sha256_twice ( built.bytes, chain::header_size, full );
		read_display ( "00000000839a8e6886ab5951d76f411475428afc90947ee320161bbf18eb6048", expected );
		check ( base::same ( full, expected, 32 ), L"block 1 assembles from a stratum-ordered previous hash and hashes to 00000000839a8e68..." );

	}

}

#endif
