#include "chain/merkle.hh"
#include "base/memory.hh"
#include "hash/sha256.hh"

namespace chain {

	static void pair ( const uint8_t* left, const uint8_t* right, uint8_t* out ) {

		uint8_t joined [ 64 ];

		base::copy ( joined, left, 32 );
		base::copy ( joined + 32, right, 32 );
		hash::sha256_twice ( joined, 64, out );

	}

	void merkle_fold ( const uint8_t* leaf, const uint8_t* branches, int32_t count, uint8_t* out ) {

		uint8_t running [ 32 ];

		base::copy ( running, leaf, 32 );

		for ( int32_t i = 0; i < count; i++ )
			pair ( running, branches + i * 32, running );

		base::copy ( out, running, 32 );

	}

	void merkle_tree ( const uint8_t* leaves, int32_t count, uint8_t* out ) {

		if ( count <= 0 ) {

			base::zero ( out, 32 );

			return;

		}

		uint8_t* level = new uint8_t [ static_cast < size_t > ( count + 1 ) * 32 ];
		int32_t size = count;

		base::copy ( level, leaves, static_cast < size_t > ( count ) * 32 );

		while ( size > 1 ) {

			if ( size & 1 ) {

				base::copy ( level + size * 32, level + ( size - 1 ) * 32, 32 );
				size++;

			}

			for ( int32_t i = 0; i < size / 2; i++ )
				pair ( level + i * 64, level + i * 64 + 32, level + i * 32 );

			size /= 2;

		}

		base::copy ( out, level, 32 );
		delete [ ] level;

	}

	void merkle_branches ( const uint8_t* leaves, int32_t count, uint8_t* out, int32_t& depth ) {

		depth = 0;

		if ( count <= 1 )
			return;

		uint8_t* level = new uint8_t [ static_cast < size_t > ( count + 1 ) * 32 ];
		int32_t size = count;

		base::copy ( level, leaves, static_cast < size_t > ( count ) * 32 );

		while ( size > 1 ) {

			if ( size & 1 ) {

				base::copy ( level + size * 32, level + ( size - 1 ) * 32, 32 );
				size++;

			}

			base::copy ( out + depth * 32, level + 32, 32 );
			depth++;

			for ( int32_t i = 1; i < size / 2; i++ )
				pair ( level + i * 64, level + i * 64 + 32, level + i * 32 );

			size /= 2;

		}

		delete [ ] level;

	}

}
