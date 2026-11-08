#include "randomx/superscalar.hh"
#include "hash/blake2b.hh"
#include "base/bits.hh"
#include "base/memory.hh"

namespace randomx {

	static constexpr int32_t cycle_room = superscalar_latency + 4;
	static constexpr int32_t look_ahead = 4;
	static constexpr int32_t throw_limit = 256;
	static constexpr int32_t group_none = -1;

	enum port : uint8_t {

		PORT_NONE = 0,
		PORT_0 = 1,
		PORT_1 = 2,
		PORT_5 = 4,
		PORT_01 = 3,
		PORT_05 = 5,
		PORT_015 = 7,

	};

	struct macro_op {

		int32_t latency;
		uint8_t first;
		uint8_t second;
		bool dependent;

	};

	struct op_info {

		int32_t type;
		macro_op ops [ 3 ];
		int32_t count;
		int32_t result_op;
		int32_t dst_op;
		int32_t src_op;

	};

	static constexpr macro_op sub_rr = { 1, PORT_015, PORT_NONE, false };
	static constexpr macro_op xor_rr = { 1, PORT_015, PORT_NONE, false };
	static constexpr macro_op lea_sib = { 1, PORT_01, PORT_NONE, false };
	static constexpr macro_op imul_rr = { 3, PORT_1, PORT_NONE, false };
	static constexpr macro_op ror_ri = { 1, PORT_05, PORT_NONE, false };
	static constexpr macro_op add_ri = { 1, PORT_015, PORT_NONE, false };
	static constexpr macro_op xor_ri = { 1, PORT_015, PORT_NONE, false };
	static constexpr macro_op mov_rr = { 0, PORT_NONE, PORT_NONE, false };
	static constexpr macro_op mul_r = { 4, PORT_1, PORT_5, false };
	static constexpr macro_op mov_ri64 = { 1, PORT_015, PORT_NONE, false };
	static constexpr macro_op imul_rr_chained = { 3, PORT_1, PORT_NONE, true };

	static constexpr op_info info_isub_r = { SCALAR_ISUB_R, { sub_rr }, 1, 0, 0, 0 };
	static constexpr op_info info_ixor_r = { SCALAR_IXOR_R, { xor_rr }, 1, 0, 0, 0 };
	static constexpr op_info info_iadd_rs = { SCALAR_IADD_RS, { lea_sib }, 1, 0, 0, 0 };
	static constexpr op_info info_imul_r = { SCALAR_IMUL_R, { imul_rr }, 1, 0, 0, 0 };
	static constexpr op_info info_iror_c = { SCALAR_IROR_C, { ror_ri }, 1, 0, 0, -1 };
	static constexpr op_info info_iadd_c7 = { SCALAR_IADD_C7, { add_ri }, 1, 0, 0, -1 };
	static constexpr op_info info_ixor_c7 = { SCALAR_IXOR_C7, { xor_ri }, 1, 0, 0, -1 };
	static constexpr op_info info_iadd_c8 = { SCALAR_IADD_C8, { add_ri }, 1, 0, 0, -1 };
	static constexpr op_info info_ixor_c8 = { SCALAR_IXOR_C8, { xor_ri }, 1, 0, 0, -1 };
	static constexpr op_info info_iadd_c9 = { SCALAR_IADD_C9, { add_ri }, 1, 0, 0, -1 };
	static constexpr op_info info_ixor_c9 = { SCALAR_IXOR_C9, { xor_ri }, 1, 0, 0, -1 };
	static constexpr op_info info_imulh_r = { SCALAR_IMULH_R, { mov_rr, mul_r, mov_rr }, 3, 1, 0, 1 };
	static constexpr op_info info_ismulh_r = { SCALAR_ISMULH_R, { mov_rr, mul_r, mov_rr }, 3, 1, 0, 1 };
	static constexpr op_info info_imul_rcp = { SCALAR_IMUL_RCP, { mov_ri64, imul_rr_chained }, 2, 1, 1, -1 };
	static constexpr op_info info_nop = { group_none, { }, 0, 0, 0, -1 };

	struct decoder {

