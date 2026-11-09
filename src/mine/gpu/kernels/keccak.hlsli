static const uint2 round_constants [ 24 ] = {

	uint2 ( 0x00000001, 0x00000000 ), uint2 ( 0x00008082, 0x00000000 ), uint2 ( 0x0000808A, 0x80000000 ), uint2 ( 0x80008000, 0x80000000 ),
	uint2 ( 0x0000808B, 0x00000000 ), uint2 ( 0x80000001, 0x00000000 ), uint2 ( 0x80008081, 0x80000000 ), uint2 ( 0x00008009, 0x80000000 ),
	uint2 ( 0x0000008A, 0x00000000 ), uint2 ( 0x00000088, 0x00000000 ), uint2 ( 0x80008009, 0x00000000 ), uint2 ( 0x8000000A, 0x00000000 ),
	uint2 ( 0x8000808B, 0x00000000 ), uint2 ( 0x0000008B, 0x80000000 ), uint2 ( 0x00008089, 0x80000000 ), uint2 ( 0x00008003, 0x80000000 ),
	uint2 ( 0x00008002, 0x80000000 ), uint2 ( 0x00000080, 0x80000000 ), uint2 ( 0x0000800A, 0x00000000 ), uint2 ( 0x8000000A, 0x80000000 ),
	uint2 ( 0x80008081, 0x80000000 ), uint2 ( 0x00008080, 0x80000000 ), uint2 ( 0x80000001, 0x00000000 ), uint2 ( 0x80008008, 0x80000000 ),

};

uint2 turn64 ( uint2 v, uint n ) {

	if ( n >= 32 ) {

		v = v.yx;
		n -= 32;

	}

	if ( n == 0 )
		return v;

	return uint2 ( v.x << n | v.y >> ( 32 - n ), v.y << n | v.x >> ( 32 - n ) );

}

struct lanes {

	uint2 a0, a1, a2, a3, a4;
	uint2 a5, a6, a7, a8, a9;
	uint2 a10, a11, a12, a13, a14;
	uint2 a15, a16, a17, a18, a19;
	uint2 a20, a21, a22, a23, a24;

};

void keccak ( inout lanes s ) {

	[ fastopt ]
	for ( uint round = 0; round < 24; round++ ) {

		uint2 c0 = s.a0 ^ s.a5 ^ s.a10 ^ s.a15 ^ s.a20;
		uint2 c1 = s.a1 ^ s.a6 ^ s.a11 ^ s.a16 ^ s.a21;
		uint2 c2 = s.a2 ^ s.a7 ^ s.a12 ^ s.a17 ^ s.a22;
		uint2 c3 = s.a3 ^ s.a8 ^ s.a13 ^ s.a18 ^ s.a23;
		uint2 c4 = s.a4 ^ s.a9 ^ s.a14 ^ s.a19 ^ s.a24;

		uint2 d0 = c4 ^ turn64 ( c1, 1 );
		uint2 d1 = c0 ^ turn64 ( c2, 1 );
		uint2 d2 = c1 ^ turn64 ( c3, 1 );
		uint2 d3 = c2 ^ turn64 ( c4, 1 );
		uint2 d4 = c3 ^ turn64 ( c0, 1 );

		uint2 b0 = s.a0 ^ d0;
		uint2 b1 = turn64 ( s.a6 ^ d1, 44 );
		uint2 b2 = turn64 ( s.a12 ^ d2, 43 );
		uint2 b3 = turn64 ( s.a18 ^ d3, 21 );
		uint2 b4 = turn64 ( s.a24 ^ d4, 14 );
		uint2 b5 = turn64 ( s.a3 ^ d3, 28 );
		uint2 b6 = turn64 ( s.a9 ^ d4, 20 );
		uint2 b7 = turn64 ( s.a10 ^ d0, 3 );
		uint2 b8 = turn64 ( s.a16 ^ d1, 45 );
		uint2 b9 = turn64 ( s.a22 ^ d2, 61 );
		uint2 b10 = turn64 ( s.a1 ^ d1, 1 );
		uint2 b11 = turn64 ( s.a7 ^ d2, 6 );
		uint2 b12 = turn64 ( s.a13 ^ d3, 25 );
		uint2 b13 = turn64 ( s.a19 ^ d4, 8 );
		uint2 b14 = turn64 ( s.a20 ^ d0, 18 );
		uint2 b15 = turn64 ( s.a4 ^ d4, 27 );
		uint2 b16 = turn64 ( s.a5 ^ d0, 36 );
		uint2 b17 = turn64 ( s.a11 ^ d1, 10 );
		uint2 b18 = turn64 ( s.a17 ^ d2, 15 );
		uint2 b19 = turn64 ( s.a23 ^ d3, 56 );
		uint2 b20 = turn64 ( s.a2 ^ d2, 62 );
		uint2 b21 = turn64 ( s.a8 ^ d3, 55 );
		uint2 b22 = turn64 ( s.a14 ^ d4, 39 );
		uint2 b23 = turn64 ( s.a15 ^ d0, 41 );
		uint2 b24 = turn64 ( s.a21 ^ d1, 2 );

		s.a0 = b0 ^ ( ~b1 & b2 );
		s.a1 = b1 ^ ( ~b2 & b3 );
		s.a2 = b2 ^ ( ~b3 & b4 );
		s.a3 = b3 ^ ( ~b4 & b0 );
		s.a4 = b4 ^ ( ~b0 & b1 );
		s.a5 = b5 ^ ( ~b6 & b7 );
		s.a6 = b6 ^ ( ~b7 & b8 );
		s.a7 = b7 ^ ( ~b8 & b9 );
		s.a8 = b8 ^ ( ~b9 & b5 );
		s.a9 = b9 ^ ( ~b5 & b6 );
		s.a10 = b10 ^ ( ~b11 & b12 );
		s.a11 = b11 ^ ( ~b12 & b13 );
		s.a12 = b12 ^ ( ~b13 & b14 );
		s.a13 = b13 ^ ( ~b14 & b10 );
		s.a14 = b14 ^ ( ~b10 & b11 );
		s.a15 = b15 ^ ( ~b16 & b17 );
		s.a16 = b16 ^ ( ~b17 & b18 );
		s.a17 = b17 ^ ( ~b18 & b19 );
		s.a18 = b18 ^ ( ~b19 & b15 );
		s.a19 = b19 ^ ( ~b15 & b16 );
		s.a20 = b20 ^ ( ~b21 & b22 );
		s.a21 = b21 ^ ( ~b22 & b23 );
		s.a22 = b22 ^ ( ~b23 & b24 );
		s.a23 = b23 ^ ( ~b24 & b20 );
		s.a24 = b24 ^ ( ~b20 & b21 );
		s.a0 ^= round_constants [ round ];

	}

}

lanes empty_lanes ( ) {

	lanes s;

	s.a0 = 0; s.a1 = 0; s.a2 = 0; s.a3 = 0; s.a4 = 0;
	s.a5 = 0; s.a6 = 0; s.a7 = 0; s.a8 = 0; s.a9 = 0;
	s.a10 = 0; s.a11 = 0; s.a12 = 0; s.a13 = 0; s.a14 = 0;
	s.a15 = 0; s.a16 = 0; s.a17 = 0; s.a18 = 0; s.a19 = 0;
	s.a20 = 0; s.a21 = 0; s.a22 = 0; s.a23 = 0; s.a24 = 0;

	return s;

}
