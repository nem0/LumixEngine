// Tests for the bitwise operators: & | ^ ~ << >> and their compound forms.
//
// Two kinds of tests live here:
//  - a generated matrix that, for every integer width, checks each operator
//    over a set of bit patterns in three forms (runtime operands, runtime
//    operand with a literal, fully constant operands), against a C++ reference.
//  - hand-written tests for precedence, tokenization, compound assignment,
//    compile-time evaluation, operator overloads, diagnostics and algorithms.
//
// Every runtime case is run with and without bytecode optimization.

#include <string>
#include <type_traits>

struct BitwiseCase {
	std::string name;
	i64 expected;
	std::string body;
};

// Compiles `source` and calls every case (a zero-argument function returning
// i64), once per optimization mode.
static bool runBitwiseCases(const char* test_name, const std::string& source, const std::vector<BitwiseCase>& cases) {
	for (bool optimize : {false, true}) {
		TestContext context;
		ex_module* module = ex_module_create(&context.host);
		EXPECT_TRUE(module != nullptr);
		const bool compiled = ex_module_compile(module, toLs(source.c_str()), makeStringView(test_name), nullptr, nullptr);
		if (!compiled) printf("TEST FAILED in %s: source did not compile\n", test_name);
		EXPECT_TRUE(compiled);
		{
			RuntimeGuard runtime(module, &context.host, optimize);
			EXPECT_TRUE(runtime);
			for (const BitwiseCase& c : cases) {
				const ex_call_result result = test_call(runtime.runtime, toLs(c.name.c_str()));
				if (result != EX_CALL_RESULT_OK) {
					printf("TEST FAILED in %s (optimize=%d): %s did not run: %s\n", test_name, (int)optimize, c.name.c_str(), c.body.c_str());
					return false;
				}
				const i64 actual = ex_task_to_i64(runtime.task, -1);
				if (actual != c.expected) {
					printf("TEST FAILED in %s (optimize=%d): %s expected %lld, got %lld\n    %s\n",
						test_name, (int)optimize, c.name.c_str(), (long long)c.expected, (long long)actual, c.body.c_str());
					return false;
				}
			}
		}
		ex_module_destroy(module);
	}
	return true;
}

// ---------------------------------------------------------------------------
// Generated matrix
// ---------------------------------------------------------------------------

struct BitwiseGenerator {
	std::string source;
	std::vector<BitwiseCase> cases;

	void add(const std::string& body, i64 expected) {
		std::string name = "t" + std::to_string(cases.size());
		source += "fn " + name + "() : i64 {\n\t" + body + "\n}\n";
		cases.push_back({ name, expected, body });
	}
};

template <typename T>
static std::string bitwiseLiteral(T value) {
	char buffer[40];
	if constexpr (std::is_signed_v<T>) snprintf(buffer, sizeof(buffer), "(%lld)", (long long)value);
	else snprintf(buffer, sizeof(buffer), "(0x%llX)", (unsigned long long)value);
	return buffer;
}

template <typename T>
static T bitwiseReferenceBinary(char op, T a, T b) {
	using U = std::make_unsigned_t<T>;
	const U x = (U)a;
	const U y = (U)b;
	switch (op) {
		case '&': return (T)(x & y);
		case '|': return (T)(x | y);
		default: return (T)(x ^ y);
	}
}

// Shift semantics from the language reference: counts >= bit width are
// defined, `<<` and logical `>>` give 0, arithmetic `>>` gives 0 or -1.
template <typename T>
static T bitwiseReferenceShift(bool left, T a, u32 n) {
	using U = std::make_unsigned_t<T>;
	constexpr u32 bits = sizeof(T) * 8;
	if (left) return n >= bits ? (T)0 : (T)(U)((U)a << n);
	if constexpr (std::is_signed_v<T>) {
		if (n >= bits) return a < 0 ? (T)-1 : (T)0;
		return (T)(a >> n);
	}
	else {
		return n >= bits ? (T)0 : (T)((U)a >> n);
	}
}

template <typename T>
static void generateBitwiseMatrix(BitwiseGenerator& g, const char* type_name) {
	using U = std::make_unsigned_t<T>;
	constexpr u32 bits = sizeof(T) * 8;
	const std::string ty = type_name;

	const U max_signed = (U)((U)(~(U)0) >> 1);
	const T values[] = {
		(T)1,
		(T)(U)0x5555555555555555ull,
		(T)(U)0xAAAAAAAAAAAAAAAAull,
		(T)(U)(~(U)0),
		(T)max_signed,
	};
	const u32 value_count = (u32)(sizeof(values) / sizeof(values[0]));

	const char ops[] = { '&', '|', '^' };
	for (char op : ops) {
		const std::string op_str(1, op);
		for (u32 i = 0; i < value_count; ++i) {
			for (u32 j = i; j < value_count; ++j) {
				const T a = values[i];
				const T b = values[j];
				const i64 expected = (i64)bitwiseReferenceBinary(op, a, b);
				const std::string A = bitwiseLiteral(a);
				const std::string B = bitwiseLiteral(b);
				g.add("var a : " + ty + " = " + A + "; var b : " + ty + " = " + B + "; return (a " + op_str + " b) as i64;", expected);
				g.add("var a : " + ty + " = " + A + "; return (a " + op_str + " " + B + ") as i64;", expected);
				g.add("return ((" + A + " as " + ty + ") " + op_str + " (" + B + " as " + ty + ")) as i64;", expected);
			}
		}
	}

	// ~ on zero and every pattern
	const T not_values[] = { (T)0, values[0], values[1], values[2], values[3], values[4] };
	for (T a : not_values) {
		const i64 expected = (i64)(T)~(U)a;
		const std::string A = bitwiseLiteral(a);
		g.add("var a : " + ty + " = " + A + "; return (~a) as i64;", expected);
		g.add("return (~(" + A + " as " + ty + ")) as i64;", expected);
		g.add("var a : " + ty + " = " + A + "; return (~~a) as i64;", (i64)a);
	}

	// shifts, with in-range, boundary and over-wide counts
	const u32 counts[] = { 0, 1, bits / 2, bits - 1, bits, bits + 1, bits * 2 };
	for (u32 left = 0; left < 2; ++left) {
		const char* op = left ? "<<" : ">>";
		for (T a : values) {
			const std::string A = bitwiseLiteral(a);
			for (u32 n : counts) {
				const i64 expected = (i64)bitwiseReferenceShift<T>(left != 0, a, n);
				const std::string N = std::to_string(n);
				g.add("var a : " + ty + " = " + A + "; var n : u32 = " + N + "; return (a " + op + " n) as i64;", expected);
				g.add("var a : " + ty + " = " + A + "; var n : u8 = " + N + "; return (a " + op + " n) as i64;", expected);
				g.add("var a : " + ty + " = " + A + "; var n : i64 = " + N + "; return (a " + op + " n) as i64;", expected);
				g.add("var a : " + ty + " = " + A + "; return (a " + op + " " + N + ") as i64;", expected);
				g.add("return ((" + A + " as " + ty + ") " + op + " " + N + ") as i64;", expected);
			}
		}
	}

	// compound assignment against the same reference
	for (u32 i = 0; i < value_count; ++i) {
		for (u32 j = 0; j < value_count; ++j) {
			const T a = values[i];
			const T b = values[j];
			const std::string A = bitwiseLiteral(a);
			const std::string B = bitwiseLiteral(b);
			g.add("var a : " + ty + " = " + A + "; a &= " + B + "; return a as i64;", (i64)bitwiseReferenceBinary('&', a, b));
			g.add("var a : " + ty + " = " + A + "; a |= " + B + "; return a as i64;", (i64)bitwiseReferenceBinary('|', a, b));
			g.add("var a : " + ty + " = " + A + "; a ^= " + B + "; return a as i64;", (i64)bitwiseReferenceBinary('^', a, b));
		}
	}
	for (T a : values) {
		const std::string A = bitwiseLiteral(a);
		for (u32 n : counts) {
			const std::string N = std::to_string(n);
			g.add("var a : " + ty + " = " + A + "; a <<= " + N + "; return a as i64;", (i64)bitwiseReferenceShift<T>(true, a, n));
			g.add("var a : " + ty + " = " + A + "; a >>= " + N + "; return a as i64;", (i64)bitwiseReferenceShift<T>(false, a, n));
			g.add("var a : " + ty + " = " + A + "; var n : u32 = " + N + "; a <<= n; return a as i64;", (i64)bitwiseReferenceShift<T>(true, a, n));
			g.add("var a : " + ty + " = " + A + "; var n : u32 = " + N + "; a >>= n; return a as i64;", (i64)bitwiseReferenceShift<T>(false, a, n));
		}
	}
}

