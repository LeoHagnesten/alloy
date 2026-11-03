#include "app/commands.hh"
#include "base/console.hh"
#include "base/text.hh"
#include "chain/header.hh"
#include <windows.h>

#ifdef ALLOY_DEBUG

namespace tests {

	uint32_t run_all ( );

}

#endif

namespace app {

	static constexpr uint32_t max_lanes = 256;
	static constexpr uint64_t nonce_space = 0x100000000ull;

	struct lane {

		chain::header_hasher hasher;
		uint64_t first = 0;
		uint64_t step = 1;
		uint32_t bits = 0;
		bool searching = false;
		volatile long long done = 0;
		volatile long* stop = nullptr;
		volatile long* found = nullptr;
		volatile long* winner = nullptr;

	};

	static DWORD __stdcall work ( void* context ) {

		lane* self = static_cast < lane* > ( context );
		uint8_t digest [ 32 ];
		uint64_t nonce = self->first;

		while ( !*self->stop && nonce < nonce_space ) {

			long long batch = 0;

			for ( ; batch < 4096 && nonce < nonce_space; batch++, nonce += self->step ) {

				self->hasher.hash ( static_cast < uint32_t > ( nonce ), digest );

				if ( self->searching && chain::meets ( digest, self->bits ) ) {

					if ( InterlockedCompareExchange ( self->found, 1, 0 ) == 0 )
						InterlockedExchange ( self->winner, static_cast < long > ( static_cast < uint32_t > ( nonce ) ) );

					InterlockedExchange ( self->stop, 1 );
					batch++;

					break;

				}

			}

			InterlockedExchangeAdd64 ( &self->done, batch );

		}

		return 0;

	}

	static uint32_t thread_count ( uint32_t wanted ) {

		uint32_t available = GetActiveProcessorCount ( ALL_PROCESSOR_GROUPS );

		if ( !available )
			available = 1;

		return base::min ( base::max ( wanted ? wanted : available, 1u ), max_lanes );

	}

	static double seconds_now ( ) {

		LARGE_INTEGER counter;
		LARGE_INTEGER frequency;

		QueryPerformanceCounter ( &counter );
		QueryPerformanceFrequency ( &frequency );

		return static_cast < double > ( counter.QuadPart ) / static_cast < double > ( frequency.QuadPart );

	}

	struct crew {

		lane* lanes = nullptr;
		HANDLE threads [ max_lanes ] = { };
		uint32_t count = 0;
		volatile long stop = 0;
		volatile long found = 0;
		volatile long winner = 0;

		void launch ( const chain::header& source, uint64_t from, bool searching ) {

			lanes = new lane [ count ];

			for ( uint32_t i = 0; i < count; i++ ) {

				lanes [ i ].hasher.prepare ( source );
				lanes [ i ].first = from + i;
				lanes [ i ].step = count;
				lanes [ i ].bits = source.bits ( );
				lanes [ i ].searching = searching;
				lanes [ i ].stop = &stop;
				lanes [ i ].found = &found;
				lanes [ i ].winner = &winner;
				threads [ i ] = CreateThread ( nullptr, 0, &work, &lanes [ i ], 0, nullptr );

			}

		}

		uint64_t total ( ) const {

			uint64_t sum = 0;

			for ( uint32_t i = 0; i < count; i++ )
				sum += static_cast < uint64_t > ( lanes [ i ].done );

			return sum;

		}

		void finish ( ) {

			InterlockedExchange ( &stop, 1 );
			WaitForMultipleObjects ( count, threads, TRUE, INFINITE );

			for ( uint32_t i = 0; i < count; i++ )
				CloseHandle ( threads [ i ] );

			delete [ ] lanes;
			lanes = nullptr;

		}

	};

	uint32_t bench ( const wchar_t* algorithm, uint32_t seconds, uint32_t threads ) {

		if ( !base::same_text ( algorithm, L"sha256d" ) ) {

			base::text note;

			note.add ( L"alloy: " ).add ( algorithm ).add ( L" isn't written yet. Only sha256d runs so far." );
			console::line ( note.get ( ) );

			return 1;

		}

		crew* team = new crew ( );
		chain::header source;
		base::text note;

		team->count = thread_count ( threads );
		chain::genesis ( source );
		note.add ( L"sha256d on " ).number ( team->count ).add ( team->count == 1 ? L" thread" : L" threads" ).add ( L", portable code, " ).number ( seconds ).add ( L" seconds" );
		console::line ( note.get ( ) );

		double started = seconds_now ( );
		uint64_t previous = 0;
		double previous_at = started;

		team->launch ( source, 0, false );

		for ( uint32_t tick = 0; tick < seconds; tick++ ) {

			Sleep ( 1000 );

			double now = seconds_now ( );
			uint64_t done = team->total ( );

			note.clear ( );
			note.add ( L"  " ).rate ( static_cast < double > ( done - previous ) / ( now - previous_at ), L"H/s" );
			console::line ( note.get ( ) );
			previous = done;
			previous_at = now;

		}

		team->finish ( );

		double elapsed = seconds_now ( ) - started;
		uint64_t done = previous;
		double per_second = static_cast < double > ( done ) / elapsed;

		note.clear ( );
		note.add ( L"average " ).rate ( per_second, L"H/s" ).add ( L", " ).rate ( per_second / team->count, L"H/s" ).add ( L" per thread" );
		console::line ( note.get ( ) );
		delete team;

		return 0;

	}

