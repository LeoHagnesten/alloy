#include "chain/address.hh"
#include "hash/keccak.hh"
#include "hash/sha256.hh"
#include "base/memory.hh"
#include "base/strings.hh"

namespace chain {

	static const char base58_alphabet [ ] = "123456789ABCDEFGHJKLMNPQRSTUVWXYZabcdefghijkmnopqrstuvwxyz";
	static const char bech32_alphabet [ ] = "qpzry9x8gf2tvdw0s3jn54khce6mua7l";

	struct base58_coin {

		uint8_t algorithm;
		uint8_t version;
		const wchar_t* coin;

	};

	static const base58_coin base58_coins [ ] = {

		{ ALGORITHM_SHA256D, 0x00, L"Bitcoin" },
		{ ALGORITHM_SHA256D, 0x05, L"Bitcoin" },
		{ ALGORITHM_SHA256D, 0x6F, L"Bitcoin testnet" },
		{ ALGORITHM_SHA256D, 0xC4, L"Bitcoin testnet" },
		{ ALGORITHM_SCRYPT, 0x30, L"Litecoin" },
		{ ALGORITHM_SCRYPT, 0x32, L"Litecoin" },
		{ ALGORITHM_SCRYPT, 0x05, L"Litecoin" },
		{ ALGORITHM_SCRYPT, 0x1E, L"Dogecoin" },
		{ ALGORITHM_SCRYPT, 0x16, L"Dogecoin" },

	};

	struct segwit_coin {

		uint8_t algorithm;
		const char* prefix;
		const wchar_t* coin;

	};

	static const segwit_coin segwit_coins [ ] = {

		{ ALGORITHM_SHA256D, "bc", L"Bitcoin" },
		{ ALGORITHM_SHA256D, "tb", L"Bitcoin testnet" },
		{ ALGORITHM_SCRYPT, "ltc", L"Litecoin" },

	};

	static int32_t find_char ( const char* alphabet, char value ) {

		for ( int32_t i = 0; alphabet [ i ]; i++ ) {

			if ( alphabet [ i ] == value )
				return i;

		}

		return -1;

	}

	static char lower ( char value ) {

		return value >= 'A' && value <= 'Z' ? static_cast < char > ( value + 32 ) : value;

	}

	bool base58_decode ( const char* text, uint8_t* out, int32_t room, int32_t& size ) {

		int32_t zeros = 0;
		int32_t used = 0;

		size = 0;

		while ( text [ zeros ] == '1' )
			zeros++;

		for ( int32_t i = 0; text [ i ]; i++ ) {

			int32_t digit = find_char ( base58_alphabet, text [ i ] );

			if ( digit < 0 )
				return false;

			uint32_t carry = static_cast < uint32_t > ( digit );

			for ( int32_t k = 0; k < used; k++ ) {

				carry += static_cast < uint32_t > ( out [ room - 1 - k ] ) * 58;
				out [ room - 1 - k ] = static_cast < uint8_t > ( carry );
				carry >>= 8;

			}

			while ( carry ) {

				if ( used >= room )
					return false;

				out [ room - 1 - used ] = static_cast < uint8_t > ( carry );
				used++;
				carry >>= 8;

			}

		}

		if ( zeros + used > room )
			return false;

		base::move ( out + zeros, out + room - used, static_cast < size_t > ( used ) );
		base::zero ( out, static_cast < size_t > ( zeros ) );
		size = zeros + used;

		return true;

	}

	bool base58_encode ( const uint8_t* bytes, int32_t size, char* out, int32_t room ) {

		uint8_t digits [ 128 ] = { };
		int32_t used = 0;
		int32_t zeros = 0;

		while ( zeros < size && !bytes [ zeros ] )
			zeros++;

		for ( int32_t i = zeros; i < size; i++ ) {

			uint32_t carry = bytes [ i ];

			for ( int32_t k = 0; k < used; k++ ) {

				carry += static_cast < uint32_t > ( digits [ k ] ) << 8;
				digits [ k ] = static_cast < uint8_t > ( carry % 58 );
				carry /= 58;

			}

			while ( carry ) {

				if ( used >= static_cast < int32_t > ( sizeof ( digits ) ) )
					return false;

				digits [ used++ ] = static_cast < uint8_t > ( carry % 58 );
				carry /= 58;

			}

		}

		if ( zeros + used + 1 > room )
			return false;

		int32_t at = 0;

		for ( int32_t i = 0; i < zeros; i++ )
			out [ at++ ] = '1';

		for ( int32_t i = used - 1; i >= 0; i-- )
			out [ at++ ] = base58_alphabet [ digits [ i ] ];

		out [ at ] = 0;

		return true;

	}