#define BITWISE_MATRIX_TEST(suffix, type) \
	TEST(BitwiseMatrix##suffix) { \
		BitwiseGenerator generator; \
		generateBitwiseMatrix<type>(generator, #suffix); \
		return runBitwiseCases(__func__, generator.source, generator.cases); \
	}

BITWISE_MATRIX_TEST(i8, i8)
BITWISE_MATRIX_TEST(u8, u8)
BITWISE_MATRIX_TEST(i16, i16)
BITWISE_MATRIX_TEST(u16, u16)
BITWISE_MATRIX_TEST(i32, i32)
BITWISE_MATRIX_TEST(u32, u32)
BITWISE_MATRIX_TEST(i64, i64)
BITWISE_MATRIX_TEST(u64, u64)
#undef BITWISE_MATRIX_TEST

// ---------------------------------------------------------------------------
// Precedence and tokenization
// ---------------------------------------------------------------------------

TEST(BitwisePrecedenceAgainstComparison) {
	// unlike C, bitwise operators bind tighter than comparisons
	const char* source = R"(
		fn and_before_eq() : i64 {
			var f : u32 = 0xF0;
			var m : u32 = 0x0F;
			if f & m == 0 { return 1; }
			return 0;
		}
		fn or_before_eq() : i64 {
			var a : u32 = 1;
			var b : u32 = 2;
			if a | b == 3 { return 1; }
			return 0;
		}
		fn xor_before_ne() : i64 {
			var a : u32 = 6;
			var b : u32 = 3;
			if a ^ b != 5 { return 0; }
			return 1;
		}
		fn shr_before_eq() : i64 {
			var a : u32 = 5;
			if a >> 1 == 2 { return 1; }
			return 0;
		}
		fn shl_before_lt() : i64 {
			var a : u32 = 1;
			if a << 3 < 9 { return 1; }
			return 0;
		}
		fn not_over_whole_comparison() : i64 {
			var a : u32 = 1;
			var b : u32 = 2;
			// not (a & b == 0): a & b is 0, so the comparison is true, so not gives false
			if not a & b == 0 { return 1; }
			return 0;
		}
		fn with_and_or_keywords() : i64 {
			var a : u32 = 5;
			var b : u32 = 4;
			if a & 1 == 1 and b & 1 == 0 { return 1; }
			return 0;
		}
		fn with_or_keyword() : i64 {
			var a : u32 = 2;
			if a & 1 == 1 or a & 2 == 2 { return 1; }
			return 0;
		}
		fn in_ternary() : i64 {
			var a : u32 = 5;
			return a & 1 == 1 ? 10 : 20;
		}
	)";
	return runBitwiseCases(__func__, source, {
		{ "and_before_eq", 1 },
		{ "or_before_eq", 1 },
		{ "xor_before_ne", 1 },
		{ "shr_before_eq", 1 },
		{ "shl_before_lt", 1 },
		{ "not_over_whole_comparison", 0 },
		{ "with_and_or_keywords", 1 },
		{ "with_or_keyword", 1 },
		{ "in_ternary", 10 },
	});
}

TEST(BitwisePrecedenceAmongThemselves) {
	// loosest to tightest: | ^ & << >> then + - then * / %
	const char* source = R"(
		fn and_over_or() : i64 {
			var a : u32 = 8; var b : u32 = 6; var c : u32 = 3;
			return (a | b & c) as i64;    // 8 | (6 & 3) = 10, not (8 | 6) & 3 = 2
		}
		fn and_over_xor() : i64 {
			var a : u32 = 12; var b : u32 = 10; var c : u32 = 6;
			return (a & b ^ c) as i64;    // (12 & 10) ^ 6 = 14, not 12 & (10 ^ 6) = 12
		}
		fn xor_over_or() : i64 {
			var a : u32 = 7; var b : u32 = 1; var c : u32 = 2;
			return (a ^ b | c) as i64;    // (7 ^ 1) | 2 = 6, not 7 ^ (1 | 2) = 4
		}
		fn or_chain_left_to_right() : i64 {
			var a : u32 = 1; var b : u32 = 2; var c : u32 = 4;
			return (a | b | c) as i64;
		}
		fn shift_over_and() : i64 {
			var a : u32 = 3; var b : u32 = 4;
			return (a << 1 & b) as i64;   // (3 << 1) & 4 = 4, not 3 << (1 & 4) = 3
		}
		fn shift_over_or() : i64 {
			var a : u32 = 1; var b : u32 = 1;
			return (a << 4 | b) as i64;   // 17
		}
		fn shift_over_xor() : i64 {
			var a : u32 = 3;
			return (a << 1 ^ 1) as i64;   // 7
		}
		fn add_over_shift() : i64 {
			var a : u32 = 1; var b : u32 = 2;
			return (a + b << 1) as i64;   // (1 + 2) << 1 = 6, not 1 + (2 << 1) = 5
		}
		fn shift_rhs_add() : i64 {
			var a : u32 = 1;
			return (a << 1 + 2) as i64;   // 1 << 3 = 8, not (1 << 1) + 2 = 4
		}
		fn mul_over_shift() : i64 {
			var a : u32 = 3;
			return (a << 2 * 2) as i64;   // 3 << 4 = 48, not (3 << 2) * 2 = 24
		}
		fn sub_over_shift() : i64 {
			var a : u32 = 16;
			return (a >> 3 - 1) as i64;   // 16 >> 2 = 4, not (16 >> 3) - 1 = 1
		}
		fn shift_left_assoc() : i64 {
			var a : u32 = 1;
			return (a << 1 << 2) as i64;  // 8
		}
		fn shift_right_assoc() : i64 {
			var a : u32 = 100;
			return (a >> 2 >> 1) as i64;  // 12
		}
		fn mixed_shift_assoc() : i64 {
			var a : u32 = 3;
			return (a << 4 >> 2) as i64;  // 12
		}
		fn parens_override() : i64 {
			var a : u32 = 8; var b : u32 = 6; var c : u32 = 3;
			return ((a | b) & c) as i64;  // 2
		}
		fn not_tight_and() : i64 {
			var a : u8 = 15; var b : u8 = 60;
			return (~a & b) as i64;       // (~15) & 60 = 48, not ~(15 & 60) = 243
		}
		fn not_binds_before_cast() : i64 {
			var x : u8 = 1;
			return (~x as u32) as i64;    // (~x) as u32 = 254, not ~(x as u32)
		}
		fn not_and_neg() : i64 {
			var x : i32 = 5;
			return (~-x) as i64;          // ~(-5) = 4
		}
		fn neg_and_not() : i64 {
			var x : i32 = 5;
			return (-~x) as i64;          // -(~5) = 6
		}
		fn not_not() : i64 {
			var x : i32 = 5;
			return (~~x) as i64;
		}
		fn not_shift() : i64 {
			var x : u8 = 1;
			return (~x << 1) as i64;      // (~1) << 1 = 0xFE << 1 = 0xFC
		}
	)";
	return runBitwiseCases(__func__, source, {
		{ "and_over_or", 10 },
		{ "and_over_xor", 14 },
		{ "xor_over_or", 6 },
		{ "or_chain_left_to_right", 7 },
		{ "shift_over_and", 4 },
		{ "shift_over_or", 17 },
		{ "shift_over_xor", 7 },
		{ "add_over_shift", 6 },
		{ "shift_rhs_add", 8 },
		{ "mul_over_shift", 48 },
		{ "sub_over_shift", 4 },
		{ "shift_left_assoc", 8 },
		{ "shift_right_assoc", 12 },
		{ "mixed_shift_assoc", 12 },
		{ "parens_override", 2 },
		{ "not_tight_and", 48 },
		{ "not_binds_before_cast", 254 },
		{ "not_and_neg", 4 },
		{ "neg_and_not", 6 },
		{ "not_not", 5 },
		{ "not_shift", 252 },
	});
}

