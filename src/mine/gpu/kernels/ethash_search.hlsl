cbuffer search : register ( b0 ) {

	uint4 header [ 2 ];
	uint4 limits;
	uint4 more;

};

Buffer < uint4 > dag0 : register ( t0 );
Buffer < uint4 > dag1 : register ( t1 );
Buffer < uint4 > dag2 : register ( t2 );
Buffer < uint4 > dag3 : register ( t3 );
Buffer < uint4 > dag4 : register ( t4 );
Buffer < uint4 > dag5 : register ( t5 );
Buffer < uint4 > dag6 : register ( t6 );
Buffer < uint4 > dag7 : register ( t7 );
RWByteAddressBuffer results : register ( u0 );

#include "keccak.hlsli"

uint fnv ( uint a, uint b ) {

	return a * 0x01000193 ^ b;

}

uint swap_bytes ( uint v ) {

	return v >> 24 | ( v >> 8 & 0xFF00 ) | ( v << 8 & 0xFF0000 ) | v << 24;

}

void fetch ( uint row, out uint4 r0, out uint4 r1, out uint4 r2, out uint4 r3, out uint4 r4, out uint4 r5, out uint4 r6, out uint4 r7 ) {

	uint part = row / more.z;
	uint at = ( row - part * more.z ) * 8;

	[ branch ]
	if ( part == 0 ) {

		r0 = dag0 [ at ]; r1 = dag0 [ at + 1 ]; r2 = dag0 [ at + 2 ]; r3 = dag0 [ at + 3 ];
		r4 = dag0 [ at + 4 ]; r5 = dag0 [ at + 5 ]; r6 = dag0 [ at + 6 ]; r7 = dag0 [ at + 7 ];

	} else if ( part == 1 ) {

		r0 = dag1 [ at ]; r1 = dag1 [ at + 1 ]; r2 = dag1 [ at + 2 ]; r3 = dag1 [ at + 3 ];
		r4 = dag1 [ at + 4 ]; r5 = dag1 [ at + 5 ]; r6 = dag1 [ at + 6 ]; r7 = dag1 [ at + 7 ];

	} else if ( part == 2 ) {

		r0 = dag2 [ at ]; r1 = dag2 [ at + 1 ]; r2 = dag2 [ at + 2 ]; r3 = dag2 [ at + 3 ];
		r4 = dag2 [ at + 4 ]; r5 = dag2 [ at + 5 ]; r6 = dag2 [ at + 6 ]; r7 = dag2 [ at + 7 ];

	} else if ( part == 3 ) {

		r0 = dag3 [ at ]; r1 = dag3 [ at + 1 ]; r2 = dag3 [ at + 2 ]; r3 = dag3 [ at + 3 ];
		r4 = dag3 [ at + 4 ]; r5 = dag3 [ at + 5 ]; r6 = dag3 [ at + 6 ]; r7 = dag3 [ at + 7 ];

	} else if ( part == 4 ) {

		r0 = dag4 [ at ]; r1 = dag4 [ at + 1 ]; r2 = dag4 [ at + 2 ]; r3 = dag4 [ at + 3 ];
		r4 = dag4 [ at + 4 ]; r5 = dag4 [ at + 5 ]; r6 = dag4 [ at + 6 ]; r7 = dag4 [ at + 7 ];

	} else if ( part == 5 ) {

		r0 = dag5 [ at ]; r1 = dag5 [ at + 1 ]; r2 = dag5 [ at + 2 ]; r3 = dag5 [ at + 3 ];
		r4 = dag5 [ at + 4 ]; r5 = dag5 [ at + 5 ]; r6 = dag5 [ at + 6 ]; r7 = dag5 [ at + 7 ];

	} else if ( part == 6 ) {

		r0 = dag6 [ at ]; r1 = dag6 [ at + 1 ]; r2 = dag6 [ at + 2 ]; r3 = dag6 [ at + 3 ];
		r4 = dag6 [ at + 4 ]; r5 = dag6 [ at + 5 ]; r6 = dag6 [ at + 6 ]; r7 = dag6 [ at + 7 ];

	} else {

		r0 = dag7 [ at ]; r1 = dag7 [ at + 1 ]; r2 = dag7 [ at + 2 ]; r3 = dag7 [ at + 3 ];
		r4 = dag7 [ at + 4 ]; r5 = dag7 [ at + 5 ]; r6 = dag7 [ at + 6 ]; r7 = dag7 [ at + 7 ];

	}

}