		int32_t index;
		int32_t count;
		int32_t slots [ 4 ];

	};

	static constexpr decoder decoders [ 6 ] = {

		{ 0, 3, { 4, 8, 4 } },
		{ 1, 4, { 7, 3, 3, 3 } },
		{ 2, 4, { 3, 7, 3, 3 } },
		{ 3, 3, { 4, 9, 3 } },
		{ 4, 4, { 4, 4, 4, 4 } },
		{ 5, 3, { 3, 3, 10 } },

	};

	struct register_state {

		int32_t latency = 0;
		int32_t last_group = group_none;
		int32_t last_par = -1;

	};

	struct candidate {

		const op_info* info = &info_nop;
		int32_t src = -1;
		int32_t dst = -1;
		int32_t mod = 0;
		uint32_t imm32 = 0;
		int32_t group = group_none;
		int32_t group_par = -1;
		bool can_reuse = false;
		bool par_is_source = false;

	};

	static bool zero_or_power ( uint32_t value ) {

		return ( value & ( value - 1 ) ) == 0;

	}

	static bool multiplies ( int32_t type ) {

		return type == SCALAR_IMUL_R || type == SCALAR_IMULH_R || type == SCALAR_ISMULH_R || type == SCALAR_IMUL_RCP;

	}

	seed_generator::seed_generator ( const void* seed, size_t size ) {

		base::copy ( data, seed, base::min ( size, static_cast < size_t > ( 60 ) ) );

	}

	void seed_generator::refill ( uint32_t needed ) {

		if ( used + needed <= 64 )
			return;

		hash::blake2b_once ( data, 64, data, 64 );
		used = 0;

	}

	uint8_t seed_generator::next_byte ( ) {

		refill ( 1 );

		return data [ used++ ];

	}

	uint32_t seed_generator::next_word ( ) {

		refill ( 4 );

		uint32_t value = base::load_little ( data + used );

		used += 4;

		return value;

	}

	uint64_t reciprocal ( uint32_t divisor ) {

		uint64_t top = 1ull << 63;
		uint64_t quotient = top / divisor;
		uint64_t remainder = top % divisor;
		uint32_t bits = 0;

		for ( uint64_t value = divisor; value; value >>= 1 )
			bits++;

		for ( uint32_t i = 0; i < bits; i++ ) {

			if ( remainder >= divisor - remainder ) {

				quotient = quotient * 2 + 1;
				remainder = remainder * 2 - divisor;

			} else {

				quotient = quotient * 2;
				remainder = remainder * 2;

			}

		}

		return quotient;

	}

	static const decoder& next_decoder ( int32_t previous, int32_t cycle, int32_t multiplications, seed_generator& random ) {

		if ( previous == SCALAR_IMULH_R || previous == SCALAR_ISMULH_R )
			return decoders [ 5 ];

		if ( multiplications < cycle + 1 )
			return decoders [ 4 ];

		if ( previous == SCALAR_IMUL_RCP )
			return random.next_byte ( ) & 1 ? decoders [ 0 ] : decoders [ 3 ];

		return decoders [ random.next_byte ( ) & 3 ];

	}