	bool read_authority_key ( const char* text, uint8_t* out ) {

		uint8_t decoded [ 64 ];
		uint8_t digest [ 32 ];
		int32_t size = 0;

		if ( !base58_decode ( text, decoded, sizeof ( decoded ), size ) || size != 38 || decoded [ 0 ] != 1 || decoded [ 1 ] != 0 )
			return false;

		hash::sha256_twice ( decoded, 34, digest );

		if ( !base::same ( digest, decoded + 34, 4 ) )
			return false;

		base::copy ( out, decoded + 2, 32 );

		return true;

	}

	bool write_authority_key ( const uint8_t* key, char* out, int32_t room ) {

		uint8_t raw [ 38 ] = { 1, 0 };
		uint8_t digest [ 32 ];

		base::copy ( raw + 2, key, 32 );
		hash::sha256_twice ( raw, 34, digest );
		base::copy ( raw + 34, digest, 4 );

		return base58_encode ( raw, 38, out, room );

	}

	bool monero_decode ( const char* text, uint8_t* out, int32_t room, int32_t& size ) {

		static const int32_t block_bytes [ 12 ] = { 0, -1, 1, 2, -1, 3, 4, 5, -1, 6, 7, 8 };
		int32_t length = base::length ( text );

		size = 0;

		for ( int32_t at = 0; at < length; at += 11 ) {

			int32_t chars = base::min ( 11, length - at );
			int32_t bytes = block_bytes [ chars ];
			unsigned __int128 value = 0;

			if ( bytes < 0 || size + bytes > room )
				return false;

			for ( int32_t i = 0; i < chars; i++ ) {

				int32_t digit = find_char ( base58_alphabet, text [ at + i ] );

				if ( digit < 0 )
					return false;

				value = value * 58 + static_cast < uint32_t > ( digit );

			}

			if ( bytes < 8 && ( value >> ( bytes * 8 ) ) != 0 )
				return false;

			if ( value >> 64 )
				return false;

			for ( int32_t i = bytes - 1; i >= 0; i-- ) {

				out [ size + i ] = static_cast < uint8_t > ( value );
				value >>= 8;

			}

			size += bytes;

		}

		return true;

	}

	static const base58_coin* base58_match ( const char* text, uint8_t algorithm, bool& decoded ) {

		uint8_t bytes [ 64 ];
		uint8_t digest [ 32 ];
		int32_t size = 0;

		decoded = base58_decode ( text, bytes, sizeof ( bytes ), size ) && size == 25;

		if ( !decoded )
			return nullptr;

		hash::sha256_twice ( bytes, 21, digest );

		if ( !base::same ( digest, bytes + 21, 4 ) )
			return nullptr;

		const base58_coin* other = nullptr;

		for ( const base58_coin& coin : base58_coins ) {

			if ( coin.version != bytes [ 0 ] )
				continue;

			if ( coin.algorithm == algorithm )
				return &coin;

			other = other ? other : &coin;

		}

		return other;

	}

	static uint32_t bech32_step ( uint32_t check, uint32_t value ) {

		static const uint32_t generator [ 5 ] = { 0x3B6A57B2, 0x26508E6D, 0x1EA119FA, 0x3D4233DD, 0x2A1462B3 };
		uint32_t top = check >> 25;

		check = ( check & 0x1FFFFFF ) << 5 ^ value;

		for ( int32_t i = 0; i < 5; i++ ) {

			if ( top >> i & 1 )
				check ^= generator [ i ];

		}

		return check;

	}

	static const segwit_coin* segwit_match ( const char* text, uint8_t algorithm, bool& attempted ) {

		int32_t length = base::length ( text );
		int32_t split = -1;

		attempted = false;

		for ( int32_t i = 0; i < length; i++ ) {

			if ( text [ i ] == '1' )
				split = i;

		}

		if ( split < 1 || length - split - 1 < 7 || length > 90 )
			return nullptr;

		const segwit_coin* found = nullptr;

		for ( const segwit_coin& coin : segwit_coins ) {

			int32_t size = base::length ( coin.prefix );
			bool same = size == split;

			for ( int32_t i = 0; i < size && same; i++ )
				same = lower ( text [ i ] ) == coin.prefix [ i ];

			if ( same && ( !found || coin.algorithm == algorithm ) )
				found = &coin;

		}

		if ( !found )
			return nullptr;

		attempted = true;

		uint32_t check = 1;
		int32_t witness = -1;

		for ( int32_t i = 0; i < split; i++ )
			check = bech32_step ( check, static_cast < uint32_t > ( lower ( text [ i ] ) ) >> 5 );

		check = bech32_step ( check, 0 );

		for ( int32_t i = 0; i < split; i++ )
			check = bech32_step ( check, static_cast < uint32_t > ( lower ( text [ i ] ) ) & 31 );

		for ( int32_t i = split + 1; i < length; i++ ) {

			int32_t value = find_char ( bech32_alphabet, lower ( text [ i ] ) );

			if ( value < 0 )
				return nullptr;

			if ( witness < 0 )
				witness = value;

			check = bech32_step ( check, static_cast < uint32_t > ( value ) );

		}

		uint32_t wanted = witness == 0 ? 1u : 0x2BC830A3u;

		return check == wanted ? found : nullptr;

	}

