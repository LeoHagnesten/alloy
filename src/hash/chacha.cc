#include "hash/chacha.hh"
#include "hash/sha256.hh"
#include "base/bits.hh"
#include "base/memory.hh"
#include "base/strings.hh"

namespace hash {

	static inline uint32_t spin ( uint32_t value, uint32_t count ) {

		return value << count | value >> ( 32 - count );

	}

	static inline void quarter ( uint32_t* x, int32_t a, int32_t b, int32_t c, int32_t d ) {

		x [ a ] += x [ b ];
		x [ d ] = spin ( x [ d ] ^ x [ a ], 16 );
		x [ c ] += x [ d ];
		x [ b ] = spin ( x [ b ] ^ x [ c ], 12 );
		x [ a ] += x [ b ];
		x [ d ] = spin ( x [ d ] ^ x [ a ], 8 );
		x [ c ] += x [ d ];
		x [ b ] = spin ( x [ b ] ^ x [ c ], 7 );

	}

	void chacha20_block ( const uint8_t* key, uint32_t counter, const uint8_t* nonce, uint8_t* out ) {

		uint32_t start [ 16 ] = { 0x61707865, 0x3320646E, 0x79622D32, 0x6B206574 };
		uint32_t x [ 16 ];

		for ( int32_t i = 0; i < 8; i++ )
			start [ 4 + i ] = base::load_little ( key + i * 4 );

		start [ 12 ] = counter;

		for ( int32_t i = 0; i < 3; i++ )
			start [ 13 + i ] = base::load_little ( nonce + i * 4 );

		base::copy ( x, start, sizeof ( x ) );

		for ( int32_t round = 0; round < 10; round++ ) {

			quarter ( x, 0, 4, 8, 12 );
			quarter ( x, 1, 5, 9, 13 );
			quarter ( x, 2, 6, 10, 14 );
			quarter ( x, 3, 7, 11, 15 );
			quarter ( x, 0, 5, 10, 15 );
			quarter ( x, 1, 6, 11, 12 );
			quarter ( x, 2, 7, 8, 13 );
			quarter ( x, 3, 4, 9, 14 );

		}

		for ( int32_t i = 0; i < 16; i++ )
			base::store_little ( out + i * 4, x [ i ] + start [ i ] );

	}

	void chacha20_xor ( const uint8_t* key, uint32_t counter, const uint8_t* nonce, const uint8_t* in, uint8_t* out, size_t size ) {

		uint8_t stream [ 64 ];

		for ( size_t done = 0; done < size; done += 64 ) {

			chacha20_block ( key, counter++, nonce, stream );

			size_t take = base::min ( size - done, static_cast < size_t > ( 64 ) );

			for ( size_t i = 0; i < take; i++ )
				out [ done + i ] = in [ done + i ] ^ stream [ i ];

		}

	}

