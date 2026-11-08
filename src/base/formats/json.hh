#pragma once

#include "base/text.hh"

namespace base {

	enum json_kind : uint8_t {

		JSON_NULL,
		JSON_BOOL,
		JSON_NUMBER,
		JSON_STRING,
		JSON_ARRAY,
		JSON_OBJECT,

	};

	struct json_node {

		json_kind kind = JSON_NULL;
		bool truth = false;
		int32_t start = 0;
		int32_t size = 0;
		int32_t name_start = 0;
		int32_t name_size = 0;
		int32_t first = -1;
		int32_t next = -1;
		int32_t children = 0;
		double number = 0.0;

	};

	class json {

	public:

		static constexpr int32_t room = 768;
		static constexpr int32_t most = 1 << 21;

		json ( ) = default;

		json ( const json& ) = delete;

		json& operator = ( const json& ) = delete;

		~json ( );

		bool parse ( const char* text, int32_t size );

		int32_t member ( int32_t object, const char* name ) const;

		int32_t item ( int32_t array, int32_t index ) const;

		int32_t count ( int32_t node ) const;

		json_kind kind ( int32_t node ) const;

		bool is_null ( int32_t node ) const;

		double number ( int32_t node ) const;

		bool truth ( int32_t node ) const;

		int32_t text ( int32_t node, char* out, int32_t limit ) const;

		bool text_is ( int32_t node, const char* value ) const;

		const char* raw ( int32_t node ) const;

		int32_t raw_size ( int32_t node ) const;

	private:

		int32_t value ( int32_t& at, int32_t depth );

		void skip_space ( int32_t& at ) const;

		bool string_end ( int32_t& at ) const;

		bool grow ( );

		const char* source = nullptr;
		int32_t length = 0;
		json_node inline_nodes [ room ];
		json_node* nodes = inline_nodes;
		int32_t capacity = room;
		int32_t used = 0;

	};

	void add_json_string ( line& out, const char* value );

}