TEST(BitwiseTokenization) {
	const char* source = R"(
		fn no_spaces() : i64 {
			var a : u32 = 6;
			var b : u32 = 3;
			// (a << 1) | (((b >> 1) & 7) ^ 1) = 12 | 0
			return (a<<1|b>>1&7^1) as i64;
		}
		fn and_then_not() : i64 {
			// there is no `&~` operator: this is a & (~b)
			var m : u32 = 0xFF;
			var b : u32 = 0x0F;
			return (m &~ b) as i64;
		}
		fn shift_assign_no_spaces() : i64 {
			var x : u32 = 64;
			x>>=2;
			x<<=1;
			return x as i64;
		}
		fn comparisons_unaffected() : i64 {
			var a : u32 = 5;
			var b : u32 = 5;
			var n : i64 = 0;
			if a>=b { n += 1; }
			if a<=b { n += 10; }
			if a<b { n += 100; }
			if a>b { n += 1000; }
			return n;
		}
		fn shift_then_compare() : i64 {
			var a : u32 = 1;
			var n : i64 = 0;
			if a<<1>=2 { n += 1; }
			if a<<1<=1 { n += 10; }
			return n;
		}
		fn not_adjacent_to_paren() : i64 {
			var a : u8 = 0x0F;
			return (~(a)) as i64;
		}
		fn address_of_and_bitand() : i64 {
			var a : u32 = 12;
			var b : u32 = 10;
			var p = &a;
			return (p.* & b) as i64;
		}
	)";
	return runBitwiseCases(__func__, source, {
		{ "no_spaces", 12 },
		{ "and_then_not", 0xF0 },
		{ "shift_assign_no_spaces", 32 },
		{ "comparisons_unaffected", 11 },
		{ "shift_then_compare", 1 },
		{ "not_adjacent_to_paren", 0xF0 },
		{ "address_of_and_bitand", 8 },
	});
}

// ---------------------------------------------------------------------------
// Shift semantics
// ---------------------------------------------------------------------------

