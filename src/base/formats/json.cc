#include "base/formats/json.hh"
#include "base/memory.hh"
#include "base/strings.hh"

namespace base {

	json::~json ( ) {

		if ( nodes != inline_nodes )
			delete [ ] nodes;

	}

	bool json::grow ( ) {

		if ( capacity >= most )
			return false;

		int32_t larger = capacity * 4;
		json_node* bigger = new json_node [ larger ];

		base::copy ( bigger, nodes, sizeof ( json_node ) * static_cast < size_t > ( used ) );

		if ( nodes != inline_nodes )
			delete [ ] nodes;

		nodes = bigger;
		capacity = larger;

		return true;

	}

	bool json::parse ( const char* text, int32_t size ) {

		int32_t at = 0;

		source = text;
		length = size;
		used = 0;

		if ( value ( at, 0 ) != 0 )
			return false;

		skip_space ( at );

		return at == length;

	}

	void json::skip_space ( int32_t& at ) const {

		while ( at < length && ( source [ at ] == ' ' || source [ at ] == '\t' || source [ at ] == '\r' || source [ at ] == '\n' ) )
			at++;

	}

	bool json::string_end ( int32_t& at ) const {

		while ( at < length && source [ at ] != '"' ) {

			if ( source [ at ] == '\\' )
				at++;

			at++;

		}

		if ( at >= length )
			return false;

		at++;

		return true;

	}

	int32_t json::value ( int32_t& at, int32_t depth ) {

		skip_space ( at );

		if ( at >= length || depth > 32 || ( used >= capacity && !grow ( ) ) )
			return -1;

		int32_t index = used++;
		json_node& node = nodes [ index ];
		char lead = source [ at ];

		node = json_node { };
		node.start = at;

		if ( lead == '{' || lead == '[' ) {

			bool object = lead == '{';
			char close = object ? '}' : ']';
			int32_t last = -1;

			node.kind = object ? JSON_OBJECT : JSON_ARRAY;
			at++;
			skip_space ( at );

			if ( at < length && source [ at ] == close ) {

				at++;
				nodes [ index ].size = at - nodes [ index ].start;

				return index;

			}

			while ( at < length ) {

				int32_t name_start = 0;
				int32_t name_size = 0;

				if ( object ) {

					skip_space ( at );

					if ( at >= length || source [ at ] != '"' )
						return -1;

					name_start = ++at;

					if ( !string_end ( at ) )
						return -1;

					name_size = at - 1 - name_start;
					skip_space ( at );

					if ( at >= length || source [ at ] != ':' )
						return -1;

					at++;

				}

				int32_t child = value ( at, depth + 1 );

				if ( child < 0 )
					return -1;

				nodes [ child ].name_start = name_start;
				nodes [ child ].name_size = name_size;

				if ( last < 0 )
					nodes [ index ].first = child;
				else
					nodes [ last ].next = child;

				last = child;
				nodes [ index ].children++;
				skip_space ( at );

				if ( at < length && source [ at ] == ',' ) {

					at++;

					continue;

				}

				if ( at < length && source [ at ] == close ) {

					at++;
					nodes [ index ].size = at - nodes [ index ].start;

					return index;

				}

				return -1;

			}

			return -1;

		}

		if ( lead == '"' ) {

			node.kind = JSON_STRING;
			node.start = ++at;

			if ( !string_end ( at ) )
				return -1;

			node.size = at - 1 - node.start;

			return index;

		}

		if ( lead == 't' || lead == 'f' || lead == 'n' ) {

			const char* word = lead == 't' ? "true" : ( lead == 'f' ? "false" : "null" );
			int32_t size = base::length ( word );

			if ( at + size > length || !same_span ( source + at, size, word ) )
				return -1;

			node.kind = lead == 'n' ? JSON_NULL : JSON_BOOL;
			node.truth = lead == 't';
			node.size = size;
			at += size;

			return index;

		}

		if ( lead == '-' || ( lead >= '0' && lead <= '9' ) ) {

			int32_t end = at + 1;

			while ( end < length && ( ( source [ end ] >= '0' && source [ end ] <= '9' ) || source [ end ] == '.' || source [ end ] == 'e' || source [ end ] == 'E' || source [ end ] == '+' || source [ end ] == '-' ) )
				end++;

			node.kind = JSON_NUMBER;
			node.size = end - at;
			node.number = read_decimal ( source + at, node.size );
			at = end;

			return index;

		}

		return -1;

	}

