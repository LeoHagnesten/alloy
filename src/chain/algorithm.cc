#include "chain/algorithm.hh"
#include "base/strings.hh"

namespace chain {

	struct algorithm_facts {

		const wchar_t* name;
		const wchar_t* key;
		const wchar_t* coins;
		double top;

	};

	static const algorithm_facts facts [ ALGORITHM_COUNT ] = {

		{ L"SHA-256d", L"sha256d", L"Bitcoin, Bitcoin Cash", 4294901760.0 },
		{ L"Scrypt", L"scrypt", L"Litecoin, Dogecoin", 281470681743360.0 },
		{ L"RandomX", L"randomx", L"Monero, Wownero", 18446744073709551616.0 },
		{ L"kHeavyHash", L"kheavyhash", L"Kaspa", 4294901760.0 },

	};

	static const algorithm_facts& facts_of ( uint8_t value ) {

		return facts [ value < ALGORITHM_COUNT ? value : 0 ];

	}

	const wchar_t* algorithm_name ( uint8_t value ) {

		return facts_of ( value ).name;

	}

	const wchar_t* algorithm_key ( uint8_t value ) {

		return facts_of ( value ).key;

	}

	const wchar_t* algorithm_coins ( uint8_t value ) {

		return facts_of ( value ).coins;

	}

	bool read_algorithm ( const wchar_t* text, uint8_t& out ) {

		static const wchar_t* const spellings [ ] [ 2 ] = {

			{ L"sha256", L"bitcoin" },
			{ L"ltc", L"litecoin" },
			{ L"rx/0", L"monero" },
			{ L"kas", L"kaspa" },

		};

		for ( uint8_t i = 0; i < ALGORITHM_COUNT; i++ ) {

			if ( base::same_folded ( text, facts [ i ].key ) || base::same_folded ( text, facts [ i ].name ) || base::same_folded ( text, spellings [ i ] [ 0 ] ) || base::same_folded ( text, spellings [ i ] [ 1 ] ) ) {

				out = i;

				return true;

			}

		}

		return false;

	}

	double unit_top ( uint8_t value ) {

		return facts_of ( value ).top;

	}

	bool uses_header ( uint8_t value ) {

		return value == ALGORITHM_SHA256D || value == ALGORITHM_SCRYPT;

	}

}
