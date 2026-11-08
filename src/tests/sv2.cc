#ifdef ALLOY_DEBUG

#include "tests/harness.hh"
#include "chain/address.hh"
#include "chain/secp256k1.hh"
#include "hash/chacha.hh"
#include "hash/scrypt.hh"
#include "app/session.hh"
#include "pool/test_pool.hh"
#include "base/system/clock.hh"
#include "base/memory.hh"
#include "base/strings.hh"
#include "base/text.hh"

namespace tests {

	static void read_into ( const char* hex, uint8_t* out, size_t count ) {

		base::read_hex ( hex, out, count );

	}

	void sv2_crypto_tests ( ) {

		uint8_t key [ 32 ];
		uint8_t tag [ 16 ];
		static const char forum [ ] = "Cryptographic Forum Research Group";

		read_into ( "85d6be7857556d337f4452fe42d506a80103808afb0db2fd4abff6af4149f51b", key, 32 );
		hash::poly1305 ( key, reinterpret_cast < const uint8_t* > ( forum ), 34, tag );
		check ( hex_is ( tag, 16, "a8061dc1305136c6c22b8baf0c0127a9" ), L"Poly1305 matches RFC 8439's example" );

		static const char speech [ ] = "Ladies and Gentlemen of the class of '99: If I could offer you only one tip for the future, sunscreen would be it.";
		uint8_t nonce [ 12 ];
		uint8_t aad [ 12 ];
		uint8_t sealed [ 130 ];
		uint8_t opened [ 114 ];

		for ( int32_t i = 0; i < 32; i++ )
			key [ i ] = static_cast < uint8_t > ( 0x80 + i );

		read_into ( "070000004041424344454647", nonce, 12 );
		read_into ( "50515253c0c1c2c3c4c5c6c7", aad, 12 );
		hash::aead_seal ( key, nonce, aad, 12, reinterpret_cast < const uint8_t* > ( speech ), 114, sealed );

		bool roundtrip = hash::aead_open ( key, nonce, aad, 12, sealed, 130, opened ) && base::same ( opened, speech, 114 );

		sealed [ 3 ] ^= 1;

		bool tamper = !hash::aead_open ( key, nonce, aad, 12, sealed, 130, opened );

		sealed [ 3 ] ^= 1;
		check ( hex_is ( sealed, 16, "d31a8d34648e60db7b86afbc53ef7ec2" ) && hex_is ( sealed + 114, 16, "1ae10b594f09e26a7e902ecbd0600691" ) && roundtrip && tamper, L"ChaCha20-Poly1305 matches RFC 8439's AEAD example and refuses a flipped bit" );

		uint8_t mac [ 32 ];
		hash::hmac_sha256 keyed ( "Jefe", 4 );

		keyed.add ( "what do ya want for nothing?", 28 );
		keyed.end ( mac );
		check ( hex_is ( mac, 32, "5bdcc146bf60754e6a042426089575c75a003f089d2739839dec58b964ec3843" ), L"HMAC-SHA256 matches RFC 4231's second case" );

		uint8_t secret [ 32 ] = { };
		uint8_t x [ 32 ];

		secret [ 31 ] = 3;
		check ( chain::sqrt_minus_three_holds ( ) && chain::public_x ( secret, x ) && hex_is ( x, 32, "f9308a019258c31049344f85f89d5229b531c845836f99b08601f113bce036f9" ), L"secp256k1 field and point arithmetic: c squared is -3, and 3G lands on the known point" );

		uint8_t message [ 32 ] = { };
		uint8_t aux [ 32 ] = { };
		uint8_t signature [ 64 ];
		bool first = chain::schnorr_sign ( secret, message, aux, signature ) && hex_is ( signature, 64, "e907831f80848d1069a5371b402410364bdf1c5f8307b0084c55f1ce2dca821525f66a4a85ea8b71e482a74f382d2ce5ebeee8fdb2172f477df4900d310536c0" );

		read_into ( "b7e151628aed2a6abf7158809cf4f3c762e7160f38b4da56a784d9045190cfef", secret, 32 );
		read_into ( "243f6a8885a308d313198a2e03707344a4093822299f31d0082efa98ec4e6c89", message, 32 );
		aux [ 31 ] = 1;

		bool second = chain::schnorr_sign ( secret, message, aux, signature ) && hex_is ( signature, 64, "6896bd60eeae296db48a229ff71dfe071bde413e6d43f917dc8dcf8c78de33418906d11ac976abccb20b091292bff4ea897efcb639ea871cfa95f6de339e4b0a" );
		uint8_t public_key [ 32 ];

		chain::public_x ( secret, public_key );

		bool verified = chain::schnorr_verify ( public_key, message, signature );

		message [ 0 ] ^= 1;

		bool refused = !chain::schnorr_verify ( public_key, message, signature );

		check ( first && second && verified && refused, L"BIP340 signatures match the first two reference vectors, verify, and fail on a changed message" );

		static const char* const decodes [ ] [ 2 ] = {

			{ "00000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000", "edd1fd3e327ce90cc7a3542614289aee9682003e9cf7dcc9cf2ca9743be5aa0c" },
			{ "000000000000000000000000000000000000000000000000000000000000000001d3475bf7655b0fb2d852921035b2ef607f49069b97454e6795251062741771", "b5da00b73cd6560520e7c364086e7cd23a34bf60d0e707be9fc34d4cd5fdfa2c" },
			{ "00000000000000000000000000000000000000000000000000000000000000008421cc930e77c9f514b6915c3dbe2a94c6d8f690b5b739864ba6789fb8a55dd0", "9f59c40275f5085a006f05dae77eb98c6fd0db1ab4a72ac47eae90a4fc9e57e0" },

		};

		int32_t decoded = 0;

		for ( const auto& row : decodes ) {

			uint8_t encoded [ 64 ];

			read_into ( row [ 0 ], encoded, 64 );

			if ( chain::ellswift_decode ( encoded, x ) && hex_is ( x, 32, row [ 1 ] ) )
				decoded++;

		}

		check ( decoded == 3, L"ElligatorSwift decodes BIP324's test vectors, the edge cases with u or t at zero included" );

		uint8_t ours [ 64 ];
		uint8_t theirs [ 64 ];
		uint8_t shared_x [ 32 ];
		uint8_t secret_out [ 32 ];

		read_into ( "61062ea5071d800bbfd59e2e8b53d47d194b095ae5a4df04936b49772ef0d4d7", secret, 32 );
		read_into ( "ec0adff257bbfe500c188c80b4fdd640f6b45a482bbc15fc7cef5931deff0aa186f6eb9bba7b85dc4dcc28b28722de1e3d9108b985e2967045668f66098e475b", ours, 64 );
		read_into ( "a4a94dfce69b4a2a0a099313d10f9f7e7d649d60501c9e1d274c300e0d89aafaffffffffffffffffffffffffffffffffffffffffffffffffffffffff8faf88d5", theirs, 64 );

		bool own = chain::public_x ( secret, x ) && hex_is ( x, 32, "19e965bc20fc40614e33f2f82d4eeff81b5e7516b12a5c6c0d6053527eba0923" );
		bool own_decoded = chain::ellswift_decode ( ours, x ) && hex_is ( x, 32, "19e965bc20fc40614e33f2f82d4eeff81b5e7516b12a5c6c0d6053527eba0923" );
		bool point = chain::ellswift_decode ( theirs, x ) && chain::ecdh_xonly ( x, secret, shared_x ) && hex_is ( shared_x, 32, "4eb2bf85bd00939468ea2abb25b63bc642e3d1eb8b967fb90caa2d89e716050e" );
		bool agreed = chain::ellswift_xdh ( theirs, ours, secret, true, secret_out ) && hex_is ( secret_out, 32, "c6992a117f5edbea70c3f511d32d26b9798be4b81a62eaee1a5acaa8459a3592" );

		check ( own && own_decoded && point && agreed, L"the ElligatorSwift key exchange reproduces BIP324's shared secret for the initiator" );

		int32_t round_trips = 0;

		for ( int32_t i = 0; i < 16; i++ ) {

			uint8_t encoded [ 64 ];
			uint8_t back [ 32 ];

			chain::random_secret ( secret );

			if ( chain::ellswift_create ( secret, encoded ) && chain::public_x ( secret, x ) && chain::ellswift_decode ( encoded, back ) && base::same ( x, back, 32 ) )
				round_trips++;

		}

		uint8_t a [ 32 ];
		uint8_t b [ 32 ];
		uint8_t a_public [ 64 ];
		uint8_t b_public [ 64 ];
		uint8_t from_a [ 32 ];
		uint8_t from_b [ 32 ];

		chain::random_secret ( a );
		chain::random_secret ( b );

		bool symmetric = chain::ellswift_create ( a, a_public ) && chain::ellswift_create ( b, b_public ) && chain::ellswift_xdh ( b_public, a_public, a, true, from_a ) && chain::ellswift_xdh ( a_public, b_public, b, false, from_b ) && base::same ( from_a, from_b, 32 );

		check ( round_trips == 16 && symmetric, L"fresh ElligatorSwift encodings decode back to their keys, and both sides agree on the secret" );

		uint8_t authority [ 32 ];
		char written [ 80 ];
		bool example = chain::read_authority_key ( "9bXiEd8boQVhq7WddEcERUL5tyyJVFYdU8th3HfbNXK3Yw6GRXh", authority ) && chain::write_authority_key ( authority, written, sizeof ( written ) ) && base::same_text ( written, "9bXiEd8boQVhq7WddEcERUL5tyyJVFYdU8th3HfbNXK3Yw6GRXh" );
		bool typo = !chain::read_authority_key ( "9bXiEd8boQVhq7WddEcERUL5tyyJVFYdU8th3HfbNXK3Yw6GRXi", authority );

		check ( example && typo, L"the pool's authority key reads from the spec's base58check example and writes back the same, and a typo is caught" );

	}

