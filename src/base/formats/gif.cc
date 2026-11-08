#include "base/formats/gif.hh"
#include "base/bits.hh"
#include "base/memory.hh"
#include "base/containers/sort.hh"

namespace base {

	static constexpr uint32_t color_space = 1u << 24;
	static constexpr uint32_t mapped = 0x80000000u;
	static constexpr int32_t exact_slots = 176;

	struct swatch {

		uint32_t color;
		uint32_t count;

	};

	struct bucket {

		int32_t start;
		int32_t end;

	};

	static uint32_t channel ( uint32_t color, int32_t axis ) {

		return color >> ( 16 - axis * 8 ) & 255;

	}

	static uint32_t pack ( const uint8_t* at ) {

		return static_cast < uint32_t > ( at [ 0 ] ) << 16 | static_cast < uint32_t > ( at [ 1 ] ) << 8 | at [ 2 ];

	}

	static int32_t spread ( const swatch* items, bucket range, int32_t& axis ) {

		int32_t best = -1;

		for ( int32_t a = 0; a < 3; a++ ) {

			uint32_t low = 255;
			uint32_t high = 0;

			for ( int32_t i = range.start; i < range.end; i++ ) {

				uint32_t value = channel ( items [ i ].color, a );

				low = min ( low, value );
				high = max ( high, value );

			}

			if ( static_cast < int32_t > ( high - low ) > best ) {

				best = static_cast < int32_t > ( high - low );
				axis = a;

			}

		}

		return best;

	}

	gif_writer::~gif_writer ( ) {

		release_pages ( table );
		release ( previous );
		release ( current );
		release ( region );

	}

	bool gif_writer::open ( int32_t width, int32_t height ) {

		size_t pixels = static_cast < size_t > ( width ) * static_cast < size_t > ( height );

		wide = width;
		tall = height;

		if ( !table )
			table = static_cast < uint32_t* > ( allocate_pages ( color_space * sizeof ( uint32_t ) ) );
		else
			zero ( table, color_space * sizeof ( uint32_t ) );

		release ( previous );
		release ( current );
		release ( region );
		previous = static_cast < uint8_t* > ( allocate ( pixels ) );
		current = static_cast < uint8_t* > ( allocate ( pixels ) );
		region = static_cast < uint8_t* > ( allocate ( pixels ) );
		bytes.clear ( );
		written = 0;
		last_delay = -1;
		colors = 0;

		return table && previous && current && region;

	}

	void gif_writer::learn ( const uint8_t* rgba ) {

		size_t pixels = static_cast < size_t > ( wide ) * static_cast < size_t > ( tall );

		for ( size_t i = 0; i < pixels; i++ )
			table [ pack ( rgba + i * 4 ) ]++;

	}

	uint8_t gif_writer::nearest ( uint32_t color ) const {

		int32_t r = static_cast < int32_t > ( channel ( color, 0 ) );
		int32_t g = static_cast < int32_t > ( channel ( color, 1 ) );
		int32_t b = static_cast < int32_t > ( channel ( color, 2 ) );
		int32_t best = 0;
		int32_t best_distance = 0x7FFFFFFF;

		for ( int32_t i = 0; i < colors; i++ ) {

			int32_t dr = r - palette [ i * 3 ];
			int32_t dg = g - palette [ i * 3 + 1 ];
			int32_t db = b - palette [ i * 3 + 2 ];
			int32_t distance = dr * dr * 3 + dg * dg * 4 + db * db * 2;

			if ( distance < best_distance ) {

				best_distance = distance;
				best = i;

			}

		}

		return static_cast < uint8_t > ( best );

	}

	uint32_t gif_writer::shade_of ( uint32_t rgb ) const {

		uint32_t entry = table ? table [ rgb & ( color_space - 1 ) ] : 0;
		uint32_t index = entry & mapped ? entry & 255 : nearest ( rgb );

		return static_cast < uint32_t > ( palette [ index * 3 ] ) << 16 | static_cast < uint32_t > ( palette [ index * 3 + 1 ] ) << 8 | palette [ index * 3 + 2 ];

	}

