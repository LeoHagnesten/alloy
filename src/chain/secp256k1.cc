#include "chain/secp256k1.hh"
#include "hash/chacha.hh"
#include "base/system/hardware.hh"
#include "base/memory.hh"

namespace chain {

	using wide = unsigned __int128;

	struct fe {

		uint64_t v [ 4 ] = { };

	};

	struct point {

		fe x;
		fe y;
		fe z;
		bool infinity = true;

	};

	static constexpr uint64_t fold = 0x1000003D1ull;
	static const fe prime = { { 0xFFFFFFFEFFFFFC2Full, 0xFFFFFFFFFFFFFFFFull, 0xFFFFFFFFFFFFFFFFull, 0xFFFFFFFFFFFFFFFFull } };
	static const uint64_t order [ 4 ] = { 0xBFD25E8CD0364141ull, 0xBAAEDCE6AF48A03Bull, 0xFFFFFFFFFFFFFFFEull, 0xFFFFFFFFFFFFFFFFull };
	static const uint64_t inverse_power [ 4 ] = { 0xFFFFFFFEFFFFFC2Dull, 0xFFFFFFFFFFFFFFFFull, 0xFFFFFFFFFFFFFFFFull, 0xFFFFFFFFFFFFFFFFull };
	static const uint64_t root_power [ 4 ] = { 0xFFFFFFFFBFFFFF0Cull, 0xFFFFFFFFFFFFFFFFull, 0xFFFFFFFFFFFFFFFFull, 0x3FFFFFFFFFFFFFFFull };
	static const fe generator_x = { { 0x59F2815B16F81798ull, 0x029BFCDB2DCE28D9ull, 0x55A06295CE870B07ull, 0x79BE667EF9DCBBACull } };
	static const fe generator_y = { { 0x9C47D08FFB10D4B8ull, 0xFD17B448A6855419ull, 0x5DA4FBFC0E1108A8ull, 0x483ADA7726A3C465ull } };
	static const fe root_minus_three = { { 0x7D8D27AE1CD5F852ull, 0xC61F6D15DA14ECD4ull, 0x233770C2A797962Cull, 0x0A2D2BA93507F1DFull } };

	static bool at_least ( const uint64_t* a, const uint64_t* b ) {

		for ( int32_t i = 3; i >= 0; i-- ) {

			if ( a [ i ] != b [ i ] )
				return a [ i ] > b [ i ];

		}

		return true;

	}

	static void take ( uint64_t* a, const uint64_t* b ) {

		uint64_t borrow = 0;

		for ( int32_t i = 0; i < 4; i++ ) {

			uint64_t next = a [ i ] - b [ i ] - borrow;

			borrow = a [ i ] < b [ i ] + borrow || ( borrow && b [ i ] == ~0ull ) ? 1 : 0;
			a [ i ] = next;

		}

	}

	static fe settle ( fe a ) {

		if ( at_least ( a.v, prime.v ) )
			take ( a.v, prime.v );

		return a;

	}

	static fe add ( const fe& a, const fe& b ) {

		fe out;
		wide carry = 0;

		for ( int32_t i = 0; i < 4; i++ ) {

			carry += static_cast < wide > ( a.v [ i ] ) + b.v [ i ];
			out.v [ i ] = static_cast < uint64_t > ( carry );
			carry >>= 64;

		}

		if ( carry ) {

			carry = fold;

			for ( int32_t i = 0; i < 4; i++ ) {

				carry += out.v [ i ];
				out.v [ i ] = static_cast < uint64_t > ( carry );
				carry >>= 64;

			}

		}

		return settle ( out );

	}

	static fe negate ( const fe& a ) {

		bool zero = !( a.v [ 0 ] | a.v [ 1 ] | a.v [ 2 ] | a.v [ 3 ] );
		fe out = prime;

		if ( zero )
			return a;

		take ( out.v, a.v );

		return out;

	}

