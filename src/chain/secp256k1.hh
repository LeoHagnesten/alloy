#pragma once

#include "base/base.hh"

namespace chain {

	bool valid_secret ( const uint8_t* secret );

	void random_secret ( uint8_t* out );

	bool public_x ( const uint8_t* secret, uint8_t* out );

	bool ellswift_decode ( const uint8_t* encoded, uint8_t* out );

	bool ellswift_create ( const uint8_t* secret, uint8_t* encoded );

	bool ecdh_xonly ( const uint8_t* x, const uint8_t* secret, uint8_t* out );

	bool ellswift_xdh ( const uint8_t* theirs, const uint8_t* ours, const uint8_t* secret, bool initiating, uint8_t* out );

	bool schnorr_sign ( const uint8_t* secret, const uint8_t* message, const uint8_t* aux, uint8_t* signature );

	bool schnorr_verify ( const uint8_t* public_key, const uint8_t* message, const uint8_t* signature );

	bool sqrt_minus_three_holds ( );

}