	void poly1305 ( const uint8_t* key, const uint8_t* data, size_t size, uint8_t* tag ) {

		static constexpr uint32_t mask = 0x3FFFFFF;
		uint32_t r0 = base::load_little ( key ) & 0x3FFFFFF;
		uint32_t r1 = ( base::load_little ( key + 3 ) >> 2 ) & 0x3FFFF03;
		uint32_t r2 = ( base::load_little ( key + 6 ) >> 4 ) & 0x3FFC0FF;
		uint32_t r3 = ( base::load_little ( key + 9 ) >> 6 ) & 0x3F03FFF;
		uint32_t r4 = ( base::load_little ( key + 12 ) >> 8 ) & 0x00FFFFF;
		uint32_t s1 = r1 * 5;
		uint32_t s2 = r2 * 5;
		uint32_t s3 = r3 * 5;
		uint32_t s4 = r4 * 5;
		uint32_t h0 = 0;
		uint32_t h1 = 0;
		uint32_t h2 = 0;
		uint32_t h3 = 0;
		uint32_t h4 = 0;

		for ( size_t at = 0; at < size; at += 16 ) {

			uint8_t block [ 16 ] = { };
			size_t take = base::min ( size - at, static_cast < size_t > ( 16 ) );
			uint32_t high = take == 16 ? 1u << 24 : 0;

			base::copy ( block, data + at, take );

			if ( take < 16 )
				block [ take ] = 1;

			h0 += base::load_little ( block ) & mask;
			h1 += ( base::load_little ( block + 3 ) >> 2 ) & mask;
			h2 += ( base::load_little ( block + 6 ) >> 4 ) & mask;
			h3 += ( base::load_little ( block + 9 ) >> 6 ) & mask;
			h4 += ( base::load_little ( block + 12 ) >> 8 ) | high;

			uint64_t d0 = static_cast < uint64_t > ( h0 ) * r0 + static_cast < uint64_t > ( h1 ) * s4 + static_cast < uint64_t > ( h2 ) * s3 + static_cast < uint64_t > ( h3 ) * s2 + static_cast < uint64_t > ( h4 ) * s1;
			uint64_t d1 = static_cast < uint64_t > ( h0 ) * r1 + static_cast < uint64_t > ( h1 ) * r0 + static_cast < uint64_t > ( h2 ) * s4 + static_cast < uint64_t > ( h3 ) * s3 + static_cast < uint64_t > ( h4 ) * s2;
			uint64_t d2 = static_cast < uint64_t > ( h0 ) * r2 + static_cast < uint64_t > ( h1 ) * r1 + static_cast < uint64_t > ( h2 ) * r0 + static_cast < uint64_t > ( h3 ) * s4 + static_cast < uint64_t > ( h4 ) * s3;
			uint64_t d3 = static_cast < uint64_t > ( h0 ) * r3 + static_cast < uint64_t > ( h1 ) * r2 + static_cast < uint64_t > ( h2 ) * r1 + static_cast < uint64_t > ( h3 ) * r0 + static_cast < uint64_t > ( h4 ) * s4;
			uint64_t d4 = static_cast < uint64_t > ( h0 ) * r4 + static_cast < uint64_t > ( h1 ) * r3 + static_cast < uint64_t > ( h2 ) * r2 + static_cast < uint64_t > ( h3 ) * r1 + static_cast < uint64_t > ( h4 ) * r0;
			uint64_t carry = d0 >> 26;

			h0 = static_cast < uint32_t > ( d0 ) & mask;
			d1 += carry;
			carry = d1 >> 26;
			h1 = static_cast < uint32_t > ( d1 ) & mask;
			d2 += carry;
			carry = d2 >> 26;
			h2 = static_cast < uint32_t > ( d2 ) & mask;
			d3 += carry;
			carry = d3 >> 26;
			h3 = static_cast < uint32_t > ( d3 ) & mask;
			d4 += carry;
			carry = d4 >> 26;
			h4 = static_cast < uint32_t > ( d4 ) & mask;
			h0 += static_cast < uint32_t > ( carry ) * 5;
			h1 += h0 >> 26;
			h0 &= mask;

		}

		uint32_t carry = h1 >> 26;

		h1 &= mask;
		h2 += carry;
		carry = h2 >> 26;
		h2 &= mask;
		h3 += carry;
		carry = h3 >> 26;
		h3 &= mask;
		h4 += carry;
		carry = h4 >> 26;
		h4 &= mask;
		h0 += carry * 5;
		carry = h0 >> 26;
		h0 &= mask;
		h1 += carry;

		uint32_t g0 = h0 + 5;

		carry = g0 >> 26;
		g0 &= mask;

		uint32_t g1 = h1 + carry;

		carry = g1 >> 26;
		g1 &= mask;

		uint32_t g2 = h2 + carry;

		carry = g2 >> 26;
		g2 &= mask;

		uint32_t g3 = h3 + carry;

		carry = g3 >> 26;
		g3 &= mask;

		uint32_t g4 = h4 + carry - ( 1u << 26 );
		uint32_t keep = ( g4 >> 31 ) - 1;

		g0 &= keep;
		g1 &= keep;
		g2 &= keep;
		g3 &= keep;
		g4 &= keep;
		keep = ~keep;
		h0 = ( h0 & keep ) | g0;
		h1 = ( h1 & keep ) | g1;
		h2 = ( h2 & keep ) | g2;
		h3 = ( h3 & keep ) | g3;
		h4 = ( h4 & keep ) | g4;

		uint32_t w0 = h0 | h1 << 26;
		uint32_t w1 = h1 >> 6 | h2 << 20;
		uint32_t w2 = h2 >> 12 | h3 << 14;
		uint32_t w3 = h3 >> 18 | h4 << 8;
		uint64_t sum = static_cast < uint64_t > ( w0 ) + base::load_little ( key + 16 );

		base::store_little ( tag, static_cast < uint32_t > ( sum ) );
		sum = static_cast < uint64_t > ( w1 ) + base::load_little ( key + 20 ) + ( sum >> 32 );
		base::store_little ( tag + 4, static_cast < uint32_t > ( sum ) );
		sum = static_cast < uint64_t > ( w2 ) + base::load_little ( key + 24 ) + ( sum >> 32 );
		base::store_little ( tag + 8, static_cast < uint32_t > ( sum ) );
		sum = static_cast < uint64_t > ( w3 ) + base::load_little ( key + 28 ) + ( sum >> 32 );
		base::store_little ( tag + 12, static_cast < uint32_t > ( sum ) );

	}