TEST(BitwiseShiftSemantics) {
	const char* source = R"(
		fn shl_basic() : i64 {
			var x : u32 = 3;
			return (x << 4) as i64;
		}
		fn shl_discards_high_bits_u8() : i64 {
			var x : u8 = 0x81;
			return (x << 1) as i64;       // 0x02
		}
		fn shl_into_sign_bit_i32() : i64 {
			var x : i32 = 1;
			return (x << 31) as i64;      // wraps to i32 min
		}
		fn shl_into_sign_bit_i8() : i64 {
			var x : i8 = 1;
			return (x << 7) as i64;       // -128
		}
		fn shr_logical_u32() : i64 {
			var x : u32 = 0xFFFFFFF8;
			return (x >> 1) as i64;       // 0x7FFFFFFC, zero filled
		}
		fn shr_arithmetic_i32() : i64 {
			var x : i32 = -8;
			return (x >> 1) as i64;       // -4, sign filled
		}
		fn shr_arithmetic_i8() : i64 {
			var x : i8 = -128 as i8;
			return (x >> 7) as i64;       // -1
		}
		fn shr_arithmetic_i64() : i64 {
			var x : i64 = 1;
			x <<= 63;
			return x >> 63;               // -1
		}
		fn shr_logical_u64() : i64 {
			var x : u64 = 1;
			x <<= 63;
			return (x >> 63) as i64;      // 1
		}
		fn shr_positive_signed() : i64 {
			var x : i32 = 100;
			return (x >> 2) as i64;       // 25
		}
		fn shift_by_zero() : i64 {
			var x : u32 = 0xDEADBEEF;
			var n : u32 = 0;
			return ((x << n) >> n) as i64;
		}
		fn shl_overwide_u8() : i64 {
			var x : u8 = 255;
			var n : u32 = 8;
			return (x << n) as i64;       // 0, not 255
		}
		fn shl_overwide_u32_exactly_width() : i64 {
			var x : u32 = 1;
			var n : u32 = 32;
			return (x << n) as i64;       // 0; x86 would return 1
		}
		fn shl_overwide_u64_exactly_width() : i64 {
			var x : u64 = 1;
			var n : u32 = 64;
			return (x << n) as i64;       // 0
		}
		fn shl_overwide_far() : i64 {
			var x : u32 = 1;
			var n : u32 = 1000;
			return (x << n) as i64;
		}
		fn shr_overwide_unsigned() : i64 {
			var x : u16 = 0xFFFF;
			var n : u32 = 16;
			return (x >> n) as i64;       // 0
		}
		fn shr_overwide_signed_negative() : i64 {
			var x : i8 = -100;
			var n : u32 = 9;
			return (x >> n) as i64;       // -1
		}
		fn shr_overwide_signed_positive() : i64 {
			var x : i8 = 100;
			var n : u32 = 200;
			return (x >> n) as i64;       // 0
		}
		fn shr_overwide_i64_negative() : i64 {
			var x : i64 = -5;
			var n : u32 = 64;
			return x >> n;                // -1
		}
		fn count_wider_than_value() : i64 {
			var v : u8 = 1;
			var n : u64 = 4294967297;     // 2^32 + 1, must not wrap to 1
			return (v << n) as i64;
		}
		fn count_u8_on_u64() : i64 {
			var v : u64 = 1;
			var n : u8 = 40;
			return (v << n) as i64;       // 1 << 40
		}
		fn count_i64_on_u8() : i64 {
			var v : u8 = 1;
			var n : i64 = 7;
			return (v << n) as i64;       // 128
		}
		fn count_i8_on_i32() : i64 {
			var v : i32 = 1;
			var n : i8 = 3;
			return (v << n) as i64;       // 8
		}
		fn count_negative_runtime_shl() : i64 {
			var v : i32 = 1;
			var n : i32 = -1;
			return (v << n) as i64;       // negative count is over-wide, not a right shift
		}
		fn count_negative_runtime_shr() : i64 {
			var v : i32 = -8;
			var n : i32 = -1;
			return (v >> n) as i64;       // arithmetic over-wide: -1
		}
		fn count_expression() : i64 {
			var v : u32 = 1;
			var n : u32 = 3;
			return (v << n + 1) as i64;   // 16
		}
		fn count_from_call() : i64 {
			var v : u32 = 1;
			return (v << four()) as i64;
		}
		fn four() : u32 { return 4; }
	)";
	return runBitwiseCases(__func__, source, {
		{ "shl_basic", 48 },
		{ "shl_discards_high_bits_u8", 2 },
		{ "shl_into_sign_bit_i32", -2147483648ll },
		{ "shl_into_sign_bit_i8", -128 },
		{ "shr_logical_u32", 0x7FFFFFFCll },
		{ "shr_arithmetic_i32", -4 },
		{ "shr_arithmetic_i8", -1 },
		{ "shr_arithmetic_i64", -1 },
		{ "shr_logical_u64", 1 },
		{ "shr_positive_signed", 25 },
		{ "shift_by_zero", 0xDEADBEEFll },
		{ "shl_overwide_u8", 0 },
		{ "shl_overwide_u32_exactly_width", 0 },
		{ "shl_overwide_u64_exactly_width", 0 },
		{ "shl_overwide_far", 0 },
		{ "shr_overwide_unsigned", 0 },
		{ "shr_overwide_signed_negative", -1 },
		{ "shr_overwide_signed_positive", 0 },
		{ "shr_overwide_i64_negative", -1 },
		{ "count_wider_than_value", 0 },
		{ "count_u8_on_u64", 1ll << 40 },
		{ "count_i64_on_u8", 128 },
		{ "count_i8_on_i32", 8 },
		{ "count_negative_runtime_shl", 0 },
		{ "count_negative_runtime_shr", -1 },
		{ "count_expression", 16 },
		{ "count_from_call", 16 },
	});
}

// ---------------------------------------------------------------------------
// Compound assignment
// ---------------------------------------------------------------------------

TEST(BitwiseCompoundAssignment) {
	const char* source = R"(
		struct Fields { a : u32; b : u32; }

		fn c_and() : i64 { var x : u8 = 0xFF; x &= 0x0F; return x as i64; }
		fn c_or() : i64 { var x : u8 = 0x0A; x |= 0xA0; return x as i64; }
		fn c_xor() : i64 { var x : u8 = 0xFF; x ^= 0x0F; return x as i64; }
		fn c_shl() : i64 { var x : u8 = 0x81; x <<= 1; return x as i64; }
		fn c_shr_signed() : i64 { var x : i8 = -16; x >>= 2; return x as i64; }
		fn c_shr_unsigned() : i64 { var x : u8 = 0xF0; x >>= 2; return x as i64; }
		fn c_shl_var_count() : i64 { var x : u32 = 1; var n : u8 = 10; x <<= n; return x as i64; }
		fn c_shr_overwide() : i64 { var x : u32 = 0xFFFFFFFF; x >>= 32; return x as i64; }

		fn c_chain() : i64 {
			var x : u8 = 0xFF;
			x &= 0x0F;   // 0x0F
			x |= 0xA0;   // 0xAF
			x ^= 0xFF;   // 0x50
			x <<= 1;     // 0xA0
			x >>= 4;     // 0x0A
			return x as i64;
		}

		fn c_rhs_expression() : i64 {
			var x : u32 = 0xFF;
			var y : u32 = 3;
			x &= y << 2 | 1;   // 0xFF & 13
			return x as i64;
		}

		fn c_field() : i64 {
			var s : Fields = Fields { 0xF0, 1 };
			s.a &= 0x30;
			s.b <<= 4;
			return (s.a + s.b) as i64;   // 48 + 16
		}

		fn c_array() : i64 {
			var arr : [3]u8 = [1, 2, 4];
			arr[1] ^= 0xFF;
			arr[2] <<= 1;
			return arr[0] as i64 + arr[1] as i64 + arr[2] as i64;   // 1 + 253 + 8
		}

		fn c_pointer() : i64 {
			var x : u32 = 3;
			var p = &x;
			p.* |= 8;
			p.* <<= 1;
			return x as i64;   // (3 | 8) << 1
		}

		fn c_slice() : i64 {
			var arr : [2]u32 = [1, 2];
			var s : []u32 = arr[:];
			s[1] |= 0x10;
			return arr[1] as i64;
		}

		fn c_toggle_flag() : i64 {
			var flags : u32 = 0;
			var bit3 : u32 = 1 << 3;
			flags |= bit3;
			flags |= 1 << 5;
			flags &= ~bit3;
			flags ^= 1 << 0;
			return flags as i64;   // 32 | 1
		}
	)";
	return runBitwiseCases(__func__, source, {
		{ "c_and", 15 },
		{ "c_or", 170 },
		{ "c_xor", 240 },
		{ "c_shl", 2 },
		{ "c_shr_signed", -4 },
		{ "c_shr_unsigned", 60 },
		{ "c_shl_var_count", 1024 },
		{ "c_shr_overwide", 0 },
		{ "c_chain", 10 },
		{ "c_rhs_expression", 13 },
		{ "c_field", 64 },
		{ "c_array", 262 },
		{ "c_pointer", 22 },
		{ "c_slice", 18 },
		{ "c_toggle_flag", 33 },
	});
}

// ---------------------------------------------------------------------------
// Untyped constants and compile-time evaluation
// ---------------------------------------------------------------------------

