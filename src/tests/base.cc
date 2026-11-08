#ifdef ALLOY_DEBUG

#include "tests/harness.hh"
#include "pool/job.hh"
#include "base/bits.hh"
#include "base/formats/gif.hh"
#include "base/formats/json.hh"
#include "base/math.hh"
#include "base/formats/png.hh"
#include "base/strings.hh"
#include "base/text.hh"

namespace tests {

	static bool near ( double value, double wanted, double slack ) {

		return base::abs ( value - wanted ) <= slack;

	}

	void base_tests ( ) {

		section ( L"base library" );

		base::text note;

		note.decimal ( 3.14159, 2 ).put ( L' ' ).scaled ( 52400000.0, L"H/s" ).put ( L' ' ).grouped ( 1234567 ).put ( L' ' ).duration ( 3725.0 ).put ( L' ' ).duration ( 65.0 );
		check ( base::same_text ( note.get ( ), L"3.14 52.4 MH/s 1,234,567 1:02:05 1:05" ), L"the text writer formats decimals, rates, groups and durations" );

		wchar_t wide [ 32 ];
		char round_trip [ 32 ];
		static const char accented [ ] = "h\xC3\xA9llo \xE2\x82\xAC";

		base::widen ( wide, 32, accented );
		base::narrow ( round_trip, 32, wide );
		check ( base::same_text ( wide, L"h\x00E9llo \x20AC" ) && base::same_text ( round_trip, accented ), L"UTF-8 text widens and narrows back to the same bytes" );

		uint8_t bytes [ 4 ];
		char hex [ 9 ];
		uint32_t word = 0;

		check ( base::read_hex ( "00ff10ab", bytes, 4 ) && bytes [ 1 ] == 0xFF && bytes [ 3 ] == 0xAB && !base::read_hex ( "00ff10a", bytes, 4 ) && !base::read_hex ( "00fg10ab", bytes, 4 ), L"hex reads exact lengths and refuses bad digits" );
		base::write_hex ( hex, bytes, 4 );
		check ( base::same_text ( hex, "00ff10ab" ) && base::read_hex32 ( "1d00ffff", 8, word ) && word == 0x1D00FFFF, L"hex writes back and reads big-endian words" );
		check ( near ( base::read_decimal ( "0.0005", 6 ), 0.0005, 1e-12 ) && near ( base::read_decimal ( "1e-3", 4 ), 0.001, 1e-12 ) && near ( base::read_decimal ( L"-2.5" ), -2.5, 1e-12 ), L"decimals parse plain and exponent forms" );

		check ( base::swap_bytes ( 0x11223344 ) == 0x44332211 && base::rotate_right ( 1, 1 ) == 0x80000000 && base::leading_zeros ( 1 ) == 31 && base::leading_zeros ( 0 ) == 32, L"byte swaps, rotations and leading zeros" );
		check ( near ( base::sin ( base::pi * 0.5f ), 1.0, 1e-5 ) && near ( base::cos ( 0.0f ), 1.0, 1e-5 ) && near ( base::sin ( 7.0f ), 0.6569866, 1e-5 ) && base::floor ( -1.5f ) == -2.0f && base::round ( 2.5f ) == 3.0f, L"the math helpers match known values" );
		check ( base::nice_ceiling ( 47.0 ) == 50.0 && base::nice_ceiling ( 1.3e8 ) == 1.5e8 && base::nice_ceiling ( 0.0 ) == 1.0, L"chart ceilings round up to tidy numbers" );

		base::json message;
		static const char sample [ ] = "{\"params\": [\"bf\", \"4d16b6f85af6e2198f44ae2a6de67f78487ae5611b77c6c0440b921e00000000\", \"01000000010000000000000000000000000000000000000000000000000000000000000000ffffffff20020862062f503253482f04b8864e5008\", \"072f736c7573682f000000000100f2052a010000001976a914d23fcdf86f7e756a64a7a9688ef9903327048ed988ac00000000\", [], \"00000002\", \"1c2ac4af\", \"504e86b9\", false], \"id\": null, \"method\": \"mining.notify\"}";

		bool parsed = message.parse ( sample, base::length ( sample ) );
		pool::job work;
		bool read = parsed && pool::parse_notify ( message, message.member ( 0, "params" ), work );

		check ( parsed && message.text_is ( message.member ( 0, "method" ), "mining.notify" ) && message.is_null ( message.member ( 0, "id" ) ), L"JSON parses the stratum protocol's own example notify" );
		check ( read && base::same_text ( work.id, "bf" ) && work.version == 2 && work.bits == 0x1C2AC4AF && work.time == 0x504E86B9 && !work.clean && work.branch_count == 0 && work.previous [ 0 ] == 0x4D && work.head [ 0 ] == 0x01 && work.head [ work.head_size - 1 ] == 0x08 && work.tail [ 0 ] == 0x07 && work.tail [ work.tail_size - 1 ] == 0x00, L"the example notify becomes a job with the right fields" );

		base::line written;

		pool::write_notify ( written, work );

		base::json again;
		pool::job copy;

		check ( again.parse ( written.get ( ), written.size ( ) ) && pool::parse_notify ( again, again.member ( 0, "params" ), copy ) && base::same_text ( copy.id, "bf" ) && copy.head_size == work.head_size && copy.bits == work.bits, L"a job written as notify reads back the same" );

		char escaped [ 64 ];
		base::json quoted;
		static const char tricky [ ] = "{\"a\":\"x\\\"y\\\\z\\u00e9\",\"b\":[1,2.5,-3e2],\"c\":{\"d\":true}}";

		check ( quoted.parse ( tricky, base::length ( tricky ) ) && quoted.text ( quoted.member ( 0, "a" ), escaped, 64 ) == 7 && base::same_text ( escaped, "x\"y\\z\xC3\xA9" ) && quoted.count ( quoted.member ( 0, "b" ) ) == 3 && quoted.number ( quoted.item ( quoted.member ( 0, "b" ), 2 ) ) == -300.0 && quoted.truth ( quoted.member ( quoted.member ( 0, "c" ), "d" ) ), L"JSON escapes, arrays and nested objects" );
		check ( !quoted.parse ( "{\"a\":}", 6 ) && !quoted.parse ( "[1,2", 4 ) && !quoted.parse ( "tru", 3 ), L"broken JSON is refused" );

		check ( base::crc32 ( reinterpret_cast < const uint8_t* > ( "123456789" ), 9 ) == 0xCBF43926, L"crc32 gives the standard check value" );

		uint8_t pixels [ 16 * 16 * 4 ];
		base::list < uint8_t > png;

		for ( int32_t i = 0; i < 16 * 16 * 4; i++ )
			pixels [ i ] = static_cast < uint8_t > ( i * 7 );

		bool encoded = base::encode_png ( pixels, 16, 16, png );
		int32_t size = png.size ( );

		check ( encoded && size > 57 && png [ 0 ] == 0x89 && png [ 1 ] == 'P' && base::load_big ( png.data ( ) + size - 4 ) == 0xAE426082, L"PNG files start with the signature and end with the standard IEND checksum" );

		static const uint32_t flats [ ] = { 0x121417, 0x0D0F11, 0x1B1E22, 0x3DB39A, 0xE5E8EA, 0xE8EAEC };
		uint8_t* frame = new uint8_t [ 128 * 128 * 4 ];
		base::gif_writer* gif = new base::gif_writer;

		for ( int32_t i = 0; i < 128 * 128; i++ ) {

			uint32_t color = i < 128 * 96 ? flats [ i / 2048 ] : static_cast < uint32_t > ( i * 2654435761u ) & 0xFFFFFF;

			frame [ i * 4 ] = static_cast < uint8_t > ( color >> 16 );
			frame [ i * 4 + 1 ] = static_cast < uint8_t > ( color >> 8 );
			frame [ i * 4 + 2 ] = static_cast < uint8_t > ( color );
			frame [ i * 4 + 3 ] = 255;

		}

		bool exact = gif->open ( 128, 128 );

		gif->learn ( frame );
		gif->seal ( );

		for ( uint32_t color : flats )
			exact = exact && gif->shade_of ( color ) == color;

		check ( exact && gif->frame ( frame, 2 ), L"GIF frames keep the interface's flat colours exactly, not rounded to 15 bits" );
		delete gif;
		delete [ ] frame;

	}

}

#endif