	static fe sub ( const fe& a, const fe& b ) {

		return add ( a, negate ( b ) );

	}

	static fe mul ( const fe& a, const fe& b ) {

		uint64_t t [ 8 ] = { };

		for ( int32_t i = 0; i < 4; i++ ) {

			wide carry = 0;

			for ( int32_t j = 0; j < 4; j++ ) {

				carry += static_cast < wide > ( a.v [ i ] ) * b.v [ j ] + t [ i + j ];
				t [ i + j ] = static_cast < uint64_t > ( carry );
				carry >>= 64;

			}

			t [ i + 4 ] = static_cast < uint64_t > ( carry );

		}

		fe out;
		wide carry = 0;

		for ( int32_t i = 0; i < 4; i++ ) {

			carry += static_cast < wide > ( t [ i ] ) + static_cast < wide > ( t [ i + 4 ] ) * fold;
			out.v [ i ] = static_cast < uint64_t > ( carry );
			carry >>= 64;

		}

		uint64_t top = static_cast < uint64_t > ( carry );

		carry = static_cast < wide > ( out.v [ 0 ] ) + static_cast < wide > ( top ) * fold;
		out.v [ 0 ] = static_cast < uint64_t > ( carry );
		carry >>= 64;

		for ( int32_t i = 1; i < 4; i++ ) {

			carry += out.v [ i ];
			out.v [ i ] = static_cast < uint64_t > ( carry );
			carry >>= 64;

		}

		if ( carry ) {

			carry = fold;

			for ( int32_t i = 0; i < 4; i++ ) {

				carry += out.v [ i ];
				out.v [ i ] = static_cast < uint64_t > ( carry );
				carry >>= 64;

			}

		}

		return settle ( out );

	}

	static fe square ( const fe& a ) {

		return mul ( a, a );

	}

	static fe power ( const fe& a, const uint64_t* exponent ) {

		fe out = { { 1, 0, 0, 0 } };

		for ( int32_t bit = 255; bit >= 0; bit-- ) {

			out = square ( out );

			if ( exponent [ bit / 64 ] >> ( bit % 64 ) & 1 )
				out = mul ( out, a );

		}

		return out;

	}

	static fe invert ( const fe& a ) {

		return power ( a, inverse_power );

	}

	static bool same ( const fe& a, const fe& b ) {

		return a.v [ 0 ] == b.v [ 0 ] && a.v [ 1 ] == b.v [ 1 ] && a.v [ 2 ] == b.v [ 2 ] && a.v [ 3 ] == b.v [ 3 ];

	}

	static bool is_zero ( const fe& a ) {

		return !( a.v [ 0 ] | a.v [ 1 ] | a.v [ 2 ] | a.v [ 3 ] );

	}

	static bool root ( const fe& a, fe& out ) {

		out = power ( a, root_power );

		return same ( square ( out ), a );

	}

	static fe small ( uint64_t value ) {

		return fe { { value, 0, 0, 0 } };

	}

	static fe half ( const fe& a ) {

		static const fe two_inverse = { { 0xFFFFFFFF7FFFFE18ull, 0xFFFFFFFFFFFFFFFFull, 0xFFFFFFFFFFFFFFFFull, 0x7FFFFFFFFFFFFFFFull } };

		return mul ( a, two_inverse );

	}

	static fe from_bytes ( const uint8_t* bytes ) {

		fe out;

		for ( int32_t i = 0; i < 4; i++ ) {

			uint64_t word = 0;

			for ( int32_t k = 0; k < 8; k++ )
				word = word << 8 | bytes [ ( 3 - i ) * 8 + k ];

			out.v [ i ] = word;

		}

		return out;

	}

	static void to_bytes ( const uint64_t* value, uint8_t* bytes ) {

		for ( int32_t i = 0; i < 4; i++ ) {

			for ( int32_t k = 0; k < 8; k++ )
				bytes [ ( 3 - i ) * 8 + k ] = static_cast < uint8_t > ( value [ i ] >> ( 56 - k * 8 ) );

		}

	}