TEST(BitwiseComptimeAndUntypedConstants) {
	const char* source = R"(
		comptime Mask = (1 << 8) - 1;
		comptime Both = 0xF0 | 0x0F;
		comptime Xored = 0xFF ^ 0x0F;
		comptime Anded = 0xFF & 0x3C;
		comptime Inverted = ~0;
		comptime Shifted = 256 >> 4;
		comptime Mixed = (1 << 4 | 1 << 1) & ~(1 << 1);
		comptime Big = 1 << 40;
		comptime TypedWide : u32 = (1 as u32) << 32;
		comptime TypedTruncated : u8 = (0xFF as u8) << 4;
		comptime TypedNot : u8 = ~(0 as u8);
		comptime TypedSar : i32 = (-16 as i32) >> 2;
		comptime TypedShr : u32 = (0xFFFFFFFF as u32) >> 28;
		comptime Count = 4;
		comptime Slots = 1 << Count;

		fn k_mask() : i64 { return Mask; }
		fn k_both() : i64 { return Both; }
		fn k_xor() : i64 { return Xored; }
		fn k_and() : i64 { return Anded; }
		fn k_inverted() : i64 { return Inverted; }
		fn k_shifted() : i64 { return Shifted; }
		fn k_mixed() : i64 { return Mixed; }
		fn k_big() : i64 { return Big; }
		fn k_typed_wide() : i64 { return TypedWide as i64; }
		fn k_typed_truncated() : i64 { return TypedTruncated as i64; }
		fn k_typed_not() : i64 { return TypedNot as i64; }
		fn k_typed_sar() : i64 { return TypedSar as i64; }
		fn k_typed_shr() : i64 { return TypedShr as i64; }
		fn k_array_size() : i64 { return sizeof([1 << 3]u32); }
		fn k_array_size_from_comptime() : i64 { return sizeof([1 << Count]u8); }
		fn k_slots() : i64 { return Slots; }

		// untyped operands adopt the other operand's type
		fn untyped_adopts_u8() : i64 {
			var x : u8 = 0xFF;
			return (x & 0x0F) as i64;
		}
		fn untyped_adopts_on_left() : i64 {
			var x : u8 = 0xFF;
			return (0xF0 ^ x) as i64;
		}
		fn untyped_shift_lhs_takes_declared_type() : i64 {
			var r : u8 = 1 << 7;
			return r as i64;
		}
		fn untyped_not_cast_to_unsigned() : i64 {
			// ~ of an untyped constant is a negative untyped constant (-0xF1);
			// the explicit cast truncates it like any other out-of-range cast
			var r : u8 = ~0xF0 as u8;
			return r as i64;
		}
		fn untyped_not_signed() : i64 {
			var r : i32 = ~0;
			return r as i64;
		}
		fn untyped_shift_as_u64() : i64 {
			var r : u64 = 1 << 40;
			return r as i64;
		}
	)";
	return runBitwiseCases(__func__, source, {
		{ "k_mask", 255 },
		{ "k_both", 255 },
		{ "k_xor", 240 },
		{ "k_and", 0x3C },
		{ "k_inverted", -1 },
		{ "k_shifted", 16 },
		{ "k_mixed", 16 },
		{ "k_big", 1ll << 40 },
		{ "k_typed_wide", 0 },
		{ "k_typed_truncated", 0xF0 },
		{ "k_typed_not", 255 },
		{ "k_typed_sar", -4 },
		{ "k_typed_shr", 15 },
		{ "k_array_size", 32 },
		{ "k_array_size_from_comptime", 16 },
		{ "k_slots", 16 },
		{ "untyped_adopts_u8", 0x0F },
		{ "untyped_adopts_on_left", 0x0F },
		{ "untyped_shift_lhs_takes_declared_type", 128 },
		{ "untyped_not_cast_to_unsigned", 0x0F },
		{ "untyped_not_signed", -1 },
		{ "untyped_shift_as_u64", 1ll << 40 },
	});
}

// ---------------------------------------------------------------------------
// Operator overloads
// ---------------------------------------------------------------------------

TEST(BitwiseOperatorOverloads) {
	const char* source = R"(
		struct Flags { bits : u32; }

		operator |(a : Flags, b : Flags) : Flags { return Flags { a.bits | b.bits }; }
		operator &(a : Flags, b : Flags) : Flags { return Flags { a.bits & b.bits }; }
		operator ^(a : Flags, b : Flags) : Flags { return Flags { a.bits ^ b.bits }; }
		operator ~(a : Flags) : Flags { return Flags { ~a.bits }; }
		operator <<(a : Flags, n : u32) : Flags { return Flags { a.bits << n }; }
		operator >>(a : Flags, n : u32) : Flags { return Flags { a.bits >> n }; }

		fn f_or() : i64 {
			var a : Flags = Flags { 1 };
			var b : Flags = Flags { 4 };
			return (a | b).bits as i64;
		}
		fn f_and() : i64 {
			var a : Flags = Flags { 7 };
			var b : Flags = Flags { 12 };
			return (a & b).bits as i64;
		}
		fn f_xor() : i64 {
			var a : Flags = Flags { 7 };
			var b : Flags = Flags { 12 };
			return (a ^ b).bits as i64;
		}
		fn f_not() : i64 {
			var a : Flags = Flags { 0xFFFFFFF0 };
			return (~a).bits as i64;
		}
		fn f_shl() : i64 {
			var a : Flags = Flags { 3 };
			return (a << 2).bits as i64;
		}
		fn f_shr() : i64 {
			var a : Flags = Flags { 48 };
			return (a >> 4).bits as i64;
		}
		fn f_precedence() : i64 {
			var a : Flags = Flags { 8 };
			var b : Flags = Flags { 6 };
			var c : Flags = Flags { 3 };
			return (a | b & c).bits as i64;   // same grouping as for integers
		}
		fn f_compound() : i64 {
			var f : Flags = Flags { 1 };
			var g : Flags = Flags { 4 };
			f |= g;                 // 5
			f <<= 1;                // 10
			f ^= Flags { 0xF };     // 5
			f &= ~Flags { 1 };      // 4
			f >>= 1;                // 2
			return f.bits as i64;
		}
		fn f_clear_with_not() : i64 {
			var f : Flags = Flags { 0xFF };
			var m : Flags = Flags { 0x0F };
			return (f & ~m).bits as i64;
		}
	)";
	return runBitwiseCases(__func__, source, {
		{ "f_or", 5 },
		{ "f_and", 4 },
		{ "f_xor", 11 },
		{ "f_not", 0x0F },
		{ "f_shl", 12 },
		{ "f_shr", 3 },
		{ "f_precedence", 10 },
		{ "f_compound", 2 },
		{ "f_clear_with_not", 0xF0 },
	});
}

TEST(BitwiseOperatorOverloadResolution) {
	// overloads are selected by operand types, as for the arithmetic operators
	const char* source = R"(
		struct A { v : i32; }
		struct B { v : i32; }

		operator &(a : A, b : A) : i32 { return 1; }
		operator &(a : A, b : B) : i32 { return 2; }
		operator &(a : B, b : A) : i32 { return 3; }
		operator <<(a : A, n : i32) : i32 { return 10; }
		operator <<(a : A, n : u8) : i32 { return 20; }

		fn aa() : i64 { var a : A = A { 0 }; var b : A = A { 0 }; return (a & b) as i64; }
		fn ab() : i64 { var a : A = A { 0 }; var b : B = B { 0 }; return (a & b) as i64; }
		fn ba() : i64 { var a : A = A { 0 }; var b : B = B { 0 }; return (b & a) as i64; }
		fn shift_i32() : i64 { var a : A = A { 0 }; var n : i32 = 1; return (a << n) as i64; }
		fn shift_u8() : i64 { var a : A = A { 0 }; var n : u8 = 1; return (a << n) as i64; }
	)";
	return runBitwiseCases(__func__, source, {
		{ "aa", 1 },
		{ "ab", 2 },
		{ "ba", 3 },
		{ "shift_i32", 10 },
		{ "shift_u8", 20 },
	});
}