	static bool cash_checksum ( const char* prefix, const char* body, int32_t length ) {

		static const uint64_t generator [ 5 ] = { 0x98F2BC8E61ull, 0x79B76D99E2ull, 0xF33E5FB3C4ull, 0xAE2EABE2A8ull, 0x1E4F43E470ull };
		uint64_t check = 1;

		auto step = [ & ] ( uint64_t value ) {

			uint64_t top = check >> 35;

			check = ( check & 0x07FFFFFFFFull ) << 5 ^ value;

			for ( int32_t i = 0; i < 5; i++ ) {

				if ( top >> i & 1 )
					check ^= generator [ i ];

			}

		};

		for ( int32_t i = 0; prefix [ i ]; i++ )
			step ( static_cast < uint64_t > ( prefix [ i ] ) & 31 );

		step ( 0 );

		for ( int32_t i = 0; i < length; i++ ) {

			int32_t value = find_char ( bech32_alphabet, lower ( body [ i ] ) );

			if ( value < 0 )
				return false;

			step ( static_cast < uint64_t > ( value ) );

		}

		return ( check ^ 1 ) == 0;

	}

	static bool cash_match ( const char* text, bool& attempted ) {

		static const char prefix [ ] = "bitcoincash";
		const char* body = text;
		bool prefixed = true;

		attempted = false;

		for ( int32_t i = 0; prefix [ i ]; i++ )
			prefixed = prefixed && lower ( text [ i ] ) == prefix [ i ];

		if ( prefixed && text [ 11 ] == ':' )
			body = text + 12;
		else if ( lower ( text [ 0 ] ) != 'q' && lower ( text [ 0 ] ) != 'p' )
			return false;

		int32_t length = base::length ( body );

		if ( length != 42 )
			return false;

		for ( int32_t i = 0; i < length; i++ ) {

			if ( find_char ( bech32_alphabet, lower ( body [ i ] ) ) < 0 )
				return false;

		}

		attempted = true;

		return cash_checksum ( prefix, body, length );

	}

	static bool kaspa_match ( const char* text, bool& attempted ) {

		static const char* const prefixes [ ] = { "kaspa", "kaspatest" };

		attempted = false;

		for ( const char* prefix : prefixes ) {

			int32_t size = base::length ( prefix );
			bool same = text [ size ] == ':';

			for ( int32_t i = 0; i < size && same; i++ )
				same = lower ( text [ i ] ) == prefix [ i ];

			if ( !same )
				continue;

			const char* body = text + size + 1;
			int32_t length = base::length ( body );

			attempted = true;

			return ( length == 61 || length == 63 ) && cash_checksum ( prefix, body, length );

		}

		return false;

	}

	static bool monero_match ( const char* text, bool& attempted ) {

		uint8_t bytes [ 80 ];
		uint8_t digest [ 32 ];
		int32_t size = 0;
		int32_t length = base::length ( text );

		attempted = length == 95 || length == 106;

		if ( !attempted || !monero_decode ( text, bytes, sizeof ( bytes ), size ) || size < 69 )
			return false;

		static const uint8_t prefixes [ ] = { 18, 19, 42, 53, 54, 63, 24, 25, 36 };
		bool known = false;

		for ( uint8_t prefix : prefixes )
			known = known || bytes [ 0 ] == prefix;

		hash::keccak_256 ( bytes, static_cast < size_t > ( size - 4 ), digest );

		return known && base::same ( digest, bytes + size - 4, 4 );

	}

