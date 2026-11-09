cbuffer heavy : register ( b0 ) {

	uint4 pow_words [ 13 ];
	uint4 finish_words [ 13 ];
	uint4 limits;
	uint4 rows [ 128 ];

};

RWByteAddressBuffer results : register ( u0 );

#include "keccak.hlsli"

lanes load ( uint4 words [ 13 ] ) {

	lanes s;

	s.a0 = words [ 0 ].xy; s.a1 = words [ 0 ].zw; s.a2 = words [ 1 ].xy; s.a3 = words [ 1 ].zw; s.a4 = words [ 2 ].xy;
	s.a5 = words [ 2 ].zw; s.a6 = words [ 3 ].xy; s.a7 = words [ 3 ].zw; s.a8 = words [ 4 ].xy; s.a9 = words [ 4 ].zw;
	s.a10 = words [ 5 ].xy; s.a11 = words [ 5 ].zw; s.a12 = words [ 6 ].xy; s.a13 = words [ 6 ].zw; s.a14 = words [ 7 ].xy;
	s.a15 = words [ 7 ].zw; s.a16 = words [ 8 ].xy; s.a17 = words [ 8 ].zw; s.a18 = words [ 9 ].xy; s.a19 = words [ 9 ].zw;
	s.a20 = words [ 10 ].xy; s.a21 = words [ 10 ].zw; s.a22 = words [ 11 ].xy; s.a23 = words [ 11 ].zw; s.a24 = words [ 12 ].xy;

	return s;

}

uint row_word ( uint row, uint word ) {

	uint4 pair = rows [ row * 2 + ( word >> 2 ) ];
	uint pick = word & 3;

	return pick == 0 ? pair.x : ( pick == 1 ? pair.y : ( pick == 2 ? pair.z : pair.w ) );

}

[ numthreads ( 256, 1, 1 ) ]
void main ( uint3 id : SV_DispatchThreadID ) {

	uint index = id.y * limits.z + id.x;
	uint nonce = limits.w + index;
	lanes s = load ( pow_words );

	s.a9 ^= uint2 ( nonce, limits.y );
	keccak ( s );

	uint h [ 8 ] = { s.a0.x, s.a0.y, s.a1.x, s.a1.y, s.a2.x, s.a2.y, s.a3.x, s.a3.y };
	uint spread [ 32 ];

	[ unroll ]
	for ( uint k = 0; k < 8; k++ ) {

		uint packed = ( ( h [ k ] >> 4 ) & 0x0F0F0F0F ) | ( ( h [ k ] << 4 ) & 0xF0F0F0F0 );
		uint turned = packed >> 16 | packed << 16;

		[ unroll ]
		for ( uint t = 0; t < 4; t++ )
			spread [ k * 4 + t ] = ( turned >> ( 4 * t ) ) & 0x000F000F;

	}

	uint mixed [ 8 ];

	[ fastopt ]
	for ( uint w = 0; w < 8; w++ ) {

		uint built = 0;

		[ unroll ]
		for ( uint q = 0; q < 4; q++ ) {

			uint upper = 0;
			uint lower = 0;
			uint row = w * 8 + q * 2;

			[ unroll ]
			for ( uint k = 0; k < 8; k++ ) {

				uint a = row_word ( row, k );
				uint b = row_word ( row + 1, k );

				[ unroll ]
				for ( uint t = 0; t < 4; t++ ) {

					upper += ( ( a >> ( 4 * t ) ) & 0x000F000F ) * spread [ k * 4 + t ];
					lower += ( ( b >> ( 4 * t ) ) & 0x000F000F ) * spread [ k * 4 + t ];

				}

			}

			built |= ( ( ( upper >> 26 ) << 4 ) | ( lower >> 26 ) ) << ( 8 * q );

		}

		mixed [ w ] = built;

	}

	s = load ( finish_words );
	s.a0 ^= uint2 ( mixed [ 0 ] ^ h [ 0 ], mixed [ 1 ] ^ h [ 1 ] );
	s.a1 ^= uint2 ( mixed [ 2 ] ^ h [ 2 ], mixed [ 3 ] ^ h [ 3 ] );
	s.a2 ^= uint2 ( mixed [ 4 ] ^ h [ 4 ], mixed [ 5 ] ^ h [ 5 ] );
	s.a3 ^= uint2 ( mixed [ 6 ] ^ h [ 6 ], mixed [ 7 ] ^ h [ 7 ] );

	keccak ( s );

	uint top = s.a3.y;

	if ( limits.x == 0xFFFFFFFF && index < 16384 ) {

		results.Store ( index * 4, top );

		return;

	}

	if ( top <= limits.x ) {

		uint slot;

		results.InterlockedAdd ( 0, 1, slot );

		if ( slot < 31 )
			results.Store ( 4 + slot * 4, nonce );

	}

}