#define SHA_ROUNDS [ loop ]

#include "sha256.hlsli"
#include "scrypt.hlsli"

[ numthreads ( 64, 1, 1 ) ]
void main ( uint3 id : SV_DispatchThreadID ) {

	uint index = lane_of ( id );
	uint nonce = tail.w + index;
	uint at = index * state_stride;
	uint4 low = states [ at + 8 ];
	uint4 high = states [ at + 9 ];
	uint last [ 8 ] = { low.x, low.y, low.z, low.w, high.x, high.y, high.z, high.w };
	uint chunk [ 16 ];

	[ unroll ]
	for ( uint part = 0; part < 2; part++ ) {

		[ unroll ]
		for ( uint q = 0; q < 4; q++ ) {

			uint4 value = states [ at + part * 4 + q ];

			chunk [ q * 4 ] = swap ( value.x );
			chunk [ q * 4 + 1 ] = swap ( value.y );
			chunk [ q * 4 + 2 ] = swap ( value.z );
			chunk [ q * 4 + 3 ] = swap ( value.w );

		}

		compress ( last, chunk );

	}

	uint closing [ 16 ] = { 1, 0x80000000, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 1568 };

	compress ( last, closing );

	low = states [ at + 10 ];
	high = states [ at + 11 ];

	uint result [ 8 ] = { low.x, low.y, low.z, low.w, high.x, high.y, high.z, high.w };
	uint wrap [ 16 ] = { last [ 0 ], last [ 1 ], last [ 2 ], last [ 3 ], last [ 4 ], last [ 5 ], last [ 6 ], last [ 7 ], 0x80000000, 0, 0, 0, 0, 0, 0, 768 };

	compress ( result, wrap );

	uint top = swap ( result [ 7 ] );

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