	address_check check_address ( uint8_t algorithm, const char* text ) {

		address_check out;
		bool attempted = false;
		bool decoded = false;
		int32_t length = base::length ( text );

		if ( length < 20 )
			return out;

		if ( kaspa_match ( text, attempted ) ) {

			out.coin = L"Kaspa";
			out.verdict = algorithm == ALGORITHM_KHEAVYHASH ? ADDRESS_VALID : ADDRESS_OTHER_COIN;

			return out;

		}

		bool kaspa_like = attempted;

		if ( monero_match ( text, attempted ) ) {

			out.coin = L"Monero";
			out.verdict = algorithm == ALGORITHM_RANDOMX ? ADDRESS_VALID : ADDRESS_OTHER_COIN;

			return out;

		}

		bool monero_like = attempted;

		if ( cash_match ( text, attempted ) ) {

			out.coin = L"Bitcoin Cash";
			out.verdict = algorithm == ALGORITHM_SHA256D ? ADDRESS_VALID : ADDRESS_OTHER_COIN;

			return out;

		}

		bool cash_like = attempted;

		if ( const segwit_coin* coin = segwit_match ( text, algorithm, attempted ) ) {

			out.coin = coin->coin;
			out.verdict = coin->algorithm == algorithm ? ADDRESS_VALID : ADDRESS_OTHER_COIN;

			return out;

		}

		bool segwit_like = attempted;

		if ( const base58_coin* coin = base58_match ( text, algorithm, decoded ) ) {

			out.coin = coin->coin;
			out.verdict = coin->algorithm == algorithm ? ADDRESS_VALID : ADDRESS_OTHER_COIN;

			return out;

		}

		bool base58_like = decoded && length >= 26 && length <= 35;

		if ( kaspa_like || monero_like || cash_like || segwit_like || base58_like )
			out.verdict = ADDRESS_BROKEN;

		return out;

	}

	address_check check_worker ( uint8_t algorithm, const wchar_t* worker ) {

		char text [ 160 ];
		int32_t used = 0;

		for ( int32_t i = 0; worker [ i ] && used < 159; i++ ) {

			wchar_t value = worker [ i ];

			if ( value == L'.' || value == L'+' )
				break;

			if ( value > 127 )
				return address_check { };

			text [ used++ ] = static_cast < char > ( value );

		}

		text [ used ] = 0;

		return check_address ( algorithm, text );

	}

	bool payout_script ( uint8_t algorithm, const wchar_t* worker, uint8_t* out, int32_t room, int32_t& size ) {

		char text [ 160 ];
		int32_t used = 0;

		size = 0;

		for ( int32_t i = 0; worker [ i ] && used < 159; i++ ) {

			if ( worker [ i ] == L'.' || worker [ i ] == L'+' || worker [ i ] > 127 )
				break;

			text [ used++ ] = static_cast < char > ( worker [ i ] );

		}

		text [ used ] = 0;

		address_check check = check_address ( algorithm, text );

		if ( check.verdict != ADDRESS_VALID || room < 42 )
			return false;

		int32_t split = -1;

		for ( int32_t i = 0; text [ i ]; i++ ) {

			if ( text [ i ] == '1' )
				split = i;

		}

		uint8_t decoded [ 80 ];
		int32_t length = 0;

		if ( base58_decode ( text, decoded, sizeof ( decoded ), length ) && length == 25 ) {

			static const uint8_t script_hash [ ] = { 0x05, 0xC4, 0x32, 0x16 };
			bool hashed = false;

			for ( uint8_t prefix : script_hash )
				hashed = hashed || decoded [ 0 ] == prefix;

			if ( hashed ) {

				out [ size++ ] = 0xA9;
				out [ size++ ] = 0x14;
				base::copy ( out + size, decoded + 1, 20 );
				size += 20;
				out [ size++ ] = 0x87;

			} else {

				out [ size++ ] = 0x76;
				out [ size++ ] = 0xA9;
				out [ size++ ] = 0x14;
				base::copy ( out + size, decoded + 1, 20 );
				size += 20;
				out [ size++ ] = 0x88;
				out [ size++ ] = 0xAC;

			}

			return true;

		}

		if ( split < 1 )
			return false;

		uint32_t gathered = 0;
		int32_t bits = 0;
		int32_t version = -1;
		uint8_t program [ 40 ];
		int32_t program_size = 0;
		int32_t end = base::length ( text ) - 6;

		for ( int32_t i = split + 1; i < end; i++ ) {

			int32_t value = find_char ( bech32_alphabet, lower ( text [ i ] ) );

			if ( value < 0 )
				return false;

			if ( version < 0 ) {

				version = value;

				continue;

			}

			gathered = gathered << 5 | static_cast < uint32_t > ( value );
			bits += 5;

			if ( bits >= 8 ) {

				bits -= 8;

				if ( program_size >= static_cast < int32_t > ( sizeof ( program ) ) )
					return false;

				program [ program_size++ ] = static_cast < uint8_t > ( gathered >> bits );

			}

		}

		if ( version < 0 || version > 16 || program_size < 2 || program_size > 40 )
			return false;

		out [ size++ ] = static_cast < uint8_t > ( version ? 0x50 + version : 0 );
		out [ size++ ] = static_cast < uint8_t > ( program_size );
		base::copy ( out + size, program, static_cast < size_t > ( program_size ) );
		size += program_size;

		return true;

	}
}
