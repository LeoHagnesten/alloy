#include "base/png.hh"
#include "base/bits.hh"

namespace base {

	static constexpr int32_t window = 32768;
	static constexpr int32_t hash_size = 1 << 15;
	static constexpr int32_t longest = 258;

	static constexpr uint16_t length_base [ 29 ] = { 3, 4, 5, 6, 7, 8, 9, 10, 11, 13, 15, 17, 19, 23, 27, 31, 35, 43, 51, 59, 67, 83, 99, 115, 131, 163, 195, 227, 258 };
	static constexpr uint8_t length_extra [ 29 ] = { 0, 0, 0, 0, 0, 0, 0, 0, 1, 1, 1, 1, 2, 2, 2, 2, 3, 3, 3, 3, 4, 4, 4, 4, 5, 5, 5, 5, 0 };
	static constexpr uint16_t distance_base [ 30 ] = { 1, 2, 3, 4, 5, 7, 9, 13, 17, 25, 33, 49, 65, 97, 129, 193, 257, 385, 513, 769, 1025, 1537, 2049, 3073, 4097, 6145, 8193, 12289, 16385, 24577 };
	static constexpr uint8_t distance_extra [ 30 ] = { 0, 0, 0, 0, 1, 1, 2, 2, 3, 3, 4, 4, 5, 5, 6, 6, 7, 7, 8, 8, 9, 9, 10, 10, 11, 11, 12, 12, 13, 13 };

	class bit_sink {

	public:

		explicit bit_sink ( list < uint8_t >& out ) : out ( out ) { }

		void put ( uint32_t value, int32_t size ) {

			bits |= static_cast < uint64_t > ( value ) << count;
			count += size;

			while ( count >= 8 ) {

				out.push ( static_cast < uint8_t > ( bits ) );
				bits >>= 8;
				count -= 8;

			}

		}

		void code ( uint32_t value, int32_t size ) {

			uint32_t reversed = 0;

			for ( int32_t i = 0; i < size; i++ )
				reversed |= ( value >> i & 1 ) << ( size - 1 - i );

			put ( reversed, size );

		}

		void symbol ( uint32_t value ) {

			if ( value < 144 )
				code ( 0x30 + value, 8 );
			else if ( value < 256 )
				code ( 0x190 + value - 144, 9 );
			else if ( value < 280 )
				code ( value - 256, 7 );
			else
				code ( 0xC0 + value - 280, 8 );

		}

		void match ( int32_t size, int32_t distance ) {

			int32_t index = 28;

			while ( length_base [ index ] > size )
				index--;

			symbol ( 257 + static_cast < uint32_t > ( index ) );
			put ( static_cast < uint32_t > ( size - length_base [ index ] ), length_extra [ index ] );

			index = 29;

			while ( distance_base [ index ] > distance )
				index--;

			code ( static_cast < uint32_t > ( index ), 5 );
			put ( static_cast < uint32_t > ( distance - distance_base [ index ] ), distance_extra [ index ] );

		}

		void finish ( ) {

			if ( count > 0 )
				out.push ( static_cast < uint8_t > ( bits ) );

			bits = 0;
			count = 0;

		}

	private:

		list < uint8_t >& out;
		uint64_t bits = 0;
		int32_t count = 0;

	};

	static uint32_t hash_of ( const uint8_t* at ) {

		return ( static_cast < uint32_t > ( at [ 0 ] ) << 16 | static_cast < uint32_t > ( at [ 1 ] ) << 8 | at [ 2 ] ) * 2654435761u >> 17;

	}

	uint32_t crc32 ( const uint8_t* data, size_t size, uint32_t running ) {

		static uint32_t table [ 256 ];
		static bool ready = false;

		if ( !ready ) {

			for ( uint32_t i = 0; i < 256; i++ ) {

				uint32_t value = i;

				for ( int32_t k = 0; k < 8; k++ )
					value = value & 1 ? 0xEDB88320u ^ value >> 1 : value >> 1;

				table [ i ] = value;

			}

			ready = true;

		}

		uint32_t value = ~running;

		for ( size_t i = 0; i < size; i++ )
			value = table [ ( value ^ data [ i ] ) & 0xFF ] ^ value >> 8;

		return ~value;

	}