	static point doubled ( const point& p ) {

		if ( p.infinity || is_zero ( p.y ) )
			return point { };

		fe a = square ( p.x );
		fe b = square ( p.y );
		fe c = square ( b );
		fe d = sub ( sub ( square ( add ( p.x, b ) ), a ), c );

		d = add ( d, d );

		fe e = add ( add ( a, a ), a );
		fe f = square ( e );
		point out;
		fe eight_c = add ( c, c );

		eight_c = add ( eight_c, eight_c );
		eight_c = add ( eight_c, eight_c );
		out.x = sub ( f, add ( d, d ) );
		out.y = sub ( mul ( e, sub ( d, out.x ) ), eight_c );
		out.z = mul ( add ( p.y, p.y ), p.z );
		out.infinity = false;

		return out;

	}

	static point added ( const point& p, const point& q ) {

		if ( p.infinity )
			return q;

		if ( q.infinity )
			return p;

		fe z1 = square ( p.z );
		fe z2 = square ( q.z );
		fe u1 = mul ( p.x, z2 );
		fe u2 = mul ( q.x, z1 );
		fe s1 = mul ( mul ( p.y, q.z ), z2 );
		fe s2 = mul ( mul ( q.y, p.z ), z1 );

		if ( same ( u1, u2 ) )
			return same ( s1, s2 ) ? doubled ( p ) : point { };

		fe h = sub ( u2, u1 );
		fe i = square ( add ( h, h ) );
		fe j = mul ( h, i );
		fe r = sub ( s2, s1 );

		r = add ( r, r );

		fe v = mul ( u1, i );
		point out;
		fe twice_s1_j = mul ( s1, j );

		twice_s1_j = add ( twice_s1_j, twice_s1_j );
		out.x = sub ( sub ( square ( r ), j ), add ( v, v ) );
		out.y = sub ( mul ( r, sub ( v, out.x ) ), twice_s1_j );
		out.z = mul ( sub ( sub ( square ( add ( p.z, q.z ) ), z1 ), z2 ), h );
		out.infinity = false;

		return out;

	}

	static point times ( const uint64_t* scalar, const point& p ) {

		point out;

		for ( int32_t bit = 255; bit >= 0; bit-- ) {

			out = doubled ( out );

			if ( scalar [ bit / 64 ] >> ( bit % 64 ) & 1 )
				out = added ( out, p );

		}

		return out;

	}

	static bool affine ( const point& p, fe& x, fe& y ) {

		if ( p.infinity )
			return false;

		fe inverse = invert ( p.z );
		fe inverse2 = square ( inverse );

		x = mul ( p.x, inverse2 );
		y = mul ( p.y, mul ( inverse2, inverse ) );

		return true;

	}

	static point generator ( ) {

		point out;

		out.x = generator_x;
		out.y = generator_y;
		out.z = small ( 1 );
		out.infinity = false;

		return out;

	}

	static bool lift ( const fe& x, point& out ) {

		fe y;

		if ( !root ( add ( mul ( square ( x ), x ), small ( 7 ) ), y ) )
			return false;

		if ( y.v [ 0 ] & 1 )
			y = negate ( y );

		out.x = x;
		out.y = y;
		out.z = small ( 1 );
		out.infinity = false;

		return true;

	}

	static bool on_curve ( const fe& x ) {

		fe y;

		return root ( add ( mul ( square ( x ), x ), small ( 7 ) ), y );

	}

	static void scalar_of ( const uint8_t* bytes, uint64_t* out ) {

		fe value = from_bytes ( bytes );

		for ( int32_t i = 0; i < 4; i++ )
			out [ i ] = value.v [ i ];

	}

	static void reduce_order ( uint64_t* value ) {

		if ( at_least ( value, order ) )
			take ( value, order );

	}