// ---------------------------------------------------------------------------
// Algorithms
// ---------------------------------------------------------------------------

TEST(BitwiseAlgorithms) {
	// reference values computed in C++
	u32 fnv = 2166136261u;
	for (u8 byte : {(u8)97, (u8)98, (u8)99}) {
		fnv ^= byte;
		fnv *= 16777619u;
	}
	u64 xorshift = 88172645463325252ull;
	for (int i = 0; i < 5; ++i) {
		xorshift ^= xorshift << 13;
		xorshift ^= xorshift >> 7;
		xorshift ^= xorshift << 17;
	}

	const char* source = R"(
		fn popcount(v : u32) : i32 {
			var x : u32 = v;
			var n : i32 = 0;
			while x != 0 {
				n += (x & 1) as i32;
				x >>= 1;
			}
			return n;
		}

		fn popcount_kernighan(v : u32) : i32 {
			var x : u32 = v;
			var n : i32 = 0;
			while x != 0 {
				x &= x - 1;
				n += 1;
			}
			return n;
		}

		fn rotl(x : u32, r : u32) : u32 {
			return (x << r) | (x >> (32 - r));
		}

		fn rotr(x : u32, r : u32) : u32 {
			return (x >> r) | (x << (32 - r));
		}

		fn pack(a : u8, b : u8, c : u8, d : u8) : u32 {
			return (a as u32) << 24 | (b as u32) << 16 | (c as u32) << 8 | d as u32;
		}

		fn byte_at(v : u32, index : u32) : u32 {
			return (v >> index * 8) & 0xFF;
		}

		fn reverse_bits_u8(v : u8) : u8 {
			var x : u8 = v;
			var r : u8 = 0;
			var i : i32 = 0;
			while i < 8 {
				r = (r << 1) | (x & 1);
				x >>= 1;
				i += 1;
			}
			return r;
		}

		fn sign_extend_8(v : u32) : i32 {
			return ((v << 24) as i32) >> 24;
		}

		fn is_power_of_two(x : u32) : bool {
			return x != 0 and x & (x - 1) == 0;
		}

		fn swap_xor() : i64 {
			var a : u32 = 0x1234;
			var b : u32 = 0xABCD;
			a ^= b;
			b ^= a;
			a ^= b;
			return a as i64 * 0x10000 + b as i64;
		}

		fn fnv1a() : i64 {
			var data : [3]u8 = [97, 98, 99];
			var hash : u32 = 2166136261;
			var i : i32 = 0;
			while i < 3 {
				hash ^= data[i] as u32;
				hash *= 16777619;
				i += 1;
			}
			return hash as i64;
		}

		fn xorshift64() : i64 {
			var x : u64 = 88172645463325252;
			var i : i32 = 0;
			while i < 5 {
				x ^= x << 13;
				x ^= x >> 7;
				x ^= x << 17;
				i += 1;
			}
			return x as i64;
		}

		fn t_popcount_mixed() : i64 { return popcount(0xF0F0F0F0) as i64; }
		fn t_popcount_zero() : i64 { return popcount(0) as i64; }
		fn t_popcount_all() : i64 { return popcount(0xFFFFFFFF) as i64; }
		fn t_popcount_ends() : i64 { return popcount(0x80000001) as i64; }
		fn t_kernighan() : i64 { return popcount_kernighan(0xB5) as i64; }
		fn t_kernighan_all() : i64 { return popcount_kernighan(0xFFFFFFFF) as i64; }
		fn t_rotl8() : i64 { return rotl(0x12345678, 8) as i64; }
		fn t_rotl_zero() : i64 { return rotl(0x12345678, 0) as i64; }
		fn t_rotl_width() : i64 { return rotl(0x12345678, 32) as i64; }
		fn t_rotl_wrap() : i64 { return rotl(0x80000001, 1) as i64; }
		fn t_rotr8() : i64 { return rotr(0x12345678, 8) as i64; }
		fn t_rotr_roundtrip() : i64 { return rotr(rotl(0xCAFEBABE, 13), 13) as i64; }
		fn t_pack() : i64 { return pack(1, 2, 3, 4) as i64; }
		fn t_pack_high() : i64 { return pack(0xFF, 0, 0, 0xFF) as i64; }
		fn t_byte_at_1() : i64 { return byte_at(0xAABBCCDD, 1) as i64; }
		fn t_byte_at_2() : i64 { return byte_at(0xAABBCCDD, 2) as i64; }
		fn t_byte_at_3() : i64 { return byte_at(0xAABBCCDD, 3) as i64; }
		fn t_reverse() : i64 { return reverse_bits_u8(0x16) as i64; }
		fn t_reverse_involution() : i64 { return reverse_bits_u8(reverse_bits_u8(0xA7)) as i64; }
		fn t_sign_extend_neg() : i64 { return sign_extend_8(0x80) as i64; }
		fn t_sign_extend_pos() : i64 { return sign_extend_8(0x7F) as i64; }
		fn t_sign_extend_ignores_high() : i64 { return sign_extend_8(0x1FF) as i64; }
		fn t_pow2_yes() : i64 { return is_power_of_two(64) ? 1 : 0; }
		fn t_pow2_no() : i64 { return is_power_of_two(65) ? 1 : 0; }
		fn t_pow2_zero() : i64 { return is_power_of_two(0) ? 1 : 0; }
		fn t_pow2_top() : i64 { return is_power_of_two(0x80000000) ? 1 : 0; }
	)";
	return runBitwiseCases(__func__, source, {
		{ "t_popcount_mixed", 16 },
		{ "t_popcount_zero", 0 },
		{ "t_popcount_all", 32 },
		{ "t_popcount_ends", 2 },
		{ "t_kernighan", 5 },
		{ "t_kernighan_all", 32 },
		{ "t_rotl8", 0x34567812ll },
		{ "t_rotl_zero", 0x12345678ll },
		{ "t_rotl_width", 0x12345678ll },
		{ "t_rotl_wrap", 3 },
		{ "t_rotr8", 0x78123456ll },
		{ "t_rotr_roundtrip", 0xCAFEBABEll },
		{ "t_pack", 0x01020304ll },
		{ "t_pack_high", 0xFF0000FFll },
		{ "t_byte_at_1", 0xCC },
		{ "t_byte_at_2", 0xBB },
		{ "t_byte_at_3", 0xAA },
		{ "t_reverse", 0x68 },
		{ "t_reverse_involution", 0xA7 },
		{ "t_sign_extend_neg", -128 },
		{ "t_sign_extend_pos", 127 },
		{ "t_sign_extend_ignores_high", -1 },
		{ "t_pow2_yes", 1 },
		{ "t_pow2_no", 0 },
		{ "t_pow2_zero", 0 },
		{ "t_pow2_top", 1 },
		{ "swap_xor", 0xABCDll * 0x10000 + 0x1234 },
		{ "fnv1a", (i64)fnv },
		{ "xorshift64", (i64)xorshift },
	});
}