	bool deflate ( const uint8_t* data, size_t size, list < uint8_t >& out ) {

		int32_t* head = static_cast < int32_t* > ( allocate ( hash_size * sizeof ( int32_t ) ) );
		int32_t* previous = static_cast < int32_t* > ( allocate ( window * sizeof ( int32_t ) ) );

		if ( !head || !previous ) {

			release ( head );
			release ( previous );

			return false;

		}

		zero ( head, hash_size * sizeof ( int32_t ) );

		uint32_t low = 1;
		uint32_t high = 0;

		for ( size_t i = 0; i < size; i++ ) {

			low = ( low + data [ i ] ) % 65521;
			high = ( high + low ) % 65521;

		}

		out.push ( 0x78 );
		out.push ( 0x01 );

		bit_sink sink ( out );
		int32_t total = static_cast < int32_t > ( size );

		auto insert = [ & ] ( int32_t at ) {

			if ( at + 3 > total )
				return;

			uint32_t hash = hash_of ( data + at );

			previous [ at % window ] = head [ hash ];
			head [ hash ] = at + 1;

		};

		sink.put ( 1, 1 );
		sink.put ( 1, 2 );

		for ( int32_t i = 0; i < total; ) {

			int32_t best = 0;
			int32_t distance = 0;

			if ( i + 3 <= total ) {

				int32_t candidate = head [ hash_of ( data + i ) ] - 1;
				int32_t limit = min ( longest, total - i );

				for ( int32_t chain = 0; candidate >= 0 && i - candidate <= window && chain < 48; chain++ ) {

					int32_t size_here = 0;

					while ( size_here < limit && data [ candidate + size_here ] == data [ i + size_here ] )
						size_here++;

					if ( size_here > best ) {

						best = size_here;
						distance = i - candidate;

						if ( best == limit )
							break;

					}

					int32_t next = previous [ candidate % window ] - 1;

					if ( next >= candidate )
						break;

					candidate = next;

				}

			}

			if ( best >= 3 ) {

				sink.match ( best, distance );

				for ( int32_t k = 0; k < best; k++ )
					insert ( i + k );

				i += best;

			} else {

				sink.symbol ( data [ i ] );
				insert ( i );
				i++;

			}

		}

		sink.symbol ( 256 );
		sink.finish ( );

		uint8_t check [ 4 ];

		store_big ( check, high << 16 | low );
		out.append ( check, 4 );
		release ( head );
		release ( previous );

		return true;

	}

	static uint8_t paeth ( uint8_t left, uint8_t up, uint8_t corner ) {

		int32_t guess = static_cast < int32_t > ( left ) + up - corner;
		int32_t to_left = guess > left ? guess - left : left - guess;
		int32_t to_up = guess > up ? guess - up : up - guess;
		int32_t to_corner = guess > corner ? guess - corner : corner - guess;

		if ( to_left <= to_up && to_left <= to_corner )
			return left;

		return to_up <= to_corner ? up : corner;

	}

	static void chunk ( list < uint8_t >& out, const char* kind, const uint8_t* data, size_t size ) {

		uint8_t field [ 4 ];

		store_big ( field, static_cast < uint32_t > ( size ) );
		out.append ( field, 4 );

		int32_t start = out.size ( );

		out.append ( reinterpret_cast < const uint8_t* > ( kind ), 4 );
		out.append ( data, static_cast < int32_t > ( size ) );
		store_big ( field, crc32 ( out.data ( ) + start, static_cast < size_t > ( out.size ( ) - start ) ) );
		out.append ( field, 4 );

	}

	bool encode_png ( const uint8_t* rgba, int32_t width, int32_t height, list < uint8_t >& out ) {

		int32_t stride = width * 4;
		list < uint8_t > raw;
		list < uint8_t > packed;
		uint8_t* trial = static_cast < uint8_t* > ( allocate ( static_cast < size_t > ( stride ) * 4 ) );

		if ( width <= 0 || height <= 0 || !trial || !raw.reserve ( ( stride + 1 ) * height ) ) {

			release ( trial );

			return false;

		}

		for ( int32_t y = 0; y < height; y++ ) {

			const uint8_t* row = rgba + static_cast < size_t > ( y ) * stride;
			const uint8_t* above = y ? row - stride : nullptr;
			uint32_t best_score = 0xFFFFFFFF;
			int32_t best = 0;

			for ( int32_t kind = 0; kind < 4; kind++ ) {

				uint8_t* line = trial + kind * stride;
				uint32_t score = 0;

				for ( int32_t x = 0; x < stride; x++ ) {

					uint8_t left = x >= 4 ? row [ x - 4 ] : 0;
					uint8_t up = above ? above [ x ] : 0;
					uint8_t corner = above && x >= 4 ? above [ x - 4 ] : 0;
					uint8_t value = row [ x ];

					if ( kind == 1 )
						value = static_cast < uint8_t > ( value - left );
					else if ( kind == 2 )
						value = static_cast < uint8_t > ( value - up );
					else if ( kind == 3 )
						value = static_cast < uint8_t > ( value - paeth ( left, up, corner ) );

					line [ x ] = value;
					score += value < 128 ? value : 256 - value;

				}

				if ( score < best_score ) {

					best_score = score;
					best = kind;

				}

			}

			static constexpr uint8_t filter_ids [ 4 ] = { 0, 1, 2, 4 };

			raw.push ( filter_ids [ best ] );
			raw.append ( trial + best * stride, stride );

		}

		release ( trial );

		if ( !deflate ( raw.data ( ), static_cast < size_t > ( raw.size ( ) ), packed ) )
			return false;

		static constexpr uint8_t signature [ 8 ] = { 0x89, 'P', 'N', 'G', '\r', '\n', 0x1A, '\n' };
		uint8_t header [ 13 ] = { };

		store_big ( header, static_cast < uint32_t > ( width ) );
		store_big ( header + 4, static_cast < uint32_t > ( height ) );
		header [ 8 ] = 8;
		header [ 9 ] = 6;

		out.clear ( );
		out.append ( signature, 8 );
		chunk ( out, "IHDR", header, 13 );
		chunk ( out, "IDAT", packed.data ( ), static_cast < size_t > ( packed.size ( ) ) );
		chunk ( out, "IEND", nullptr, 0 );

		return true;

	}

}