	static void create ( candidate& item, const op_info* info, seed_generator& random ) {

		item.info = info;
		item.src = -1;
		item.dst = -1;
		item.can_reuse = false;
		item.par_is_source = false;

		switch ( info->type ) {

			case SCALAR_ISUB_R:
				item.mod = 0;
				item.imm32 = 0;
				item.group = SCALAR_IADD_RS;
				item.par_is_source = true;
				break;

			case SCALAR_IXOR_R:
				item.mod = 0;
				item.imm32 = 0;
				item.group = SCALAR_IXOR_R;
				item.par_is_source = true;
				break;

			case SCALAR_IADD_RS:
				item.mod = random.next_byte ( );
				item.imm32 = 0;
				item.group = SCALAR_IADD_RS;
				item.par_is_source = true;
				break;

			case SCALAR_IMUL_R:
				item.mod = 0;
				item.imm32 = 0;
				item.group = SCALAR_IMUL_R;
				item.par_is_source = true;
				break;

			case SCALAR_IROR_C:
				item.mod = 0;

				do {

					item.imm32 = random.next_byte ( ) & 63;

				} while ( item.imm32 == 0 );

				item.group = SCALAR_IROR_C;
				item.group_par = -1;
				break;

			case SCALAR_IADD_C7:
			case SCALAR_IADD_C8:
			case SCALAR_IADD_C9:
				item.mod = 0;
				item.imm32 = random.next_word ( );
				item.group = SCALAR_IADD_C7;
				item.group_par = -1;
				break;

			case SCALAR_IXOR_C7:
			case SCALAR_IXOR_C8:
			case SCALAR_IXOR_C9:
				item.mod = 0;
				item.imm32 = random.next_word ( );
				item.group = SCALAR_IXOR_C7;
				item.group_par = -1;
				break;

			case SCALAR_IMULH_R:
			case SCALAR_ISMULH_R:
				item.can_reuse = true;
				item.mod = 0;
				item.imm32 = 0;
				item.group = info->type;
				item.group_par = static_cast < int32_t > ( random.next_word ( ) );
				break;

			case SCALAR_IMUL_RCP:
				item.mod = 0;

				do {

					item.imm32 = random.next_word ( );

				} while ( zero_or_power ( item.imm32 ) );

				item.group = SCALAR_IMUL_RCP;
				item.group_par = -1;
				break;

			default:
				break;

		}

	}

	static void create_for_slot ( candidate& item, seed_generator& random, int32_t slot, int32_t kind, bool last ) {

		static const op_info* const three [ 2 ] = { &info_isub_r, &info_ixor_r };
		static const op_info* const three_last [ 4 ] = { &info_isub_r, &info_ixor_r, &info_imulh_r, &info_ismulh_r };
		static const op_info* const four [ 2 ] = { &info_iror_c, &info_iadd_rs };
		static const op_info* const seven [ 2 ] = { &info_ixor_c7, &info_iadd_c7 };
		static const op_info* const eight [ 2 ] = { &info_ixor_c8, &info_iadd_c8 };
		static const op_info* const nine [ 2 ] = { &info_ixor_c9, &info_iadd_c9 };

		switch ( slot ) {

			case 3:
				create ( item, last ? three_last [ random.next_byte ( ) & 3 ] : three [ random.next_byte ( ) & 1 ], random );
				break;

			case 4:
				create ( item, kind == 4 && !last ? &info_imul_r : four [ random.next_byte ( ) & 1 ], random );
				break;

			case 7:
				create ( item, seven [ random.next_byte ( ) & 1 ], random );
				break;

			case 8:
				create ( item, eight [ random.next_byte ( ) & 1 ], random );
				break;

			case 9:
				create ( item, nine [ random.next_byte ( ) & 1 ], random );
				break;

			default:
				create ( item, &info_imul_rcp, random );
				break;

		}

	}

	static bool pick ( const int32_t* choices, int32_t count, seed_generator& random, int32_t& out ) {

		if ( !count )
			return false;

		out = choices [ count > 1 ? random.next_word ( ) % static_cast < uint32_t > ( count ) : 0 ];

		return true;

	}

	static bool choose_source ( candidate& item, int32_t cycle, const register_state* registers, seed_generator& random ) {

		int32_t choices [ 8 ];
		int32_t count = 0;

		for ( int32_t i = 0; i < 8; i++ ) {

			if ( registers [ i ].latency <= cycle )
				choices [ count++ ] = i;

		}

		if ( count == 2 && item.info->type == SCALAR_IADD_RS && ( choices [ 0 ] == 5 || choices [ 1 ] == 5 ) ) {

			item.src = 5;
			item.group_par = 5;

			return true;

		}

		if ( !pick ( choices, count, random, item.src ) )
			return false;

		if ( item.par_is_source )
			item.group_par = item.src;

		return true;

	}