	static void aead_tag ( const uint8_t* key, const uint8_t* nonce, const uint8_t* aad, size_t aad_size, const uint8_t* cipher, size_t size, uint8_t* tag ) {

		uint8_t block [ 64 ];
		size_t padded_aad = ( aad_size + 15 ) / 16 * 16;
		size_t padded = ( size + 15 ) / 16 * 16;
		size_t total = padded_aad + padded + 16;
		uint8_t* message = new uint8_t [ total ];

		chacha20_block ( key, 0, nonce, block );
		base::zero ( message, total );

		if ( aad_size )
			base::copy ( message, aad, aad_size );

		if ( size )
			base::copy ( message + padded_aad, cipher, size );

		base::store_little64 ( message + padded_aad + padded, aad_size );
		base::store_little64 ( message + padded_aad + padded + 8, size );
		poly1305 ( block, message, total, tag );
		delete [ ] message;
		base::zero ( block, sizeof ( block ) );

	}

	void aead_seal ( const uint8_t* key, const uint8_t* nonce, const uint8_t* aad, size_t aad_size, const uint8_t* plain, size_t size, uint8_t* out ) {

		chacha20_xor ( key, 1, nonce, plain, out, size );
		aead_tag ( key, nonce, aad, aad_size, out, size, out + size );

	}

	bool aead_open ( const uint8_t* key, const uint8_t* nonce, const uint8_t* aad, size_t aad_size, const uint8_t* sealed, size_t size, uint8_t* out ) {

		if ( size < 16 )
			return false;

		uint8_t tag [ 16 ];
		uint8_t difference = 0;

		aead_tag ( key, nonce, aad, aad_size, sealed, size - 16, tag );

		for ( int32_t i = 0; i < 16; i++ )
			difference |= tag [ i ] ^ sealed [ size - 16 + i ];

		if ( difference )
			return false;

		chacha20_xor ( key, 1, nonce, sealed, out, size - 16 );

		return true;

	}

	void tagged_hash ( const char* tag, const void* data, size_t size, uint8_t* out ) {

		uint8_t named [ 32 ];
		sha256 work;

		sha256_once ( tag, static_cast < size_t > ( base::length ( tag ) ), named );
		work.add ( named, 32 );
		work.add ( named, 32 );
		work.add ( data, size );
		work.end ( out );

	}

}