	static void scalar_add ( const uint64_t* a, const uint64_t* b, uint64_t* out ) {

		wide carry = 0;
		uint64_t sum [ 4 ];

		for ( int32_t i = 0; i < 4; i++ ) {

			carry += static_cast < wide > ( a [ i ] ) + b [ i ];
			sum [ i ] = static_cast < uint64_t > ( carry );
			carry >>= 64;

		}

		if ( carry || at_least ( sum, order ) )
			take ( sum, order );

		base::copy ( out, sum, sizeof ( sum ) );

	}

	static void scalar_mul ( const uint64_t* a, const uint64_t* b, uint64_t* out ) {

		uint64_t t [ 8 ] = { };

		for ( int32_t i = 0; i < 4; i++ ) {

			wide carry = 0;

			for ( int32_t j = 0; j < 4; j++ ) {

				carry += static_cast < wide > ( a [ i ] ) * b [ j ] + t [ i + j ];
				t [ i + j ] = static_cast < uint64_t > ( carry );
				carry >>= 64;

			}

			t [ i + 4 ] = static_cast < uint64_t > ( carry );

		}

		uint64_t r [ 4 ] = { };

		for ( int32_t bit = 511; bit >= 0; bit-- ) {

			uint64_t high = r [ 3 ] >> 63;

			for ( int32_t i = 3; i > 0; i-- )
				r [ i ] = r [ i ] << 1 | r [ i - 1 ] >> 63;

			r [ 0 ] = r [ 0 ] << 1 | ( t [ bit / 64 ] >> ( bit % 64 ) & 1 );

			if ( high || at_least ( r, order ) )
				take ( r, order );

		}

		base::copy ( out, r, sizeof ( r ) );

	}

	static void scalar_negate ( const uint64_t* a, uint64_t* out ) {

		uint64_t value [ 4 ];

		base::copy ( value, order, sizeof ( value ) );

		if ( !( a [ 0 ] | a [ 1 ] | a [ 2 ] | a [ 3 ] ) ) {

			base::zero ( out, 32 );

			return;

		}

		take ( value, a );
		base::copy ( out, value, sizeof ( value ) );

	}

	bool valid_secret ( const uint8_t* secret ) {

		uint64_t value [ 4 ];

		scalar_of ( secret, value );

		return ( value [ 0 ] | value [ 1 ] | value [ 2 ] | value [ 3 ] ) && !at_least ( value, order );

	}

	void random_secret ( uint8_t* out ) {

		do
			base::random_bytes ( out, 32 );
		while ( !valid_secret ( out ) );

	}

	bool public_x ( const uint8_t* secret, uint8_t* out ) {

		uint64_t scalar [ 4 ];
		fe x;
		fe y;

		if ( !valid_secret ( secret ) )
			return false;

		scalar_of ( secret, scalar );

		if ( !affine ( times ( scalar, generator ( ) ), x, y ) )
			return false;

		to_bytes ( x.v, out );

		return true;

	}

	static fe swift ( fe u, fe t ) {

		if ( is_zero ( u ) )
			u = small ( 1 );

		if ( is_zero ( t ) )
			t = small ( 1 );

		fe cube = add ( mul ( square ( u ), u ), small ( 7 ) );

		if ( is_zero ( add ( cube, square ( t ) ) ) )
			t = add ( t, t );

		fe x_big = mul ( sub ( cube, square ( t ) ), invert ( add ( t, t ) ) );
		fe y_big = mul ( add ( x_big, t ), invert ( mul ( root_minus_three, u ) ) );
		fe ratio = mul ( x_big, invert ( y_big ) );
		fe y_squared = square ( y_big );
		fe candidates [ 3 ] = { add ( u, add ( add ( y_squared, y_squared ), add ( y_squared, y_squared ) ) ), half ( sub ( negate ( ratio ), u ) ), half ( sub ( ratio, u ) ) };

		for ( const fe& x : candidates ) {

			if ( on_curve ( x ) )
				return x;

		}

		return candidates [ 2 ];

	}