	static bool choose_destination ( candidate& item, int32_t cycle, bool chained, const register_state* registers, seed_generator& random ) {

		int32_t choices [ 8 ];
		int32_t count = 0;

		for ( int32_t i = 0; i < 8; i++ ) {

			const register_state& slot = registers [ i ];

			if ( slot.latency > cycle || ( !item.can_reuse && i == item.src ) )
				continue;

			if ( !chained && item.group == SCALAR_IMUL_R && slot.last_group == SCALAR_IMUL_R )
				continue;

			if ( slot.last_group == item.group && slot.last_par == item.group_par )
				continue;

			if ( item.info->type == SCALAR_IADD_RS && i == 5 )
				continue;

			choices [ count++ ] = i;

		}

		return pick ( choices, count, random, item.dst );

	}

	static int32_t schedule_uop ( uint8_t uop, uint8_t ( *busy ) [ 3 ], int32_t cycle, bool commit, int32_t limit ) {

		for ( ; cycle < limit; cycle++ ) {

			if ( ( uop & PORT_5 ) && !busy [ cycle ] [ 2 ] ) {

				if ( commit )
					busy [ cycle ] [ 2 ] = uop;

				return cycle;

			}

			if ( ( uop & PORT_0 ) && !busy [ cycle ] [ 0 ] ) {

				if ( commit )
					busy [ cycle ] [ 0 ] = uop;

				return cycle;

			}

			if ( ( uop & PORT_1 ) && !busy [ cycle ] [ 1 ] ) {

				if ( commit )
					busy [ cycle ] [ 1 ] = uop;

				return cycle;

			}

		}

		return -1;

	}

	static int32_t schedule_op ( const macro_op& op, uint8_t ( *busy ) [ 3 ], int32_t cycle, int32_t ready, bool commit, int32_t limit ) {

		if ( op.dependent )
			cycle = base::max ( cycle, ready );

		if ( op.first == PORT_NONE )
			return cycle;

		if ( op.second == PORT_NONE )
			return schedule_uop ( op.first, busy, cycle, commit, limit );

		for ( ; cycle < limit; cycle++ ) {

			int32_t first = schedule_uop ( op.first, busy, cycle, false, limit );
			int32_t second = schedule_uop ( op.second, busy, cycle, false, limit );

			if ( first >= 0 && first == second ) {

				if ( commit ) {

					schedule_uop ( op.first, busy, first, true, limit );
					schedule_uop ( op.second, busy, second, true, limit );

				}

				return first;

			}

		}

		return -1;

	}

