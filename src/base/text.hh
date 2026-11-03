#pragma once

#include "base/base.hh"

namespace base {

	class text {

	public:

		static constexpr int32_t room = 512;

		text& add ( const wchar_t* value );

		text& number ( uint64_t value );

		text& decimal ( double value, int32_t places );

		text& hex ( const uint8_t* bytes, size_t count, bool reversed );

		text& rate ( double per_second, const wchar_t* unit );

		const wchar_t* get ( ) const {

			return buffer;

		}

		int32_t size ( ) const {

			return used;

		}

		void clear ( ) {

			used = 0;
			buffer [ 0 ] = 0;

		}

	private:

		void put ( wchar_t value );

		wchar_t buffer [ room ] = { };
		int32_t used = 0;

	};

}