	static bool swift_inverse ( const fe& x, const fe& u, uint32_t which, fe& out ) {

		fe s;
		fe v;
		fe cube = add ( mul ( square ( u ), u ), small ( 7 ) );

		if ( !( which & 2 ) ) {

			if ( on_curve ( sub ( negate ( x ), u ) ) )
				return false;

			v = x;
			s = negate ( mul ( cube, invert ( add ( add ( square ( u ), mul ( u, v ) ), square ( v ) ) ) ) );

		} else {

			s = sub ( x, u );

			if ( is_zero ( s ) )
				return false;

			fe r;
			fe four_cube = add ( add ( cube, cube ), add ( cube, cube ) );
			fe three_us = mul ( add ( add ( square ( u ), square ( u ) ), square ( u ) ), s );

			if ( !root ( negate ( mul ( s, add ( four_cube, three_us ) ) ), r ) )
				return false;

			if ( ( which & 1 ) && is_zero ( r ) )
				return false;

			v = half ( sub ( mul ( r, invert ( s ) ), u ) );

		}

		fe w;

		if ( !root ( s, w ) )
			return false;

		fe minus = half ( mul ( u, sub ( small ( 1 ), root_minus_three ) ) );
		fe plus = half ( mul ( u, add ( small ( 1 ), root_minus_three ) ) );

		switch ( which & 5 ) {

			case 0:
				out = negate ( mul ( w, add ( minus, v ) ) );
				break;

			case 1:
				out = mul ( w, add ( plus, v ) );
				break;

			case 4:
				out = mul ( w, add ( minus, v ) );
				break;

			default:
				out = negate ( mul ( w, add ( plus, v ) ) );
				break;

		}

		return true;

	}

	bool ellswift_decode ( const uint8_t* encoded, uint8_t* out ) {

		fe x = swift ( settle ( from_bytes ( encoded ) ), settle ( from_bytes ( encoded + 32 ) ) );

		to_bytes ( x.v, out );

		return on_curve ( x );

	}

	bool ellswift_create ( const uint8_t* secret, uint8_t* encoded ) {

		uint8_t x_bytes [ 32 ];

		if ( !public_x ( secret, x_bytes ) )
			return false;

		fe x = from_bytes ( x_bytes );

		for ( int32_t tries = 0; tries < 4096; tries++ ) {

			uint8_t random [ 33 ];

			base::random_bytes ( random, sizeof ( random ) );

			fe u = settle ( from_bytes ( random ) );
			fe t;

			if ( is_zero ( u ) || !swift_inverse ( x, u, random [ 32 ] & 7u, t ) )
				continue;

			to_bytes ( u.v, encoded );
			to_bytes ( t.v, encoded + 32 );

			return true;

		}

		return false;

	}

	bool ecdh_xonly ( const uint8_t* x, const uint8_t* secret, uint8_t* out ) {

		point theirs;
		uint64_t scalar [ 4 ];
		fe shared_x;
		fe shared_y;

		if ( !valid_secret ( secret ) || !lift ( settle ( from_bytes ( x ) ), theirs ) )
			return false;

		scalar_of ( secret, scalar );

		if ( !affine ( times ( scalar, theirs ), shared_x, shared_y ) )
			return false;

		to_bytes ( shared_x.v, out );

		return true;

	}

	bool ellswift_xdh ( const uint8_t* theirs, const uint8_t* ours, const uint8_t* secret, bool initiating, uint8_t* out ) {

		uint8_t x [ 32 ];
		uint8_t shared [ 32 ];
		uint8_t joined [ 160 ];

		if ( !ellswift_decode ( theirs, x ) || !ecdh_xonly ( x, secret, shared ) )
			return false;

		base::copy ( joined, initiating ? ours : theirs, 64 );
		base::copy ( joined + 64, initiating ? theirs : ours, 64 );
		base::copy ( joined + 128, shared, 32 );
		hash::tagged_hash ( "bip324_ellswift_xonly_ecdh", joined, sizeof ( joined ), out );

		return true;

	}

