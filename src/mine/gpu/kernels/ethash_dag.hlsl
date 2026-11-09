cbuffer dag : register ( b0 ) {

	uint4 limits;
	uint4 place;

};

ByteAddressBuffer cache : register ( t0 );
RWByteAddressBuffer dataset : register ( u0 );

#include "keccak.hlsli"

uint fnv ( uint a, uint b ) {

	return a * 0x01000193 ^ b;

}

void keccak_64 ( inout uint4 m0, inout uint4 m1, inout uint4 m2, inout uint4 m3 ) {

	lanes s = empty_lanes ( );

	s.a0 = m0.xy; s.a1 = m0.zw; s.a2 = m1.xy; s.a3 = m1.zw;
	s.a4 = m2.xy; s.a5 = m2.zw; s.a6 = m3.xy; s.a7 = m3.zw;
	s.a8 = uint2 ( 0x00000001, 0x80000000 );
	keccak ( s );
	m0 = uint4 ( s.a0, s.a1 );
	m1 = uint4 ( s.a2, s.a3 );
	m2 = uint4 ( s.a4, s.a5 );
	m3 = uint4 ( s.a6, s.a7 );

}

[ numthreads ( 64, 1, 1 ) ]
void main ( uint3 id : SV_DispatchThreadID ) {

	uint local = id.y * limits.w + id.x;

	if ( local >= limits.y )
		return;

	uint index = limits.x + local;
	uint count = limits.z;
	uint at = ( index % count ) * 64;
	uint4 m0 = cache.Load4 ( at );
	uint4 m1 = cache.Load4 ( at + 16 );
	uint4 m2 = cache.Load4 ( at + 32 );
	uint4 m3 = cache.Load4 ( at + 48 );

	m0.x ^= index;
	keccak_64 ( m0, m1, m2, m3 );

	[ loop ]
	for ( uint j = 0; j < 256; j += 4 ) {

		uint quarter = j & 12;

		[ unroll ]
		for ( uint t = 0; t < 4; t++ ) {

			uint4 words = quarter == 0 ? m0 : ( quarter == 4 ? m1 : ( quarter == 8 ? m2 : m3 ) );
			uint word = t == 0 ? words.x : ( t == 1 ? words.y : ( t == 2 ? words.z : words.w ) );
			uint parent = ( fnv ( index ^ ( j + t ), word ) % count ) * 64;
			uint4 p0 = cache.Load4 ( parent );
			uint4 p1 = cache.Load4 ( parent + 16 );
			uint4 p2 = cache.Load4 ( parent + 32 );
			uint4 p3 = cache.Load4 ( parent + 48 );

			m0 = m0 * 0x01000193 ^ p0;
			m1 = m1 * 0x01000193 ^ p1;
			m2 = m2 * 0x01000193 ^ p2;
			m3 = m3 * 0x01000193 ^ p3;

		}

	}

	keccak_64 ( m0, m1, m2, m3 );

	uint out_at = ( index - place.x ) * 64;

	dataset.Store4 ( out_at, m0 );
	dataset.Store4 ( out_at + 16, m1 );
	dataset.Store4 ( out_at + 32, m2 );
	dataset.Store4 ( out_at + 48, m3 );

}