	void gif_writer::seal ( ) {

		list < swatch > items;

		for ( uint32_t i = 0; i < color_space; i++ ) {

			if ( table [ i ] )
				items.push ( swatch { i, table [ i ] } );

		}

		sort ( items.data ( ), items.size ( ), [ ] ( const swatch& a, const swatch& b ) {

			return a.count > b.count || ( a.count == b.count && a.color < b.color );

		} );

		int32_t exact = items.size ( ) <= 256 ? items.size ( ) : exact_slots;

		for ( int32_t i = 0; i < exact; i++ ) {

			for ( int32_t a = 0; a < 3; a++ )
				palette [ i * 3 + a ] = static_cast < uint8_t > ( channel ( items [ i ].color, a ) );

		}

		bucket buckets [ 256 ];
		int32_t count = items.size ( ) > exact ? 1 : 0;
		int32_t room = 256 - exact;

		buckets [ 0 ] = bucket { exact, items.size ( ) };

		while ( count > 0 && count < room ) {

			int32_t chosen = -1;
			int32_t chosen_axis = 0;
			int32_t widest = 0;

			for ( int32_t i = 0; i < count; i++ ) {

				int32_t axis = 0;
				int32_t range = buckets [ i ].end - buckets [ i ].start > 1 ? spread ( items.data ( ), buckets [ i ], axis ) : 0;

				if ( range > widest ) {

					widest = range;
					chosen = i;
					chosen_axis = axis;

				}

			}

			if ( chosen < 0 )
				break;

			bucket range = buckets [ chosen ];
			int32_t size = range.end - range.start;
			swatch* sorted = static_cast < swatch* > ( allocate ( sizeof ( swatch ) * static_cast < size_t > ( size ) ) );
			int32_t starts [ 257 ] = { };
			uint64_t population = 0;

			for ( int32_t i = range.start; i < range.end; i++ ) {

				starts [ channel ( items [ i ].color, chosen_axis ) + 1 ]++;
				population += items [ i ].count;

			}

			for ( int32_t i = 1; i < 257; i++ )
				starts [ i ] += starts [ i - 1 ];

			for ( int32_t i = range.start; i < range.end; i++ )
				sorted [ starts [ channel ( items [ i ].color, chosen_axis ) ]++ ] = items [ i ];

			copy ( items.data ( ) + range.start, sorted, sizeof ( swatch ) * static_cast < size_t > ( size ) );
			release ( sorted );

			uint64_t running = 0;
			int32_t split = range.start + 1;

			for ( int32_t i = range.start; i < range.end - 1; i++ ) {

				running += items [ i ].count;
				split = i + 1;

				if ( running * 2 >= population )
					break;

			}

			buckets [ chosen ] = bucket { range.start, split };
			buckets [ count++ ] = bucket { split, range.end };

		}

		for ( int32_t i = 0; i < count; i++ ) {

			uint64_t sums [ 3 ] = { };
			uint64_t weight = 0;
			int32_t slot = exact + i;

			for ( int32_t k = buckets [ i ].start; k < buckets [ i ].end; k++ ) {

				for ( int32_t a = 0; a < 3; a++ )
					sums [ a ] += static_cast < uint64_t > ( channel ( items [ k ].color, a ) ) * items [ k ].count;

				weight += items [ k ].count;

			}

			for ( int32_t a = 0; a < 3; a++ )
				palette [ slot * 3 + a ] = static_cast < uint8_t > ( weight ? ( sums [ a ] + weight / 2 ) / weight : 0 );

		}

		colors = max ( exact + count, 2 );

		for ( int32_t i = 0; i < items.size ( ); i++ )
			table [ items [ i ].color ] = mapped | ( i < exact ? static_cast < uint32_t > ( i ) : nearest ( items [ i ].color ) );

		static const uint8_t signature [ ] = { 'G', 'I', 'F', '8', '9', 'a' };
		uint8_t screen [ 7 ] = { static_cast < uint8_t > ( wide ), static_cast < uint8_t > ( wide >> 8 ), static_cast < uint8_t > ( tall ), static_cast < uint8_t > ( tall >> 8 ), 0xF7, 0, 0 };
		static const uint8_t looping [ ] = { 0x21, 0xFF, 0x0B, 'N', 'E', 'T', 'S', 'C', 'A', 'P', 'E', '2', '.', '0', 0x03, 0x01, 0x00, 0x00, 0x00 };

		bytes.append ( signature, 6 );
		bytes.append ( screen, 7 );
		bytes.append ( palette, 768 );
		bytes.append ( looping, sizeof ( looping ) );

	}

