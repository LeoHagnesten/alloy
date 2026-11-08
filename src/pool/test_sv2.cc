#include "pool/test_pool.hh"
#include "pool/sv2.hh"
#include "chain/address.hh"
#include "chain/secp256k1.hh"
#include "chain/target.hh"
#include "hash/sha256.hh"
#include "net/noise.hh"
#include "net/socket.hh"
#include "base/system/clock.hh"
#include "base/bits.hh"
#include "base/memory.hh"
#include "base/strings.hh"

namespace pool {

	struct sv2_test_job {

		uint32_t id = 0;
		uint32_t version = 0x20000000;
		uint8_t merkle [ 32 ] = { };
		uint8_t previous [ 32 ] = { };
		uint32_t time = 0;
		uint32_t bits = 0x1D00FFFF;

	};

	struct sv2_test_state {

		sv2_test_job jobs [ 4 ];
		uint32_t made = 0;
		uint8_t target [ 32 ] = { };
		uint8_t previous [ 32 ] = { };
		uint32_t channel = 7;
		uint64_t seen [ 1024 ] = { };
		int32_t seen_count = 0;

	};

	static void send_job ( net::noise_link& link, sv2_test_state& state, bool future ) {

		sv2_test_job& work = state.jobs [ state.made % 4 ];
		uint8_t seed [ 8 ];

		work = sv2_test_job { };
		work.id = ++state.made;
		work.time = base::unix_time ( );
		base::store_little ( seed, work.id );
		base::store_little ( seed + 4, work.time );
		hash::sha256_twice ( seed, 8, work.merkle );

		if ( future ) {

			seed [ 0 ] ^= 0x5A;
			hash::sha256_twice ( seed, 8, state.previous );

		}

		base::copy ( work.previous, state.previous, 32 );

		sv2_writer job;

		job.u32 ( state.channel );
		job.u32 ( work.id );
		job.u8 ( future ? 0 : 1 );

		if ( !future )
			job.u32 ( work.time );

		job.u32 ( work.version );
		job.raw ( work.merkle, 32 );
		link.send_frame ( sv2::channel_message, sv2::NEW_MINING_JOB, job.bytes.data ( ), job.bytes.size ( ) );

		if ( !future )
			return;

		sv2_writer previous;

		previous.u32 ( state.channel );
		previous.u32 ( work.id );
		previous.raw ( work.previous, 32 );
		previous.u32 ( work.time );
		previous.u32 ( work.bits );
		link.send_frame ( sv2::channel_message, sv2::SET_NEW_PREV_HASH, previous.bytes.data ( ), previous.bytes.size ( ) );

	}

	static void refuse_share ( net::noise_link& link, const sv2_test_state& state, uint32_t sequence, const char* reason ) {

		sv2_writer out;

		out.u32 ( state.channel );
		out.u32 ( sequence );
		out.text ( reason );
		link.send_frame ( sv2::channel_message, sv2::SUBMIT_ERROR, out.bytes.data ( ), out.bytes.size ( ) );

	}

