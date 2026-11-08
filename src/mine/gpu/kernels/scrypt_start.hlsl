#define SHA_ROUNDS [ loop ]

#include "sha256.hlsli"
#include "scrypt.hlsli"

[ numthreads ( 64, 1, 1 ) ]
void main ( uint3 id : SV_DispatchThreadID ) {

	uint index = lane_of ( id );
	uint nonce = tail.w + index;
	uint words [ 16 ] = { tail.x, tail.y, tail.z, swap ( nonce ), 0x80000000, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 640 };
	uint key [ 8 ] = { midstate_low.x, midstate_low.y, midstate_low.z, midstate_low.w, midstate_high.x, midstate_high.y, midstate_high.z, midstate_high.w };

	compress ( key, words );

	uint inner [ 8 ] = start;
	uint outer [ 8 ] = start;
	uint pad [ 16 ];

	[ unroll ]
	for ( uint i = 0; i < 16; i++ )
		pad [ i ] = ( i < 8 ? key [ i & 7 ] : 0 ) ^ 0x36363636;

	compress ( inner, pad );

	[ unroll ]
	for ( uint k = 0; k < 16; k++ )
		pad [ k ] = ( k < 8 ? key [ k & 7 ] : 0 ) ^ 0x5C5C5C5C;

	compress ( outer, pad );

	uint prefix [ 8 ] = inner;
	uint message [ 16 ] = { head [ 0 ].x, head [ 0 ].y, head [ 0 ].z, head [ 0 ].w, head [ 1 ].x, head [ 1 ].y, head [ 1 ].z, head [ 1 ].w, head [ 2 ].x, head [ 2 ].y, head [ 2 ].z, head [ 2 ].w, head [ 3 ].x, head [ 3 ].y, head [ 3 ].z, head [ 3 ].w };

	compress ( prefix, message );

	uint at = index * state_stride;

	[ loop ]
	for ( uint block = 0; block < 4; block++ ) {

		uint step [ 8 ] = prefix;
		uint rest [ 16 ] = { tail.x, tail.y, tail.z, swap ( nonce ), block + 1, 0x80000000, 0, 0, 0, 0, 0, 0, 0, 0, 0, 1184 };

		compress ( step, rest );

		uint sum [ 8 ] = outer;
		uint fold [ 16 ] = { step [ 0 ], step [ 1 ], step [ 2 ], step [ 3 ], step [ 4 ], step [ 5 ], step [ 6 ], step [ 7 ], 0x80000000, 0, 0, 0, 0, 0, 0, 768 };

		compress ( sum, fold );
		states [ at + block * 2 ] = uint4 ( swap ( sum [ 0 ] ), swap ( sum [ 1 ] ), swap ( sum [ 2 ] ), swap ( sum [ 3 ] ) );
		states [ at + block * 2 + 1 ] = uint4 ( swap ( sum [ 4 ] ), swap ( sum [ 5 ] ), swap ( sum [ 6 ] ), swap ( sum [ 7 ] ) );

	}

	states [ at + 8 ] = uint4 ( inner [ 0 ], inner [ 1 ], inner [ 2 ], inner [ 3 ] );
	states [ at + 9 ] = uint4 ( inner [ 4 ], inner [ 5 ], inner [ 6 ], inner [ 7 ] );
	states [ at + 10 ] = uint4 ( outer [ 0 ], outer [ 1 ], outer [ 2 ], outer [ 3 ] );
	states [ at + 11 ] = uint4 ( outer [ 4 ], outer [ 5 ], outer [ 6 ], outer [ 7 ] );

}