// ---------------------------------------------------------------------------
// Diagnostics
// ---------------------------------------------------------------------------

TEST(BitwiseValidFormsCompile) {
	EXPECT_COMPILE(R"(
		fn main() : void {
			var a : i8 = 1;   var b : i8 = 2;
			var c : u8 = 1;   var d : u8 = 2;
			var e : i16 = 1;  var f : i16 = 2;
			var g : u16 = 1;  var h : u16 = 2;
			var i : i32 = 1;  var j : i32 = 2;
			var k : u32 = 1;  var l : u32 = 2;
			var m : i64 = 1;  var n : i64 = 2;
			var o : u64 = 1;  var p : u64 = 2;
			var r : isize = 1; var s : isize = 2;
			_ = a & b; _ = a | b; _ = a ^ b; _ = ~a; _ = a << b; _ = a >> b;
			_ = c & d; _ = c | d; _ = c ^ d; _ = ~c; _ = c << d; _ = c >> d;
			_ = e & f; _ = e | f; _ = e ^ f; _ = ~e; _ = e << f; _ = e >> f;
			_ = g & h; _ = g | h; _ = g ^ h; _ = ~g; _ = g << h; _ = g >> h;
			_ = i & j; _ = i | j; _ = i ^ j; _ = ~i; _ = i << j; _ = i >> j;
			_ = k & l; _ = k | l; _ = k ^ l; _ = ~k; _ = k << l; _ = k >> l;
			_ = m & n; _ = m | n; _ = m ^ n; _ = ~m; _ = m << n; _ = m >> n;
			_ = o & p; _ = o | p; _ = o ^ p; _ = ~o; _ = o << p; _ = o >> p;
			_ = r & s; _ = r | s; _ = r ^ s; _ = ~r; _ = r << s; _ = r >> s;
		}
	)");

	// shift count type is independent of the shifted value's type
	EXPECT_COMPILE(R"(
		fn main() : void {
			var v : u64 = 1;
			var n8 : u8 = 1;
			var n64 : i64 = 1;
			_ = v << n8;
			_ = v >> n64;
			var w : u8 = 1;
			var n32 : u32 = 1;
			_ = w << n32;
		}
	)");

	// the result of a shift has the type of the left operand
	EXPECT_COMPILE(R"(
		fn takes_u8(v : u8) : void {}
		fn main() : void {
			var v : u8 = 1;
			var n : u64 = 2;
			takes_u8(v << n);
		}
	)");
	return true;
}

TEST(BitwiseRejectsNonIntegerOperands) {
	EXPECT_COMPILE_FAIL(R"(
		fn main() : void { var a : bool = true; var b : bool = false; _ = a & b; }
	)");
	EXPECT_COMPILE_FAIL(R"(
		fn main() : void { var a : bool = true; var b : bool = false; _ = a | b; }
	)");
	EXPECT_COMPILE_FAIL(R"(
		fn main() : void { var a : bool = true; var b : bool = false; _ = a ^ b; }
	)");
	EXPECT_COMPILE_FAIL(R"(
		fn main() : void { var a : bool = true; _ = ~a; }
	)");
	EXPECT_COMPILE_FAIL(R"(
		fn main() : void { var a : bool = true; _ = a << 1; }
	)");
	EXPECT_COMPILE_FAIL(R"(
		fn main() : void { var a : f32 = 1.0; var b : f32 = 2.0; _ = a & b; }
	)");
	EXPECT_COMPILE_FAIL(R"(
		fn main() : void { var a : f64 = 1.0; var b : f64 = 2.0; _ = a | b; }
	)");
	EXPECT_COMPILE_FAIL(R"(
		fn main() : void { var a : f32 = 1.0; _ = ~a; }
	)");
	EXPECT_COMPILE_FAIL(R"(
		fn main() : void { var a : f64 = 1.0; _ = a << 1; }
	)");
	EXPECT_COMPILE_FAIL(R"(
		fn main() : void { var a : f32 = 1.0; _ = a >> 1; }
	)");
	EXPECT_COMPILE_FAIL(R"(
		fn main() : void { var a : u32 = 1; var n : f32 = 1.0; _ = a << n; }
	)");
	EXPECT_COMPILE_FAIL(R"(
		fn main() : void { var a : u32 = 1; _ = a << 1.5; }
	)");
	EXPECT_COMPILE_FAIL(R"(
		fn main() : void { var a : u32 = 1; _ = a & 1.5; }
	)");
	EXPECT_COMPILE_FAIL(R"(
		fn main() : void { _ = ~1.5; }
	)");
	EXPECT_COMPILE_FAIL(R"(
		fn main() : void { _ = 1.5 << 1; }
	)");
	EXPECT_COMPILE_FAIL(R"(
		fn main() : void { var a : u32 = 1; _ = a << true; }
	)");
	return true;
}

TEST(BitwiseRejectsMixedIntegerTypes) {
	EXPECT_COMPILE_FAIL(R"(
		fn main() : void { var a : i32 = 1; var b : i64 = 2; _ = a & b; }
	)");
	EXPECT_COMPILE_FAIL(R"(
		fn main() : void { var a : i32 = 1; var b : u32 = 2; _ = a | b; }
	)");
	EXPECT_COMPILE_FAIL(R"(
		fn main() : void { var a : u8 = 1; var b : u16 = 2; _ = a ^ b; }
	)");
	EXPECT_COMPILE_FAIL(R"(
		fn main() : void { var a : i32 = 1; var b : i64 = 2; a &= b; }
	)");
	EXPECT_COMPILE_FAIL(R"(
		fn main() : void { var a : u8 = 1; var b : u32 = 2; a |= b; }
	)");
	EXPECT_COMPILE_FAIL(R"(
		fn main() : void { var a : u8 = 1; _ = a & 256; }
	)");
	EXPECT_COMPILE_FAIL(R"(
		fn main() : void { var a : i8 = 1; _ = a | 200; }
	)");
	// the result is the left operand's type, so it cannot initialize a wider variable
	EXPECT_COMPILE_FAIL(R"(
		fn main() : void { var a : u8 = 1; var r : u32 = a << 1; }
	)");
	EXPECT_COMPILE_FAIL(R"(
		fn main() : void { var a : u8 = 1; var r : u32 = ~a; }
	)");
	return true;
}