	void test_server::run_sv2 ( ) {

		net::listener& listener = *static_cast < net::listener* > ( socket );
		uint8_t authority_secret [ 32 ];
		uint8_t authority_x [ 32 ];
		uint8_t static_secret [ 32 ];
		uint8_t static_x [ 32 ];
		uint8_t signed_message [ 32 ];
		uint8_t aux [ 32 ] = { };
		net::certificate signed_by;

		hash::sha256_once ( "alloy test authority", 20, authority_secret );
		hash::sha256_once ( "alloy test server key", 21, static_secret );
		chain::public_x ( authority_secret, authority_x );
		chain::public_x ( static_secret, static_x );
		signed_by.version = 0;
		signed_by.valid_from = base::unix_time ( ) - 3600;
		signed_by.valid_until = base::unix_time ( ) + 86400;
		net::certificate_message ( signed_by, static_x, signed_message );
		chain::schnorr_sign ( authority_secret, signed_message, aux, signed_by.signature );
		chain::write_authority_key ( authority_x, authority, sizeof ( authority ) );
		base::atomic_store ( listening, 1u );

		sv2_test_state* state = new sv2_test_state;
		base::list < uint8_t > payload;

		chain::target_for ( config.difficulty, state->target, chain::ALGORITHM_SHA256D );

		while ( !base::atomic_load ( quit ) ) {

			net::connection wire;

			if ( !listener.accept ( wire, 50 ) )
				continue;

			net::noise_link link ( wire );
			net::noise_handshake* handshake = new net::noise_handshake;
			uint8_t first [ net::noise_first_size ];
			uint8_t second [ net::noise_second_size ];
			bool shaken = link.read_exact ( first, sizeof ( first ), 5000 ) && handshake->answer ( first, static_secret, signed_by, second, link.send, link.receive ) && wire.send ( reinterpret_cast < const char* > ( second ), sizeof ( second ) );

			delete handshake;

			if ( !shaken ) {

				wire.close ( );

				continue;

			}

			base::atomic_add ( connected, 1u );

			double last_job = base::seconds ( );
			bool open = false;

			while ( !base::atomic_load ( quit ) && wire.is_open ( ) ) {

				uint16_t extension = 0;
				uint8_t type = 0;
				int32_t got = link.read_frame ( extension, type, payload, 20 );

				if ( got < 0 )
					break;

				if ( got > 0 ) {

					sv2_reader in { payload.data ( ), payload.size ( ), 0 };

					if ( type == sv2::SETUP_CONNECTION ) {

						uint8_t protocol = 0;
						uint16_t lowest = 0;
						uint16_t highest = 0;
						sv2_writer out;

						in.u8 ( protocol );
						in.u16 ( lowest );
						in.u16 ( highest );

						if ( protocol != 0 || lowest > 2 || highest < 2 ) {

							out.u32 ( 0 );
							out.text ( "unsupported-protocol" );
							link.send_frame ( 0, sv2::SETUP_ERROR, out.bytes.data ( ), out.bytes.size ( ) );

							break;

						}

						out.u16 ( 2 );
						out.u32 ( 0 );
						link.send_frame ( 0, sv2::SETUP_SUCCESS, out.bytes.data ( ), out.bytes.size ( ) );

					} else if ( type == sv2::OPEN_STANDARD_CHANNEL ) {

						uint32_t request = 0;
						char worker [ 128 ] = { };
						sv2_writer out;

						in.u32 ( request );
						in.text ( worker, sizeof ( worker ) );
						out.u32 ( request );
						out.u32 ( state->channel );
						out.raw ( state->target, 32 );
						out.u8 ( 0 );
						out.u32 ( 0 );
						link.send_frame ( 0, sv2::OPEN_STANDARD_SUCCESS, out.bytes.data ( ), out.bytes.size ( ) );
						send_job ( link, *state, true );
						base::atomic_store ( made, state->made );
						open = true;
						last_job = base::seconds ( );

					} else if ( type == sv2::SUBMIT_SHARES_STANDARD ) {

						uint32_t channel = 0;
						uint32_t sequence = 0;
						uint32_t job = 0;
						uint32_t nonce = 0;
						uint32_t time = 0;
						uint32_t version = 0;

						if ( !in.u32 ( channel ) || !in.u32 ( sequence ) || !in.u32 ( job ) || !in.u32 ( nonce ) || !in.u32 ( time ) || !in.u32 ( version ) || channel != state->channel || !( extension & sv2::channel_message ) ) {

							refuse_share ( link, *state, sequence, "invalid-channel-id" );
							base::atomic_add ( bad, 1u );

							continue;

						}

						const sv2_test_job* work = nullptr;

						for ( const sv2_test_job& slot : state->jobs ) {

							if ( slot.id == job && job )
								work = &slot;

						}

						if ( !work ) {

							refuse_share ( link, *state, sequence, "invalid-job-id" );
							base::atomic_add ( bad, 1u );

							continue;

						}

						uint64_t key = static_cast < uint64_t > ( job ) << 48 ^ static_cast < uint64_t > ( time ) << 32 ^ nonce;
						bool repeated = false;

						for ( int32_t i = 0; i < base::min ( state->seen_count, 1024 ); i++ )
							repeated = repeated || state->seen [ i ] == key;

						if ( repeated ) {

							refuse_share ( link, *state, sequence, "duplicate-share" );
							base::atomic_add ( bad, 1u );

							continue;

						}

						state->seen [ state->seen_count++ % 1024 ] = key;

						chain::header header;
						uint8_t digest [ 32 ];

						header.set_version ( version );
						base::copy ( header.previous ( ), work->previous, 32 );
						base::copy ( header.merkle_root ( ), work->merkle, 32 );
						header.set_time ( time );
						header.set_bits ( work->bits );
						header.set_nonce ( nonce );
						hash::sha256_twice ( header.bytes, chain::header_size, digest );

						if ( version != work->version || time < work->time || !chain::meets ( digest, state->target ) ) {

							refuse_share ( link, *state, sequence, "difficulty-too-low" );
							base::atomic_add ( bad, 1u );

							continue;

						}

						sv2_writer out;

						out.u32 ( state->channel );
						out.u32 ( sequence );
						out.u32 ( 1 );
						out.u64 ( 1 );
						link.send_frame ( sv2::channel_message, sv2::SUBMIT_SUCCESS, out.bytes.data ( ), out.bytes.size ( ) );
						base::atomic_add ( good, 1u );

					}

				}

				if ( open && base::seconds ( ) - last_job >= static_cast < double > ( config.job_seconds ) ) {

					send_job ( link, *state, state->made % 2 == 0 );
					base::atomic_store ( made, state->made );
					last_job = base::seconds ( );

				}

			}

			wire.close ( );
			base::atomic_add ( connected, ~0u );

		}

		delete state;

	}

}
