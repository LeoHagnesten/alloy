#include "chain/address.hh"
#include "chain/coins.hh"
#include "hash/keccak.hh"
#include "hash/sha256.hh"
#include "base/memory.hh"
#include "base/strings.hh"

namespace chain {

	static const char base58_alphabet [ ] = "123456789ABCDEFGHJKLMNPQRSTUVWXYZabcdefghijkmnopqrstuvwxyz";
	static const char bech32_alphabet [ ] = "qpzry9x8gf2tvdw0s3jn54khce6mua7l";

	struct base58_coin {

		uint8_t version;
		uint8_t coin;
		bool script;
		const wchar_t* label;

	};

	static const base58_coin base58_coins [ ] = {

		{ 0x00, COIN_BITCOIN, false, L"Bitcoin" },
		{ 0x05, COIN_BITCOIN, true, L"Bitcoin" },
		{ 0x6F, COIN_BITCOIN, false, L"Bitcoin testnet" },
		{ 0xC4, COIN_BITCOIN, true, L"Bitcoin testnet" },
		{ 0x00, COIN_BITCOIN_CASH, false, L"Bitcoin Cash" },
		{ 0x05, COIN_BITCOIN_CASH, true, L"Bitcoin Cash" },
		{ 0x00, COIN_BITCOIN_SV, false, L"Bitcoin SV" },
		{ 0x05, COIN_BITCOIN_SV, true, L"Bitcoin SV" },
		{ 0x30, COIN_LITECOIN, false, L"Litecoin" },
		{ 0x32, COIN_LITECOIN, true, L"Litecoin" },
		{ 0x05, COIN_LITECOIN, true, L"Litecoin" },
		{ 0x1E, COIN_DOGECOIN, false, L"Dogecoin" },
		{ 0x16, COIN_DOGECOIN, true, L"Dogecoin" },
		{ 0x1E, COIN_DIGIBYTE, false, L"DigiByte" },
		{ 0x3F, COIN_DIGIBYTE, true, L"DigiByte" },
		{ 0x05, COIN_DIGIBYTE, true, L"DigiByte" },

	};

	struct segwit_coin {

		const char* prefix;
		uint8_t coin;
		const wchar_t* label;

	};

	static const segwit_coin segwit_coins [ ] = {

		{ "bc", COIN_BITCOIN, L"Bitcoin" },
		{ "tb", COIN_BITCOIN, L"Bitcoin testnet" },
		{ "ltc", COIN_LITECOIN, L"Litecoin" },
		{ "dgb", COIN_DIGIBYTE, L"DigiByte" },

	};

	struct cash_coin {

		const char* prefix;
		uint8_t coin;
		const wchar_t* label;

	};

	static const cash_coin cash_coins [ ] = {

		{ "bitcoincash", COIN_BITCOIN_CASH, L"Bitcoin Cash" },
		{ "ecash", COIN_ECASH, L"eCash" },

	};

	struct candidates {

		uint8_t coins [ 12 ] = { };
		const wchar_t* labels [ 12 ] = { };
		bool scripts [ 12 ] = { };
		int32_t count = 0;
		bool attempted = false;

		void add ( uint8_t coin, const wchar_t* label, bool script ) {

			if ( count >= 12 )
				return;

			coins [ count ] = coin;
			labels [ count ] = label;
			scripts [ count++ ] = script;

		}

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