TEST(BitwiseRejectsOtherTypes) {
	EXPECT_COMPILE_FAIL(R"(
		enum Mode { A, B }
		fn main() : void { _ = Mode.A & Mode.B; }
	)");
	EXPECT_COMPILE_FAIL(R"(
		enum Mode { A, B }
		fn main() : void { _ = Mode.A | Mode.B; }
	)");
	EXPECT_COMPILE_FAIL(R"(
		enum Mode { A, B }
		fn main() : void { _ = ~Mode.A; }
	)");
	EXPECT_COMPILE_FAIL(R"(
		enum Mode { A, B }
		fn main() : void { _ = Mode.A << 1; }
	)");
	EXPECT_COMPILE_FAIL(R"(
		struct S { v : i32; }
		fn main() : void { var a : S = S { 1 }; var b : S = S { 2 }; _ = a & b; }
	)");
	EXPECT_COMPILE_FAIL(R"(
		struct S { v : i32; }
		fn main() : void { var a : S = S { 1 }; _ = ~a; }
	)");
	EXPECT_COMPILE_FAIL(R"(
		struct S { v : i32; }
		fn main() : void { var a : S = S { 1 }; _ = a << 1; }
	)");
	EXPECT_COMPILE_FAIL(R"(
		fn main() : void { var a : i32 = 1; var p = &a; var q = &a; _ = p & q; }
	)");
	EXPECT_COMPILE_FAIL(R"(
		fn main() : void { var a : i32 = 1; var p = &a; _ = p << 1; }
	)");
	EXPECT_COMPILE_FAIL(R"(
		fn main() : void { var a : [2]i32 = [1, 2]; var s : []i32 = a[:]; _ = s & s; }
	)");
	EXPECT_COMPILE_FAIL(R"(
		fn main() : void { var a : byte = undefined; var b : byte = undefined; _ = a & b; }
	)");
	EXPECT_COMPILE_FAIL(R"(
		fn main() : void { var a : ?i32 = 1; _ = a & 1; }
	)");
	EXPECT_COMPILE_FAIL(R"(
		fn main() : void { var a : bool = true; a &= false; }
	)");
	EXPECT_COMPILE_FAIL(R"(
		fn main() : void { var a : f32 = 1.0; a <<= 1; }
	)");
	EXPECT_COMPILE_FAIL(R"(
		fn main() : void { var a : u32 = 1; a &= 1.0; }
	)");
	return true;
}

TEST(BitwiseUntypedNotIsAnUntypedConstant) {
	// `~` of an untyped constant is the untyped constant -a - 1. It does not take the
	// contextual type, so an inline `~0` behaves like a named `comptime A = ~0`.
	const char* source = R"(
		fn not_i32() : i64 { var r : i32 = ~0; return r as i64; }
		fn not_i64() : i64 { var r : i64 = ~0; return r; }
		fn not_i8_pattern() : i64 { var r : i8 = ~0x0F; return r as i64; }
		fn not_double() : i64 { var r : i32 = ~~5; return r as i64; }
		fn not_negative() : i64 { var r : u32 = ~-1; return r as i64; }
		fn not_cast_u8() : i64 { return (~0 as u8) as i64; }
		fn not_cast_u32() : i64 { return (~0 as u32) as i64; }
		fn not_typed_operand() : i64 { return (~(0 as u16)) as i64; }
		fn not_typed_in_mask() : i64 { var x : u32 = 0xFFFF; return (x & ~(0xFF as u32)) as i64; }
		fn not_signed_mask() : i64 { var x : i32 = 0xFFFF; return (x & ~0xFF) as i64; }
	)";
	if (!runBitwiseCases(__func__, source, {
		{ "not_i32", -1 },
		{ "not_i64", -1 },
		{ "not_i8_pattern", -16 },
		{ "not_double", 5 },
		{ "not_negative", 0 },
		{ "not_cast_u8", 255 },
		{ "not_cast_u32", 0xFFFFFFFFll },
		{ "not_typed_operand", 0xFFFF },
		{ "not_typed_in_mask", 0xFF00 },
		{ "not_signed_mask", 0xFF00 },
	})) return false;

	// -1 and friends do not fit an unsigned type, inline or through a name
	EXPECT_COMPILE_FAIL(R"(
		fn main() : void { var r : u8 = ~0; }
	)");
	EXPECT_COMPILE_FAIL(R"(
		fn main() : void { var x : u32 = 1; x &= ~1; }
	)");
	EXPECT_COMPILE_FAIL(R"(
		fn main() : void { var x : u32 = 1; _ = x & ~0xFF; }
	)");
	EXPECT_COMPILE_FAIL(R"(
		fn take(v : u8) : void {}
		fn main() : void { take(~1); }
	)");
	return true;
}

TEST(BitwiseRejectsNegativeConstantShiftCount) {
	EXPECT_COMPILE_FAIL(R"(
		fn main() : void { var a : u32 = 1; _ = a << -1; }
	)");
	EXPECT_COMPILE_FAIL(R"(
		fn main() : void { var a : i32 = 8; _ = a >> -1; }
	)");
	EXPECT_COMPILE_FAIL(R"(
		fn main() : void { var a : u32 = 1; a <<= -2; }
	)");
	EXPECT_COMPILE_FAIL(R"(
		comptime X = 1 << -1;
		fn main() : void {}
	)");
	return true;
}

TEST(BitwiseRejectsPrimitiveOperatorOverloads) {
	EXPECT_COMPILE_FAIL(R"(
		operator &(a : i32, b : i32) : i32 { return 0; }
		fn main() : void {}
	)");
	EXPECT_COMPILE_FAIL(R"(
		operator |(a : u8, b : u8) : u8 { return 0; }
		fn main() : void {}
	)");
	EXPECT_COMPILE_FAIL(R"(
		operator ~(a : u32) : u32 { return 0; }
		fn main() : void {}
	)");
	EXPECT_COMPILE_FAIL(R"(
		operator <<(a : u64, n : u32) : u64 { return 0; }
		fn main() : void {}
	)");
	EXPECT_COMPILE_FAIL(R"(
		enum Mode { A, B }
		operator |(a : Mode, b : Mode) : Mode { return a; }
		fn main() : void {}
	)");
	return true;
}

TEST(BitwiseUnionTypeSyntaxIsNotBitwiseOr) {
	EXPECT_COMPILE(R"(
		fn main() : i32 {
			var v : union { i32, bool } = 42;
			var x : i32 = 6;
			return x | 1;
		}
	)");
	EXPECT_COMPILE_FAIL(R"(
		fn main() : void {
			var v : i32 | bool = 42;
		}
	)");
	return true;
}

TEST(BitwiseUntypedNotInComptimeConstant) {
	// with no contextual type, ~ of an untyped constant is the untyped constant -a - 1
	const char* source = R"(
		comptime A = ~0;
		comptime B = ~0xF0;
		fn as_i32() : i64 { var r : i32 = A; return r as i64; }
		fn as_i64() : i64 { var r : i64 = A; return r; }
		fn cast_u8() : i64 { return (A as u8) as i64; }
		fn cast_u32() : i64 { return (A as u32) as i64; }
		fn masked() : i64 { var x : u32 = 0xFFFF; return (x & (B as u32)) as i64; }
		fn plain() : i64 { return A; }
	)";
	if (!runBitwiseCases(__func__, source, {
		{ "as_i32", -1 },
		{ "as_i64", -1 },
		{ "cast_u8", 255 },
		{ "cast_u32", 0xFFFFFFFFll },
		{ "masked", 0xFF0F },
		{ "plain", -1 },
	})) return false;

	// the constant is -1, so it does not fit an unsigned type without a cast
	EXPECT_COMPILE_FAIL(R"(
		comptime A = ~0;
		fn main() : void { var r : u8 = A; }
	)");
	EXPECT_COMPILE_FAIL(R"(
		comptime A = ~0;
		fn main() : void { var x : u32 = 1; _ = x & A; }
	)");
	return true;
}