	int32_t json::member ( int32_t object, const char* name ) const {

		if ( object < 0 || object >= used || nodes [ object ].kind != JSON_OBJECT )
			return -1;

		int32_t size = base::length ( name );

		for ( int32_t child = nodes [ object ].first; child >= 0; child = nodes [ child ].next ) {

			if ( nodes [ child ].name_size == size && same_span ( source + nodes [ child ].name_start, size, name ) )
				return child;

		}

		return -1;

	}

	int32_t json::item ( int32_t array, int32_t index ) const {

		if ( array < 0 || array >= used || nodes [ array ].kind != JSON_ARRAY )
			return -1;

		int32_t child = nodes [ array ].first;

		for ( int32_t i = 0; i < index && child >= 0; i++ )
			child = nodes [ child ].next;

		return child;

	}

	int32_t json::count ( int32_t node ) const {

		return node >= 0 && node < used ? nodes [ node ].children : 0;

	}

	json_kind json::kind ( int32_t node ) const {

		return node >= 0 && node < used ? nodes [ node ].kind : JSON_NULL;

	}

	bool json::is_null ( int32_t node ) const {

		return kind ( node ) == JSON_NULL;

	}

	double json::number ( int32_t node ) const {

		return kind ( node ) == JSON_NUMBER ? nodes [ node ].number : 0.0;

	}

	bool json::truth ( int32_t node ) const {

		return kind ( node ) == JSON_BOOL && nodes [ node ].truth;

	}

	int32_t json::text ( int32_t node, char* out, int32_t limit ) const {

		int32_t written = 0;

		if ( limit <= 0 )
			return 0;

		if ( kind ( node ) != JSON_STRING ) {

			out [ 0 ] = 0;

			return 0;

		}

		const char* in = source + nodes [ node ].start;
		int32_t size = nodes [ node ].size;

		for ( int32_t i = 0; i < size && written < limit - 1; i++ ) {

			char value = in [ i ];

			if ( value != '\\' || i + 1 >= size ) {

				out [ written++ ] = value;

				continue;

			}

			char escape = in [ ++i ];

			if ( escape == 'u' && i + 4 < size ) {

				uint32_t point = 0;

				for ( int32_t k = 1; k <= 4; k++ )
					point = point << 4 | static_cast < uint32_t > ( max ( hex_digit ( in [ i + k ] ), 0 ) );

				i += 4;

				if ( point < 0x80 )
					out [ written++ ] = static_cast < char > ( point );
				else if ( point < 0x800 && written + 2 < limit ) {

					out [ written++ ] = static_cast < char > ( 0xC0 | point >> 6 );
					out [ written++ ] = static_cast < char > ( 0x80 | ( point & 0x3F ) );

				} else if ( written + 3 < limit ) {

					out [ written++ ] = static_cast < char > ( 0xE0 | point >> 12 );
					out [ written++ ] = static_cast < char > ( 0x80 | ( point >> 6 & 0x3F ) );
					out [ written++ ] = static_cast < char > ( 0x80 | ( point & 0x3F ) );

				}

				continue;

			}

			if ( escape == 'n' )
				out [ written++ ] = '\n';
			else if ( escape == 't' )
				out [ written++ ] = '\t';
			else if ( escape == 'r' )
				out [ written++ ] = '\r';
			else if ( escape == 'b' || escape == 'f' )
				out [ written++ ] = ' ';
			else
				out [ written++ ] = escape;

		}

		out [ written ] = 0;

		return written;

	}

	bool json::text_is ( int32_t node, const char* value ) const {

		return kind ( node ) == JSON_STRING && same_span ( source + nodes [ node ].start, nodes [ node ].size, value );

	}

	const char* json::raw ( int32_t node ) const {

		return node >= 0 && node < used ? source + nodes [ node ].start : "";

	}

	int32_t json::raw_size ( int32_t node ) const {

		return node >= 0 && node < used ? nodes [ node ].size : 0;

	}

	void add_json_string ( line& out, const char* value ) {

		static constexpr char digits [ ] = "0123456789abcdef";

		out.put ( '"' );

		for ( int32_t i = 0; value && value [ i ]; i++ ) {

			char item = value [ i ];

			if ( item == '"' || item == '\\' )
				out.put ( '\\' ).put ( item );
			else if ( static_cast < uint8_t > ( item ) < 0x20 )
				out.add ( "\\u00" ).put ( digits [ item >> 4 & 15 ] ).put ( digits [ item & 15 ] );
			else
				out.put ( item );

		}

		out.put ( '"' );

	}

}
