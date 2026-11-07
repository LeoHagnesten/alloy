cbuffer work : register ( b0 ) {

	uint4 midstate_low;
	uint4 midstate_high;
	uint4 tail;
	uint4 limits;

};

RWByteAddressBuffer results : register ( u0 );

static const uint rounds [ 64 ] = {

	0x428A2F98, 0x71374491, 0xB5C0FBCF, 0xE9B5DBA5, 0x3956C25B, 0x59F111F1, 0x923F82A4, 0xAB1C5ED5,
	0xD807AA98, 0x12835B01, 0x243185BE, 0x550C7DC3, 0x72BE5D74, 0x80DEB1FE, 0x9BDC06A7, 0xC19BF174,
	0xE49B69C1, 0xEFBE4786, 0x0FC19DC6, 0x240CA1CC, 0x2DE92C6F, 0x4A7484AA, 0x5CB0A9DC, 0x76F988DA,
	0x983E5152, 0xA831C66D, 0xB00327C8, 0xBF597FC7, 0xC6E00BF3, 0xD5A79147, 0x06CA6351, 0x14292967,
	0x27B70A85, 0x2E1B2138, 0x4D2C6DFC, 0x53380D13, 0x650A7354, 0x766A0ABB, 0x81C2C92E, 0x92722C85,
	0xA2BFE8A1, 0xA81A664B, 0xC24B8B70, 0xC76C51A3, 0xD192E819, 0xD6990624, 0xF40E3585, 0x106AA070,
	0x19A4C116, 0x1E376C08, 0x2748774C, 0x34B0BCB5, 0x391C0CB3, 0x4ED8AA4A, 0x5B9CCA4F, 0x682E6FF3,
	0x748F82EE, 0x78A5636F, 0x84C87814, 0x8CC70208, 0x90BEFFFA, 0xA4506CEB, 0xBEF9A3F7, 0xC67178F2,

};

uint turn ( uint value, uint count ) {

	return ( value >> count ) | ( value << ( 32 - count ) );

}

uint swap ( uint value ) {

	return ( value >> 24 ) | ( ( value >> 8 ) & 0xFF00 ) | ( ( value << 8 ) & 0xFF0000 ) | ( value << 24 );

}

void compress ( inout uint state [ 8 ], uint words [ 16 ] ) {

	uint a = state [ 0 ];
	uint b = state [ 1 ];
	uint c = state [ 2 ];
	uint d = state [ 3 ];
	uint e = state [ 4 ];
	uint f = state [ 5 ];
	uint g = state [ 6 ];
	uint h = state [ 7 ];

	[ unroll ]
	for ( uint i = 0; i < 64; i++ ) {

		if ( i >= 16 ) {

			uint early = words [ ( i - 15 ) & 15 ];
			uint late = words [ ( i - 2 ) & 15 ];

			words [ i & 15 ] += ( turn ( early, 7 ) ^ turn ( early, 18 ) ^ ( early >> 3 ) ) + words [ ( i - 7 ) & 15 ] + ( turn ( late, 17 ) ^ turn ( late, 19 ) ^ ( late >> 10 ) );

		}

		uint first = h + ( turn ( e, 6 ) ^ turn ( e, 11 ) ^ turn ( e, 25 ) ) + ( ( e & f ) ^ ( ~e & g ) ) + rounds [ i ] + words [ i & 15 ];
		uint second = ( turn ( a, 2 ) ^ turn ( a, 13 ) ^ turn ( a, 22 ) ) + ( ( a & b ) ^ ( a & c ) ^ ( b & c ) );

		h = g;
		g = f;
		f = e;
		e = d + first;
		d = c;
		c = b;
		b = a;
		a = first + second;

	}

	state [ 0 ] += a;
	state [ 1 ] += b;
	state [ 2 ] += c;
	state [ 3 ] += d;
	state [ 4 ] += e;
	state [ 5 ] += f;
	state [ 6 ] += g;
	state [ 7 ] += h;

}

[ numthreads ( 256, 1, 1 ) ]
void main ( uint3 id : SV_DispatchThreadID ) {

	uint index = id.y * limits.z + id.x;
	uint nonce = tail.w + index;
	uint words [ 16 ] = { tail.x, tail.y, tail.z, swap ( nonce ), 0x80000000, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 640 };
	uint state [ 8 ] = { midstate_low.x, midstate_low.y, midstate_low.z, midstate_low.w, midstate_high.x, midstate_high.y, midstate_high.z, midstate_high.w };

	compress ( state, words );

	uint again [ 16 ] = { state [ 0 ], state [ 1 ], state [ 2 ], state [ 3 ], state [ 4 ], state [ 5 ], state [ 6 ], state [ 7 ], 0x80000000, 0, 0, 0, 0, 0, 0, 256 };
	uint final [ 8 ] = { 0x6A09E667, 0xBB67AE85, 0x3C6EF372, 0xA54FF53A, 0x510E527F, 0x9B05688C, 0x1F83D9AB, 0x5BE0CD19 };

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
