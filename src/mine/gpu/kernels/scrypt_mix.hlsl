#include "scrypt.hlsli"

uint4 spin ( uint4 value, uint4 sum, uint count ) {

	return value ^ ( sum << count ) ^ ( sum >> ( 32 - count ) );

}

void salsa ( inout uint4 b0, inout uint4 b1, inout uint4 b2, inout uint4 b3 ) {

	uint4 x0 = b0;
	uint4 x1 = b1;
	uint4 x2 = b2;
	uint4 x3 = b3;

	[ loop ]
	for ( uint i = 0; i < 4; i++ ) {

		x1 = spin ( x1, x0 + x3, 7 );
		x2 = spin ( x2, x1 + x0, 9 );
		x3 = spin ( x3, x2 + x1, 13 );
		x0 = spin ( x0, x3 + x2, 18 );
		x1 = x1.wxyz;
		x2 = x2.zwxy;
		x3 = x3.yzwx;
		x3 = spin ( x3, x0 + x1, 7 );
		x2 = spin ( x2, x3 + x0, 9 );
		x1 = spin ( x1, x2 + x3, 13 );
		x0 = spin ( x0, x1 + x2, 18 );
		x1 = x1.yzwx;
		x2 = x2.zwxy;
		x3 = x3.wxyz;

	}

	b0 += x0;
	b1 += x1;
	b2 += x2;
	b3 += x3;

}

void block_mix ( inout uint4 p0, inout uint4 p1, inout uint4 p2, inout uint4 p3, inout uint4 q0, inout uint4 q1, inout uint4 q2, inout uint4 q3 ) {

	p0 ^= q0;
	p1 ^= q1;
	p2 ^= q2;
	p3 ^= q3;
	salsa ( p0, p1, p2, p3 );
	q0 ^= p0;
	q1 ^= p1;
	q2 ^= p2;
	q3 ^= p3;
	salsa ( q0, q1, q2, q3 );

}

void lanes_in ( uint4 a, uint4 b, uint4 c, uint4 d, out uint4 x0, out uint4 x1, out uint4 x2, out uint4 x3 ) {

	x0 = uint4 ( a.x, b.y, c.z, d.w );
	x1 = uint4 ( b.x, c.y, d.z, a.w );
	x2 = uint4 ( c.x, d.y, a.z, b.w );
	x3 = uint4 ( d.x, a.y, b.z, c.w );

}

void lanes_out ( uint4 x0, uint4 x1, uint4 x2, uint4 x3, out uint4 a, out uint4 b, out uint4 c, out uint4 d ) {

	a = uint4 ( x0.x, x3.y, x2.z, x1.w );
	b = uint4 ( x1.x, x0.y, x3.z, x2.w );
	c = uint4 ( x2.x, x1.y, x0.z, x3.w );
	d = uint4 ( x3.x, x2.y, x1.z, x0.w );

}

[ numthreads ( 64, 1, 1 ) ]
void main ( uint3 id : SV_DispatchThreadID ) {

	uint index = lane_of ( id );
	uint at = index * state_stride;
	uint origin = index * 4096;
	uint4 p0, p1, p2, p3, q0, q1, q2, q3;

	lanes_in ( states [ at ], states [ at + 1 ], states [ at + 2 ], states [ at + 3 ], p0, p1, p2, p3 );
	lanes_in ( states [ at + 4 ], states [ at + 5 ], states [ at + 6 ], states [ at + 7 ], q0, q1, q2, q3 );

	[ fastopt ]
	for ( uint j = 0; j < 1024; j += 2 ) {

		uint slot = origin + j * 4;

		table [ slot ] = p0;
		table [ slot + 1 ] = p1;
		table [ slot + 2 ] = p2;
		table [ slot + 3 ] = p3;
		table [ slot + 4 ] = q0;
		table [ slot + 5 ] = q1;
		table [ slot + 6 ] = q2;
		table [ slot + 7 ] = q3;
		block_mix ( p0, p1, p2, p3, q0, q1, q2, q3 );
		block_mix ( p0, p1, p2, p3, q0, q1, q2, q3 );

	}

	[ fastopt ]
	for ( uint n = 0; n < 1024; n++ ) {

		uint wanted = q0.x & 1023;
		uint pick = origin + ( wanted >> 1 ) * 8;
		uint4 v0 = table [ pick ];
		uint4 v1 = table [ pick + 1 ];
		uint4 v2 = table [ pick + 2 ];
		uint4 v3 = table [ pick + 3 ];
		uint4 v4 = table [ pick + 4 ];
		uint4 v5 = table [ pick + 5 ];
		uint4 v6 = table [ pick + 6 ];
		uint4 v7 = table [ pick + 7 ];

		[ branch ]
		if ( wanted & 1 )
			block_mix ( v0, v1, v2, v3, v4, v5, v6, v7 );

		p0 ^= v0;
		p1 ^= v1;
		p2 ^= v2;
		p3 ^= v3;
		q0 ^= v4;
		q1 ^= v5;
		q2 ^= v6;
		q3 ^= v7;
		block_mix ( p0, p1, p2, p3, q0, q1, q2, q3 );

	}

	uint4 a, b, c, d;

	lanes_out ( p0, p1, p2, p3, a, b, c, d );
	states [ at ] = a;
	states [ at + 1 ] = b;
	states [ at + 2 ] = c;
	states [ at + 3 ] = d;
	lanes_out ( q0, q1, q2, q3, a, b, c, d );
	states [ at + 4 ] = a;
	states [ at + 5 ] = b;
	states [ at + 6 ] = c;
	states [ at + 7 ] = d;

}