	bool gif_writer::frame ( const uint8_t* rgba, uint32_t hundredths ) {

		size_t pixels = static_cast < size_t > ( wide ) * static_cast < size_t > ( tall );

		for ( size_t i = 0; i < pixels; i++ ) {

			uint32_t color = pack ( rgba + i * 4 );
			uint32_t entry = table [ color ];

			if ( !( entry & mapped ) ) {

				entry = mapped | nearest ( color );
				table [ color ] = entry;

			}

			current [ i ] = static_cast < uint8_t > ( entry );

		}

		int32_t left = 0;
		int32_t top = 0;
		int32_t right = wide - 1;
		int32_t bottom = tall - 1;

		if ( written ) {

			left = wide;
			top = tall;
			right = -1;
			bottom = -1;

			for ( int32_t y = 0; y < tall; y++ ) {

				const uint8_t* now = current + static_cast < size_t > ( y ) * wide;
				const uint8_t* before = previous + static_cast < size_t > ( y ) * wide;

				for ( int32_t x = 0; x < wide; x++ ) {

					if ( now [ x ] == before [ x ] )
						continue;

					left = min ( left, x );
					right = max ( right, x );
					top = min ( top, y );
					bottom = max ( bottom, y );

				}

			}

			if ( right < 0 ) {

				uint32_t delay = bytes [ last_delay ] | static_cast < uint32_t > ( bytes [ last_delay + 1 ] ) << 8;

				delay = min ( delay + hundredths, 65535u );
				bytes [ last_delay ] = static_cast < uint8_t > ( delay );
				bytes [ last_delay + 1 ] = static_cast < uint8_t > ( delay >> 8 );

				return true;

			}

		}

		int32_t across = right - left + 1;
		int32_t down = bottom - top + 1;
		uint8_t control [ 8 ] = { 0x21, 0xF9, 0x04, 0x04, static_cast < uint8_t > ( hundredths ), static_cast < uint8_t > ( hundredths >> 8 ), 0x00, 0x00 };
		uint8_t descriptor [ 10 ] = { 0x2C, static_cast < uint8_t > ( left ), static_cast < uint8_t > ( left >> 8 ), static_cast < uint8_t > ( top ), static_cast < uint8_t > ( top >> 8 ), static_cast < uint8_t > ( across ), static_cast < uint8_t > ( across >> 8 ), static_cast < uint8_t > ( down ), static_cast < uint8_t > ( down >> 8 ), 0x00 };

		bytes.append ( control, 8 );
		last_delay = bytes.size ( ) - 4;
		bytes.append ( descriptor, 10 );

		for ( int32_t y = 0; y < down; y++ )
			copy ( region + static_cast < size_t > ( y ) * across, current + static_cast < size_t > ( top + y ) * wide + left, static_cast < size_t > ( across ) );

		write_codes ( region, across * down );

		uint8_t* swap = previous;

		previous = current;
		current = swap;
		written++;

		return true;

	}

	void gif_writer::write_codes ( const uint8_t* indices, int32_t count ) {

		static constexpr int32_t table_size = 8192;
		int32_t* keys = static_cast < int32_t* > ( allocate ( table_size * sizeof ( int32_t ) ) );
		uint16_t* codes = static_cast < uint16_t* > ( allocate ( table_size * sizeof ( uint16_t ) ) );
		uint8_t block [ 256 ];
		int32_t block_used = 0;
		uint32_t bits = 0;
		int32_t pending = 0;
		int32_t code_size = 9;
		int32_t next = 258;

		auto reset = [ & ] ( ) {

			fill ( keys, 0xFF, table_size * sizeof ( int32_t ) );

		};

		auto put = [ & ] ( uint32_t code ) {

			bits |= code << pending;
			pending += code_size;

			while ( pending >= 8 ) {

				block [ block_used++ ] = static_cast < uint8_t > ( bits );
				bits >>= 8;
				pending -= 8;

				if ( block_used == 255 ) {

					bytes.push ( 255 );
					bytes.append ( block, 255 );
					block_used = 0;

				}

			}

		};

		auto widen = [ & ] ( ) {

			if ( next > ( 1 << code_size ) - 1 && code_size < 12 )
				code_size++;

		};

		reset ( );
		bytes.push ( 8 );
		put ( 256 );

		int32_t prefix = indices [ 0 ];

		for ( int32_t i = 1; i < count; i++ ) {

			int32_t key = prefix << 8 | indices [ i ];
			uint32_t slot = static_cast < uint32_t > ( key ) * 2654435761u >> 19;
			bool found = false;

			while ( keys [ slot ] != -1 ) {

				if ( keys [ slot ] == key ) {

					prefix = codes [ slot ];
					found = true;

					break;

				}

				slot = ( slot + 1 ) & ( table_size - 1 );

			}

			if ( found )
				continue;

			put ( static_cast < uint32_t > ( prefix ) );
			widen ( );

			if ( next < 4096 ) {

				keys [ slot ] = key;
				codes [ slot ] = static_cast < uint16_t > ( next++ );

			} else {

				put ( 256 );
				reset ( );
				next = 258;
				code_size = 9;

			}

			prefix = indices [ i ];

		}

		put ( static_cast < uint32_t > ( prefix ) );
		widen ( );
		put ( 257 );

		if ( pending > 0 )
			block [ block_used++ ] = static_cast < uint8_t > ( bits );

		if ( block_used ) {

			bytes.push ( static_cast < uint8_t > ( block_used ) );
			bytes.append ( block, block_used );

		}

		bytes.push ( 0 );
		release ( keys );
		release ( codes );

	}

	bool gif_writer::finish ( list < uint8_t >& out ) {

		bytes.push ( 0x3B );
		out.clear ( );

		return out.append ( bytes.data ( ), bytes.size ( ) );

	}

}
