cbuffer work : register ( b0 ) {

	uint4 midstate_low;
	uint4 midstate_high;
	uint4 tail;
	uint4 limits;
	uint4 head [ 4 ];

};

RWByteAddressBuffer results : register ( u0 );
RWStructuredBuffer < uint4 > table : register ( u1 );
RWStructuredBuffer < uint4 > states : register ( u2 );

static const uint state_stride = 12;

uint lane_of ( uint3 id ) {

	return id.y * limits.z + id.x;

}