	static int32_t logs_starting ( const ui::snapshot& view, const wchar_t* prefix ) {

		int32_t found = 0;

		for ( int32_t i = 0; i < view.log_count; i++ )
			found += base::starts_with ( view.logs [ i ].text, prefix ) ? 1 : 0;

		return found;

	}

	static bool wait_for ( app::session& run, ui::preferences& settings, double limit, bool ( *done ) ( const ui::snapshot& ) ) {

		double began = base::seconds ( );

		while ( base::seconds ( ) - began < limit ) {

			run.tick ( base::seconds ( ), settings );

			if ( done ( run.view ( ) ) )
				return true;

			base::sleep ( 50 );

		}

		return false;

	}

	static void sv2_mining ( bool honest ) {

		pool::test_server* server = new pool::test_server;
		pool::server_settings config;

		config.algorithm = chain::ALGORITHM_SHA256D;
		config.port = 0;
		config.sv2 = true;
		config.difficulty = 0.5;
		config.job_seconds = 3;
		server->start ( config );

		double began = base::seconds ( );

		while ( !server->ready ( ) && base::seconds ( ) - began < 10.0 )
			base::sleep ( 10 );

		app::session* run = new app::session;
		ui::preferences settings;
		base::text address;
		wchar_t key [ 80 ];

		base::widen ( key, base::count_of ( key ), honest ? server->authority_key ( ) : "9bXiEd8boQVhq7WddEcERUL5tyyJVFYdU8th3HfbNXK3Yw6GRXh" );
		address.add ( L"stratum2+tcp://127.0.0.1:" ).number ( server->port ( ) ).put ( L'/' ).add ( key );
		base::copy_text ( settings.pool ( ).address, base::count_of ( settings.pool ( ).address ), address.get ( ) );
		settings.mode = ui::MODE_POOL;
		settings.threads = base::min ( run->processor ( ).threads, 4u );
		settings.gpu = true;
		run->start ( settings );

		bool done = honest ? wait_for ( *run, settings, 30.0, [ ] ( const ui::snapshot& view ) {

			return view.accepted >= 8 && logs_starting ( view, L"New job " ) >= 2 && logs_starting ( view, L"Encrypted with Noise" ) > 0;

		} ) : wait_for ( *run, settings, 15.0, [ ] ( const ui::snapshot& view ) {

			return view.link == ui::PHASE_REFUSED && logs_starting ( view, L"The pool's certificate isn't signed" ) > 0;

		} );

		const ui::snapshot& view = run->view ( );
		base::text note;

		if ( honest ) {

			note.add ( L"Stratum V2 mines over the Noise handshake with a signed certificate, across job and prev-hash changes: " ).number ( view.accepted ).add ( L" accepted, " ).number ( view.rejected ).add ( L" rejected" );

			if ( !done || server->rejected ( ) )
				note.add ( L" (server accepted " ).number ( server->accepted ( ) ).add ( L", rejected " ).number ( server->rejected ( ) ).add ( L", jobs " ).number ( server->jobs ( ) ).add ( L", link " ).number ( view.link ).put ( L')' );

			check ( done && view.rejected == 0 && server->rejected ( ) == 0 && server->accepted ( ) >= view.accepted, note.get ( ) );

		} else
			check ( done, L"a Stratum V2 pool whose certificate wasn't signed by the authority key in the address is refused" );

		run->stop ( );
		delete run;
		server->stop ( );
		delete server;

	}

	void sv2_tests ( ) {

		section ( L"stratum v2" );
		sv2_crypto_tests ( );
		sv2_mining ( true );
		sv2_mining ( false );

	}

}

#endif