uint pick ( uint4 v, uint lane ) {

	return lane == 0 ? v.x : ( lane == 1 ? v.y : ( lane == 2 ? v.z : v.w ) );

}

uint4 fold ( uint4 a, uint4 b ) {

	return uint4 ( fnv ( fnv ( fnv ( a.x, a.y ), a.z ), a.w ), fnv ( fnv ( fnv ( b.x, b.y ), b.z ), b.w ), 0, 0 );

}

[ numthreads ( 128, 1, 1 ) ]
void main ( uint3 id : SV_DispatchThreadID ) {

	uint index = id.y * limits.z + id.x;
	uint nonce = limits.w + index;
	lanes s = empty_lanes ( );

	s.a0 = header [ 0 ].xy; s.a1 = header [ 0 ].zw; s.a2 = header [ 1 ].xy; s.a3 = header [ 1 ].zw;
	s.a4 = uint2 ( nonce, more.x );
	s.a5 = uint2 ( 0x00000001, 0 );
	s.a8 = uint2 ( 0, 0x80000000 );
	keccak ( s );

	uint4 seed0 = uint4 ( s.a0, s.a1 );
	uint4 seed1 = uint4 ( s.a2, s.a3 );
	uint4 seed2 = uint4 ( s.a4, s.a5 );
	uint4 seed3 = uint4 ( s.a6, s.a7 );
	uint4 m0 = seed0, m1 = seed1, m2 = seed2, m3 = seed3;
	uint4 m4 = seed0, m5 = seed1, m6 = seed2, m7 = seed3;
	uint first = seed0.x;

	[ loop ]
	for ( uint i = 0; i < 64; i++ ) {

		uint slot = i & 31;
		uint group = slot >> 2;
		uint4 holder = group == 0 ? m0 : ( group == 1 ? m1 : ( group == 2 ? m2 : ( group == 3 ? m3 : ( group == 4 ? m4 : ( group == 5 ? m5 : ( group == 6 ? m6 : m7 ) ) ) ) ) );
		uint row = fnv ( i ^ first, pick ( holder, slot & 3 ) ) % more.y;
		uint4 r0, r1, r2, r3, r4, r5, r6, r7;

		fetch ( row, r0, r1, r2, r3, r4, r5, r6, r7 );
		m0 = m0 * 0x01000193 ^ r0;
		m1 = m1 * 0x01000193 ^ r1;
		m2 = m2 * 0x01000193 ^ r2;
		m3 = m3 * 0x01000193 ^ r3;
		m4 = m4 * 0x01000193 ^ r4;
		m5 = m5 * 0x01000193 ^ r5;
		m6 = m6 * 0x01000193 ^ r6;
		m7 = m7 * 0x01000193 ^ r7;

	}

	uint4 c0 = uint4 ( fold ( m0, m1 ).xy, fold ( m2, m3 ).xy );
	uint4 c1 = uint4 ( fold ( m4, m5 ).xy, fold ( m6, m7 ).xy );

	s = empty_lanes ( );
	s.a0 = seed0.xy; s.a1 = seed0.zw; s.a2 = seed1.xy; s.a3 = seed1.zw;
	s.a4 = seed2.xy; s.a5 = seed2.zw; s.a6 = seed3.xy; s.a7 = seed3.zw;
	s.a8 = c0.xy; s.a9 = c0.zw; s.a10 = c1.xy; s.a11 = c1.zw;
	s.a12 = uint2 ( 0x00000001, 0 );
	s.a16 = uint2 ( 0, 0x80000000 );
	keccak ( s );

	uint high = swap_bytes ( s.a0.x );
	uint low = swap_bytes ( s.a0.y );

	if ( more.w && index < 16384 ) {

		results.Store ( index * 4, high );

		return;

	}

	if ( high < limits.x || ( high == limits.x && low <= limits.y ) ) {

		uint place;

		results.InterlockedAdd ( 0, 1, place );

		if ( place < 31 )
			results.Store ( 4 + place * 4, nonce );

	}

}