	void generate_superscalar ( scalar_program& out, seed_generator& random, int32_t latency ) {

		uint8_t busy [ cycle_room ] [ 3 ] = { };
		register_state registers [ 8 ];
		candidate current;
		int32_t op_index = 0;
		int32_t cycle = 0;
		int32_t ready = 0;
		bool saturated = false;
		int32_t size = 0;
		int32_t multiplications = 0;
		int32_t thrown = 0;

		int32_t room = base::min ( 3 * latency + 2, superscalar_room );

		for ( int32_t decode_cycle = 0; decode_cycle < latency && !saturated && size < room; decode_cycle++ ) {

			const decoder& buffer = next_decoder ( current.info->type, decode_cycle, multiplications, random );
			int32_t slot = 0;

			while ( slot < buffer.count ) {

				int32_t top = cycle;

				if ( op_index >= current.info->count ) {

					if ( saturated || size >= room )
						break;

					create_for_slot ( current, random, buffer.slots [ slot ], buffer.index, buffer.count == slot + 1 );
					op_index = 0;

				}

				const macro_op& op = current.info->ops [ op_index ];
				int32_t when = schedule_op ( op, busy, cycle, ready, false, latency + 4 );

				if ( when < 0 ) {

					saturated = true;

					break;

				}

				if ( op_index == current.info->src_op ) {

					int32_t forward = 0;

					for ( ; forward < look_ahead && !choose_source ( current, when, registers, random ); forward++ ) {

						when++;
						cycle++;

					}

					if ( forward == look_ahead ) {

						if ( thrown < throw_limit ) {

							thrown++;
							op_index = current.info->count;

							continue;

						}

						current = candidate { };

						break;

					}

				}

				if ( op_index == current.info->dst_op ) {

					int32_t forward = 0;

					for ( ; forward < look_ahead && !choose_destination ( current, when, thrown > 0, registers, random ); forward++ ) {

						when++;
						cycle++;

					}

					if ( forward == look_ahead ) {

						if ( thrown < throw_limit ) {

							thrown++;
							op_index = current.info->count;

							continue;

						}

						current = candidate { };

						break;

					}

				}

				thrown = 0;
				when = schedule_op ( op, busy, when, when, true, latency + 4 );

				if ( when < 0 ) {

					saturated = true;

					break;

				}

				ready = when + op.latency;

				if ( op_index == current.info->result_op ) {

					register_state& target = registers [ current.dst ];

					target.latency = ready;
					target.last_group = current.group;
					target.last_par = current.group_par;

				}

				slot++;
				op_index++;

				if ( when >= latency )
					saturated = true;

				cycle = top;

				if ( op_index >= current.info->count ) {

					instruction& code = out.code [ size++ ];

					code.opcode = static_cast < uint8_t > ( current.info->type );
					code.dst = static_cast < uint8_t > ( current.dst );
					code.src = static_cast < uint8_t > ( current.src >= 0 ? current.src : current.dst );
					code.mod = static_cast < uint8_t > ( current.mod );
					code.imm32 = current.imm32;
					multiplications += multiplies ( current.info->type ) ? 1 : 0;

				}

			}

			cycle++;

		}

		int32_t latencies [ 8 ] = { };
		int32_t longest = 0;

		out.address_register = 0;
		out.size = size;

		for ( int32_t i = 0; i < size; i++ ) {

			const instruction& code = out.code [ i ];
			int32_t through_dst = latencies [ code.dst ] + 1;
			int32_t through_src = code.dst != code.src ? latencies [ code.src ] + 1 : 0;

			latencies [ code.dst ] = base::max ( through_dst, through_src );
			out.reciprocals [ i ] = code.opcode == SCALAR_IMUL_RCP ? reciprocal ( code.imm32 ) : 0;

		}

		for ( int32_t i = 0; i < 8; i++ ) {

			if ( latencies [ i ] > longest ) {

				longest = latencies [ i ];
				out.address_register = i;

			}

		}

	}

	static inline uint64_t high_product ( uint64_t a, uint64_t b ) {

		return static_cast < uint64_t > ( static_cast < unsigned __int128 > ( a ) * b >> 64 );

	}

	static inline uint64_t signed_high_product ( uint64_t a, uint64_t b ) {

		return static_cast < uint64_t > ( static_cast < __int128 > ( static_cast < int64_t > ( a ) ) * static_cast < int64_t > ( b ) >> 64 );

	}

	void run_superscalar ( uint64_t* r, const scalar_program& program ) {

		for ( int32_t i = 0; i < program.size; i++ ) {

			const instruction& code = program.code [ i ];
			uint64_t& dst = r [ code.dst ];
			uint64_t src = r [ code.src ];
			uint64_t wide = static_cast < uint64_t > ( static_cast < int64_t > ( static_cast < int32_t > ( code.imm32 ) ) );

			switch ( code.opcode ) {

				case SCALAR_ISUB_R:
					dst -= src;
					break;

				case SCALAR_IXOR_R:
					dst ^= src;
					break;

				case SCALAR_IADD_RS:
					dst += src << ( code.mod >> 2 & 3 );
					break;

				case SCALAR_IMUL_R:
					dst *= src;
					break;

				case SCALAR_IROR_C:
					dst = dst >> code.imm32 | dst << ( 64 - code.imm32 );
					break;

				case SCALAR_IADD_C7:
				case SCALAR_IADD_C8:
				case SCALAR_IADD_C9:
					dst += wide;
					break;

				case SCALAR_IXOR_C7:
				case SCALAR_IXOR_C8:
				case SCALAR_IXOR_C9:
					dst ^= wide;
					break;

				case SCALAR_IMULH_R:
					dst = high_product ( dst, src );
					break;

				case SCALAR_ISMULH_R:
					dst = signed_high_product ( dst, src );
					break;

				case SCALAR_IMUL_RCP:
					dst *= program.reciprocals [ i ];
					break;

			}

		}

	}

}
