#include "sha256.hlsli"

cbuffer work : register ( b0 ) {

	uint4 midstate_low;
	uint4 midstate_high;
	uint4 tail;
	uint4 limits;
	uint4 head [ 4 ];

};

RWByteAddressBuffer results : register ( u0 );

[ numthreads ( 256, 1, 1 ) ]
void main ( uint3 id : SV_DispatchThreadID ) {

	uint index = id.y * limits.z + id.x;
	uint nonce = tail.w + index;
	uint words [ 16 ] = { tail.x, tail.y, tail.z, swap ( nonce ), 0x80000000, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 640 };
	uint state [ 8 ] = { midstate_low.x, midstate_low.y, midstate_low.z, midstate_low.w, midstate_high.x, midstate_high.y, midstate_high.z, midstate_high.w };

	compress ( state, words );

	uint again [ 16 ] = { state [ 0 ], state [ 1 ], state [ 2 ], state [ 3 ], state [ 4 ], state [ 5 ], state [ 6 ], state [ 7 ], 0x80000000, 0, 0, 0, 0, 0, 0, 256 };
	uint final [ 8 ] = start;

	compress ( final, again );

	uint top = swap ( final [ 7 ] );

	if ( limits.y != 0 ) {

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
