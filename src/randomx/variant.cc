#include "randomx/variant.hh"
#include "base/strings.hh"

namespace randomx {

	using frequencies = uint8_t [ OP_COUNT ];

	static constexpr frequencies standard = { 16, 7, 16, 7, 16, 4, 4, 1, 4, 1, 8, 2, 15, 5, 8, 2, 4, 4, 16, 5, 16, 5, 6, 32, 4, 6, 25, 1, 16, 0 };
	static constexpr frequencies wownero = { 25, 7, 16, 7, 16, 4, 4, 1, 4, 1, 8, 2, 15, 5, 10, 0, 4, 8, 20, 5, 20, 5, 6, 20, 4, 6, 16, 1, 16, 0 };
	static constexpr frequencies graft = { 16, 7, 16, 7, 16, 4, 4, 1, 4, 1, 8, 2, 15, 5, 7, 3, 4, 4, 16, 5, 16, 5, 6, 32, 4, 6, 25, 1, 16, 0 };

	static constexpr uint8_t wownero_keys [ 8 ] [ 16 ] = {

		{ 0x5d, 0x46, 0x90, 0xf8, 0xa6, 0xe4, 0xfb, 0x7f, 0xb7, 0x82, 0x1f, 0x14, 0x95, 0x9e, 0x35, 0xcf },
		{ 0x50, 0xc4, 0x55, 0x6a, 0x8a, 0x27, 0xe8, 0xfe, 0xc3, 0x5a, 0x5c, 0xbd, 0xdc, 0xff, 0x41, 0x67 },
		{ 0xa4, 0x47, 0x4c, 0x11, 0xe4, 0xfd, 0x24, 0xd5, 0xd2, 0x9a, 0x27, 0xa7, 0xac, 0x4a, 0x32, 0x3d },
		{ 0x2a, 0x3a, 0x0c, 0x81, 0xff, 0xae, 0xa9, 0x99, 0xd9, 0xdb, 0xd3, 0x42, 0x08, 0xdb, 0xf6, 0x76 },
		{ 0x5d, 0x46, 0x90, 0xf8, 0xa6, 0xe4, 0xfb, 0x7f, 0xb7, 0x82, 0x1f, 0x14, 0x95, 0x9e, 0x35, 0xcf },
		{ 0x50, 0xc4, 0x55, 0x6a, 0x8a, 0x27, 0xe8, 0xfe, 0xc3, 0x5a, 0x5c, 0xbd, 0xdc, 0xff, 0x41, 0x67 },
		{ 0xa4, 0x47, 0x4c, 0x11, 0xe4, 0xfd, 0x24, 0xd5, 0xd2, 0x9a, 0x27, 0xa7, 0xac, 0x4a, 0x32, 0x3d },
		{ 0x2a, 0x3a, 0x0c, 0x81, 0xff, 0xae, 0xa9, 0x99, 0xd9, 0xdb, 0xd3, 0x42, 0x08, 0xdb, 0xf6, 0x76 },

	};

	static constexpr variant make ( uint8_t id, const char* key, const wchar_t* name, const wchar_t* coin, const char* salt, uint32_t salt_size, uint32_t passes, uint32_t lanes, int32_t latency, uint32_t level2, uint32_t level3, int32_t program_size, int32_t iterations, int32_t count, bool v2, const frequencies& weights, const uint8_t ( *keys ) [ 16 ] = nullptr ) {

		variant out { id, key, name, coin, salt, salt_size, passes, lanes, latency, level2, level3, program_size, iterations, count, v2, keys, { }, { } };
		int32_t at = 0;

		for ( int32_t op = 0; op < OP_COUNT; op++ ) {

			out.frequency [ op ] = weights [ op ];

			for ( int32_t k = 0; k < weights [ op ] && at < 256; k++ )
				out.decode [ at++ ] = static_cast < uint8_t > ( op );

		}

		while ( at < 256 )
			out.decode [ at++ ] = OP_NOP;

		return out;

	}

	static constexpr variant table [ VARIANT_COUNT ] = {

		make ( VARIANT_RX0, "rx/0", L"RandomX", L"Monero", "RandomX\x03", 8, 3, 1, 170, 262144, 2097152, 256, 2048, 8, false, standard ),
		make ( VARIANT_RX2, "rx/2", L"RandomX v2", L"Monero after the RandomX v2 fork", "RandomX\x03", 8, 3, 1, 170, 262144, 2097152, 384, 2048, 8, true, standard ),
		make ( VARIANT_WOW, "rx/wow", L"RandomWOW", L"Wownero", "RandomWOW\x01", 10, 3, 1, 170, 131072, 1048576, 256, 1024, 16, false, wownero, wownero_keys ),
		make ( VARIANT_ARQ, "rx/arq", L"RandomARQ", L"ArQmA", "RandomARQ\x01", 10, 1, 1, 170, 131072, 262144, 256, 1024, 4, false, standard ),
		make ( VARIANT_GRAFT, "rx/graft", L"RandomGRAFT", L"Graft", "RandomX-Graft\x01", 14, 3, 2, 170, 262144, 2097152, 280, 2048, 8, false, graft ),
		make ( VARIANT_SFX, "rx/sfx", L"RandomSFX", L"Safex", "RandomSFX\x01", 10, 3, 1, 170, 262144, 2097152, 256, 2048, 8, false, standard ),
		make ( VARIANT_YADA, "rx/yada", L"RandomYADA", L"YadaCoin", "RandomXYadaCoin\x03", 16, 4, 1, 150, 262144, 2097152, 256, 2048, 8, false, standard ),
		make ( VARIANT_KEVA, "rx/keva", L"RandomKEVA", L"Kevacoin", "RandomKV\x01", 9, 3, 1, 170, 131072, 1048576, 256, 2048, 8, false, standard ),
		make ( VARIANT_XEQ, "rx/xeq", L"RandomXEQ", L"Equilibria", "RandomXEQ\x01", 10, 1, 1, 170, 131072, 262144, 256, 1024, 4, false, standard ),

	};

	struct alias {

		const char* key;
		uint8_t id;

	};

	static const alias aliases [ ] = {

		{ "rx/0", VARIANT_RX0 },
		{ "randomx", VARIANT_RX0 },
		{ "rx", VARIANT_RX0 },
		{ "rx/monero", VARIANT_RX0 },
		{ "rx/2", VARIANT_RX2 },
		{ "rx/v2", VARIANT_RX2 },
		{ "randomx/2", VARIANT_RX2 },
		{ "rx/wow", VARIANT_WOW },
		{ "randomwow", VARIANT_WOW },
		{ "rx/arq", VARIANT_ARQ },
		{ "randomarq", VARIANT_ARQ },
		{ "rx/graft", VARIANT_GRAFT },
		{ "randomgraft", VARIANT_GRAFT },
		{ "rx/sfx", VARIANT_SFX },
		{ "randomsfx", VARIANT_SFX },
		{ "rx/yada", VARIANT_YADA },
		{ "randomyada", VARIANT_YADA },
		{ "rx/keva", VARIANT_KEVA },
		{ "randomkeva", VARIANT_KEVA },
		{ "rx/xeq", VARIANT_XEQ },
		{ "randomxeq", VARIANT_XEQ },

	};

	const variant& variant_of ( uint8_t id ) {

		return table [ id < VARIANT_COUNT ? id : VARIANT_RX0 ];

	}

	int32_t find_variant ( const char* key ) {

		if ( !key || !key [ 0 ] )
			return -1;

		for ( const alias& item : aliases ) {

			if ( base::same_folded ( item.key, key ) )
				return item.id;

		}

		return -1;

	}

}