	static void challenge ( const uint8_t* r, const uint8_t* px, const uint8_t* message, uint64_t* out ) {

		uint8_t joined [ 96 ];
		uint8_t digest [ 32 ];

		base::copy ( joined, r, 32 );
		base::copy ( joined + 32, px, 32 );
		base::copy ( joined + 64, message, 32 );
		hash::tagged_hash ( "BIP0340/challenge", joined, sizeof ( joined ), digest );
		scalar_of ( digest, out );
		reduce_order ( out );

	}

	bool schnorr_sign ( const uint8_t* secret, const uint8_t* message, const uint8_t* aux, uint8_t* signature ) {

		uint64_t d [ 4 ];
		fe px;
		fe py;

		if ( !valid_secret ( secret ) )
			return false;

		scalar_of ( secret, d );

		if ( !affine ( times ( d, generator ( ) ), px, py ) )
			return false;

		if ( py.v [ 0 ] & 1 )
			scalar_negate ( d, d );

		uint8_t d_bytes [ 32 ];
		uint8_t masked [ 32 ];
		uint8_t px_bytes [ 32 ];
		uint8_t joined [ 96 ];
		uint8_t digest [ 32 ];
		uint64_t k [ 4 ];
		fe rx;
		fe ry;

		to_bytes ( d, d_bytes );
		to_bytes ( px.v, px_bytes );
		hash::tagged_hash ( "BIP0340/aux", aux, 32, masked );

		for ( int32_t i = 0; i < 32; i++ )
			masked [ i ] ^= d_bytes [ i ];

		base::copy ( joined, masked, 32 );
		base::copy ( joined + 32, px_bytes, 32 );
		base::copy ( joined + 64, message, 32 );
		hash::tagged_hash ( "BIP0340/nonce", joined, sizeof ( joined ), digest );
		scalar_of ( digest, k );
		reduce_order ( k );

		if ( !( k [ 0 ] | k [ 1 ] | k [ 2 ] | k [ 3 ] ) || !affine ( times ( k, generator ( ) ), rx, ry ) )
			return false;

		if ( ry.v [ 0 ] & 1 )
			scalar_negate ( k, k );

		uint64_t e [ 4 ];
		uint64_t product [ 4 ];
		uint64_t s [ 4 ];

		to_bytes ( rx.v, signature );
		challenge ( signature, px_bytes, message, e );
		scalar_mul ( e, d, product );
		scalar_add ( k, product, s );
		to_bytes ( s, signature + 32 );
		base::zero ( d_bytes, sizeof ( d_bytes ) );
		base::zero ( d, sizeof ( d ) );

		return schnorr_verify ( px_bytes, message, signature );

	}

	bool schnorr_verify ( const uint8_t* public_key, const uint8_t* message, const uint8_t* signature ) {

		fe r = from_bytes ( signature );
		uint64_t s [ 4 ];
		point p;

		scalar_of ( signature + 32, s );

		if ( at_least ( r.v, prime.v ) || at_least ( s, order ) || !lift ( settle ( from_bytes ( public_key ) ), p ) )
			return false;

		uint64_t e [ 4 ];
		uint64_t minus_e [ 4 ];
		fe x;
		fe y;

		challenge ( signature, public_key, message, e );
		scalar_negate ( e, minus_e );

		point sum = added ( times ( s, generator ( ) ), times ( minus_e, p ) );

		if ( !affine ( sum, x, y ) || ( y.v [ 0 ] & 1 ) )
			return false;

		return same ( x, r );

	}

	bool sqrt_minus_three_holds ( ) {

		return same ( square ( root_minus_three ), negate ( small ( 3 ) ) );

	}

}