	static void base58_candidates ( const char* text, candidates& out ) {

		uint8_t bytes [ 64 ];
		uint8_t digest [ 32 ];
		int32_t size = 0;
		int32_t length = base::length ( text );

		if ( !base58_decode ( text, bytes, sizeof ( bytes ), size ) || size != 25 )
			return;

		out.attempted = out.attempted || ( length >= 26 && length <= 35 );
		hash::sha256_twice ( bytes, 21, digest );

		if ( !base::same ( digest, bytes + 21, 4 ) )
			return;

		for ( const base58_coin& coin : base58_coins ) {

			if ( coin.version == bytes [ 0 ] )
				out.add ( coin.coin, coin.label, coin.script );

		}

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

	static void segwit_candidates ( const char* text, candidates& out ) {

		int32_t length = base::length ( text );
		int32_t split = -1;

		for ( int32_t i = 0; i < length; i++ ) {

			if ( text [ i ] == '1' )
				split = i;

		}

		if ( split < 1 || length - split - 1 < 7 || length > 90 )
			return;

		const segwit_coin* found = nullptr;

		for ( const segwit_coin& coin : segwit_coins ) {

			int32_t size = base::length ( coin.prefix );
			bool same = size == split;

			for ( int32_t i = 0; i < size && same; i++ )
				same = lower ( text [ i ] ) == coin.prefix [ i ];

			if ( same )
				found = &coin;

		}

		if ( !found )
			return;

		out.attempted = true;

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
				return;

			if ( witness < 0 )
				witness = value;

			check = bech32_step ( check, static_cast < uint32_t > ( value ) );

		}

		if ( check == ( witness == 0 ? 1u : 0x2BC830A3u ) )
			out.add ( found->coin, found->label, false );

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

	static const char* cash_body ( const char* text, const char* prefix, bool& prefixed ) {

		int32_t size = base::length ( prefix );

		prefixed = text [ size ] == ':';

		for ( int32_t i = 0; i < size && prefixed; i++ )
			prefixed = lower ( text [ i ] ) == prefix [ i ];

		if ( prefixed )
			return text + size + 1;

		return lower ( text [ 0 ] ) == 'q' || lower ( text [ 0 ] ) == 'p' ? text : nullptr;

	}

	static void cash_candidates ( const char* text, candidates& out ) {

		for ( const cash_coin& coin : cash_coins ) {

			bool prefixed = false;
			const char* body = cash_body ( text, coin.prefix, prefixed );

			if ( !body || base::length ( body ) != 42 )
				continue;

			bool letters = true;

			for ( int32_t i = 0; i < 42 && letters; i++ )
				letters = find_char ( bech32_alphabet, lower ( body [ i ] ) ) >= 0;

			if ( !letters )
				continue;

			out.attempted = out.attempted || prefixed || coin.coin == COIN_BITCOIN_CASH;

			if ( cash_checksum ( coin.prefix, body, 42 ) )
				out.add ( coin.coin, coin.label, lower ( body [ 0 ] ) == 'p' );

		}

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

	address_check check_address ( uint8_t coin, const char* text ) {

		address_check out;
		candidates found;
		bool attempted = false;

		if ( base::length ( text ) < 20 )
			return out;

		if ( kaspa_match ( text, attempted ) )
			found.add ( COIN_KASPA, L"Kaspa", false );

		found.attempted = attempted;

		if ( monero_match ( text, attempted ) ) {

			found.add ( COIN_MONERO, L"Monero", false );
			found.add ( COIN_MONERO_V2, L"Monero", false );

		}

		found.attempted = found.attempted || attempted;
		cash_candidates ( text, found );
		segwit_candidates ( text, found );
		base58_candidates ( text, found );

		for ( int32_t i = 0; i < found.count; i++ ) {

			if ( found.coins [ i ] == coin ) {

				out.verdict = ADDRESS_VALID;
				out.coin = found.labels [ i ];

				return out;

			}

		}

		if ( found.count ) {

			out.verdict = ADDRESS_OTHER_COIN;
			out.coin = found.labels [ 0 ];

			return out;

		}

		if ( found.attempted )
			out.verdict = ADDRESS_BROKEN;

		return out;

	}

	static int32_t address_part ( const wchar_t* worker, char* text, int32_t room ) {

		int32_t used = 0;

		for ( int32_t i = 0; worker [ i ] && used < room - 1; i++ ) {

			wchar_t value = worker [ i ];

			if ( value == L'.' || value == L'+' )
				break;

			if ( value > 127 )
				return -1;

			text [ used++ ] = static_cast < char > ( value );

		}

		text [ used ] = 0;

		return used;

	}

	address_check check_worker ( uint8_t coin, const wchar_t* worker ) {

		char text [ 160 ];

		if ( address_part ( worker, text, sizeof ( text ) ) < 0 )
			return address_check { };

		return check_address ( coin, text );

	}

	static void hash_script ( bool script, const uint8_t* hash, uint8_t* out, int32_t& size ) {

		if ( script ) {

			out [ size++ ] = 0xA9;
			out [ size++ ] = 0x14;
			base::copy ( out + size, hash, 20 );
			size += 20;
			out [ size++ ] = 0x87;

			return;

		}

		out [ size++ ] = 0x76;
		out [ size++ ] = 0xA9;
		out [ size++ ] = 0x14;
		base::copy ( out + size, hash, 20 );
		size += 20;
		out [ size++ ] = 0x88;
		out [ size++ ] = 0xAC;

	}

	static int32_t five_to_eight ( const char* body, int32_t count, uint8_t* out, int32_t room ) {

		uint32_t gathered = 0;
		int32_t bits = 0;
		int32_t size = 0;

		for ( int32_t i = 0; i < count; i++ ) {

			int32_t value = find_char ( bech32_alphabet, lower ( body [ i ] ) );

			if ( value < 0 )
				return -1;

			gathered = gathered << 5 | static_cast < uint32_t > ( value );
			bits += 5;

			if ( bits >= 8 ) {

				bits -= 8;

				if ( size >= room )
					return -1;

				out [ size++ ] = static_cast < uint8_t > ( gathered >> bits );

			}

		}

		return size;

	}

	bool payout_script ( uint8_t coin, const wchar_t* worker, uint8_t* out, int32_t room, int32_t& size ) {

		char text [ 160 ];

		size = 0;

		if ( address_part ( worker, text, sizeof ( text ) ) <= 0 || check_address ( coin, text ).verdict != ADDRESS_VALID || room < 42 )
			return false;

		for ( const cash_coin& cash : cash_coins ) {

			bool prefixed = false;
			const char* body = cash.coin == coin ? cash_body ( text, cash.prefix, prefixed ) : nullptr;
			uint8_t payload [ 40 ];

			if ( !body || base::length ( body ) != 42 || !cash_checksum ( cash.prefix, body, 42 ) )
				continue;

			if ( five_to_eight ( body, 34, payload, sizeof ( payload ) ) < 21 || ( payload [ 0 ] & 0x07 ) != 0 )
				return false;

			hash_script ( ( payload [ 0 ] >> 3 ) == 1, payload + 1, out, size );

			return true;

		}

		uint8_t decoded [ 80 ];
		int32_t length = 0;

		if ( base58_decode ( text, decoded, sizeof ( decoded ), length ) && length == 25 ) {

			for ( const base58_coin& known : base58_coins ) {

				if ( known.coin == coin && known.version == decoded [ 0 ] ) {

					hash_script ( known.script, decoded + 1, out, size );

					return true;

				}

			}

			return false;

		}

		int32_t split = -1;

		for ( int32_t i = 0; text [ i ]; i++ ) {

			if ( text [ i ] == '1' )
				split = i;

		}

		if ( split < 1 )
			return false;

		int32_t end = base::length ( text ) - 6;
		int32_t version = find_char ( bech32_alphabet, lower ( text [ split + 1 ] ) );
		uint8_t program [ 48 ];
		int32_t program_size = five_to_eight ( text + split + 2, end - split - 2, program, sizeof ( program ) );

		if ( version < 0 || version > 16 || program_size < 2 || program_size > 40 )
			return false;

		out [ size++ ] = static_cast < uint8_t > ( version ? 0x50 + version : 0 );
		out [ size++ ] = static_cast < uint8_t > ( program_size );
		base::copy ( out + size, program, static_cast < size_t > ( program_size ) );
		size += program_size;

		return true;

	}

	bool cash_encode ( uint8_t coin, bool script, const uint8_t* hash, char* out, int32_t room ) {

		static const uint64_t generator [ 5 ] = { 0x98F2BC8E61ull, 0x79B76D99E2ull, 0xF33E5FB3C4ull, 0xAE2EABE2A8ull, 0x1E4F43E470ull };
		const char* prefix = nullptr;

		for ( const cash_coin& cash : cash_coins ) {

			if ( cash.coin == coin )
				prefix = cash.prefix;

		}

		int32_t size = prefix ? base::length ( prefix ) : 0;

		if ( !prefix || room < size + 1 + 42 + 1 )
			return false;

		uint8_t payload [ 21 ];
		uint8_t values [ 42 ];
		uint32_t gathered = 0;
		int32_t bits = 0;
		int32_t count = 0;
		uint64_t check = 1;

		payload [ 0 ] = script ? 8 : 0;
		base::copy ( payload + 1, hash, 20 );

		for ( uint8_t byte : payload ) {

			gathered = gathered << 8 | byte;
			bits += 8;

			while ( bits >= 5 ) {

				bits -= 5;
				values [ count++ ] = static_cast < uint8_t > ( gathered >> bits & 31 );

			}

		}

		values [ count++ ] = static_cast < uint8_t > ( gathered << ( 5 - bits ) & 31 );

		auto step = [ & ] ( uint64_t value ) {

			uint64_t top = check >> 35;

			check = ( check & 0x07FFFFFFFFull ) << 5 ^ value;

			for ( int32_t i = 0; i < 5; i++ ) {

				if ( top >> i & 1 )
					check ^= generator [ i ];

			}

		};

		for ( int32_t i = 0; i < size; i++ )
			step ( static_cast < uint64_t > ( prefix [ i ] ) & 31 );

		step ( 0 );

		for ( int32_t i = 0; i < count; i++ )
			step ( values [ i ] );

		for ( int32_t i = 0; i < 8; i++ )
			step ( 0 );

		check ^= 1;

		for ( int32_t i = 0; i < 8; i++ )
			values [ count++ ] = static_cast < uint8_t > ( check >> ( 5 * ( 7 - i ) ) & 31 );

		base::copy_text ( out, room, prefix );
		out [ size++ ] = ':';

		for ( int32_t i = 0; i < count; i++ )
			out [ size++ ] = bech32_alphabet [ values [ i ] ];

		out [ size ] = 0;

		return true;

	}

}