	uint32_t genesis ( uint32_t from, uint32_t threads ) {

		crew* team = new crew ( );
		chain::header source;
		base::text note;

		team->count = thread_count ( threads );
		chain::genesis ( source );
		note.add ( L"Searching for the Bitcoin genesis block's nonce from " ).number ( from ).add ( L" on " ).number ( team->count ).add ( L" threads" );
		console::line ( note.get ( ) );

		double started = seconds_now ( );

		team->launch ( source, from, true );

		while ( !team->stop ) {

			Sleep ( 1000 );

			double elapsed = seconds_now ( ) - started;
			uint64_t done = team->total ( );
			double per_second = static_cast < double > ( done ) / base::max ( elapsed, 0.001 );
			double reached = static_cast < double > ( from ) + static_cast < double > ( done );

			if ( team->stop )
				break;

			note.clear ( );
			note.add ( L"  " ).decimal ( reached / 1e9, 2 ).add ( L" G nonces checked at " ).rate ( per_second, L"H/s" );
			console::line ( note.get ( ) );

			if ( reached >= static_cast < double > ( nonce_space ) )
				break;

		}

		bool found = team->found != 0;
		uint32_t nonce = static_cast < uint32_t > ( team->winner );

		team->finish ( );

		double elapsed = seconds_now ( ) - started;

		note.clear ( );

		if ( !found ) {

			note.add ( L"No nonce below 2^32 met the target from " ).number ( from );
			console::line ( note.get ( ) );
			delete team;

			return 1;

		}

		chain::header_hasher hasher;
		uint8_t digest [ 32 ];

		hasher.prepare ( source );
		hasher.hash ( nonce, digest );
		note.add ( L"Found nonce " ).number ( nonce ).add ( L" in " ).decimal ( elapsed, 1 ).add ( L" s" );
		console::line ( note.get ( ) );
		note.clear ( );
		note.add ( L"Block hash " ).hex ( digest, 32, true );
		console::line ( note.get ( ) );
		console::line ( nonce == source.nonce ( ) ? L"That's the nonce Satoshi found on January 3, 2009." : L"That nonce differs from the real block's, so the hash code has a bug." );
		delete team;

		return nonce == source.nonce ( ) ? 0 : 1;

	}

	static void usage ( ) {

		console::line ( L"Alloy 0.1, a miner written from scratch" );
		console::line ( L"" );
		console::line ( L"  Alloy.exe --bench sha256d [--seconds <n>] [--threads <n>]" );
		console::line ( L"  Alloy.exe --genesis [--from <nonce>] [--threads <n>]" );
#ifdef ALLOY_DEBUG
		console::line ( L"  Alloy.exe --test" );
#endif

	}

	uint32_t run ( wchar_t** arguments, int32_t count ) {

		const wchar_t* action = nullptr;
		const wchar_t* algorithm = L"sha256d";
		uint32_t seconds = 10;
		uint32_t threads = 0;
		uint32_t from = 0;

		for ( int32_t i = 1; arguments && i < count; i++ ) {

			const wchar_t* item = arguments [ i ];
			const wchar_t* value = i + 1 < count ? arguments [ i + 1 ] : L"";

			if ( base::same_text ( item, L"--bench" ) ) {

				action = item;

				if ( value [ 0 ] && value [ 0 ] != L'-' ) {

					algorithm = value;
					i++;

				}

			} else if ( base::same_text ( item, L"--genesis" ) || base::same_text ( item, L"--test" ) )
				action = item;
			else if ( base::same_text ( item, L"--seconds" ) ) {

				seconds = static_cast < uint32_t > ( base::max ( base::read_number ( value ), 1ull ) );
				i++;

			} else if ( base::same_text ( item, L"--threads" ) ) {

				threads = static_cast < uint32_t > ( base::read_number ( value ) );
				i++;

			} else if ( base::same_text ( item, L"--from" ) ) {

				from = static_cast < uint32_t > ( base::min ( base::read_number ( value ), 0xFFFFFFFFull ) );
				i++;

			}

		}

		if ( action && base::same_text ( action, L"--bench" ) )
			return bench ( algorithm, seconds, threads );

		if ( action && base::same_text ( action, L"--genesis" ) )
			return genesis ( from, threads );

#ifdef ALLOY_DEBUG

		if ( action && base::same_text ( action, L"--test" ) )
			return tests::run_all ( );

#endif

		usage ( );

		return action ? 1 : 0;

	}

}