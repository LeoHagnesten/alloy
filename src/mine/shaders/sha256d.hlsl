#include "sha256.hlsli"

cbuffer work : register ( b0 ) {

	uint4 midstate_low;
	uint4 midstate_high;
	uint4 tail;
	uint4 limits;
	uint4 head [ 4 ];

};

RWByteAddressBuffer results : register ( u0 );

uint big0 ( uint x ) {

	return turn ( x, 2 ) ^ turn ( x, 13 ) ^ turn ( x, 22 );

}

uint big1 ( uint x ) {

	return turn ( x, 6 ) ^ turn ( x, 11 ) ^ turn ( x, 25 );

}

uint small0 ( uint x ) {

	return turn ( x, 7 ) ^ turn ( x, 18 ) ^ ( x >> 3 );

}

uint small1 ( uint x ) {

	return turn ( x, 17 ) ^ turn ( x, 19 ) ^ ( x >> 10 );

}

void advance ( inout uint a, inout uint b, inout uint c, inout uint d, inout uint e, inout uint f, inout uint g, inout uint h, uint added ) {

	uint first = h + big1 ( e ) + ( g ^ ( e & ( f ^ g ) ) ) + added;
	uint second = big0 ( a ) + ( ( a & b ) | ( c & ( a | b ) ) );

	h = g;
	g = f;
	f = e;
	e = d + first;
	d = c;
	c = b;
	b = a;
	a = first + second;

}

[ numthreads ( 256, 1, 1 ) ]
void main ( uint3 id : SV_DispatchThreadID ) {

	uint index = id.y * limits.z + id.x;
	uint nonce = tail.w + index;
	uint w [ 16 ] = { tail.x, tail.y, tail.z, swap ( nonce ), 0x80000000, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 640 };
	uint a = head [ 0 ].x;
	uint b = head [ 0 ].y;
	uint c = head [ 0 ].z;
	uint d = head [ 0 ].w;
	uint e = head [ 1 ].x;
	uint f = head [ 1 ].y;
	uint g = head [ 1 ].z;
	uint h = head [ 1 ].w;

	[ unroll ]
	for ( uint i = 3; i < 64; i++ ) {

		if ( i == 16 )
			w [ 0 ] = head [ 2 ].x;
		else if ( i == 17 )
			w [ 1 ] = head [ 2 ].y;
		else if ( i == 18 )
			w [ 2 ] = head [ 2 ].z + small0 ( w [ 3 ] );
		else if ( i == 19 )
			w [ 3 ] = head [ 2 ].w + w [ 3 ];
		else if ( i >= 20 )
			w [ i & 15 ] += small0 ( w [ ( i - 15 ) & 15 ] ) + w [ ( i - 7 ) & 15 ] + small1 ( w [ ( i - 2 ) & 15 ] );

		advance ( a, b, c, d, e, f, g, h, rounds [ i ] + w [ i & 15 ] );

	}

	uint x [ 16 ] = { midstate_low.x + a, midstate_low.y + b, midstate_low.z + c, midstate_low.w + d, midstate_high.x + e, midstate_high.y + f, midstate_high.z + g, midstate_high.w + h, 0x80000000, 0, 0, 0, 0, 0, 0, 256 };

	a = start [ 0 ];
	b = start [ 1 ];
	c = start [ 2 ];
	d = start [ 3 ];
	e = start [ 4 ];
	f = start [ 5 ];
	g = start [ 6 ];
	h = start [ 7 ];

	[ unroll ]
	for ( uint k = 0; k < 61; k++ ) {

		if ( k >= 16 )
			x [ k & 15 ] += small0 ( x [ ( k - 15 ) & 15 ] ) + x [ ( k - 7 ) & 15 ] + small1 ( x [ ( k - 2 ) & 15 ] );

		advance ( a, b, c, d, e, f, g, h, rounds [ k ] + x [ k & 15 ] );

	}

	uint top = swap ( start [ 7 ] + e );

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
