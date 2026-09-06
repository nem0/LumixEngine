#pragma once

#include "token.h"
#include "capi.h"

using usize = decltype(sizeof(0));

template <typename T> struct span {
	T* m_data = nullptr;
	usize m_size = 0;

	constexpr span() = default;
	constexpr span(T* data, usize size) : m_data(data), m_size(size) {}

	constexpr T* data() const { return m_data; }
	constexpr usize size() const { return m_size; }
	constexpr T& operator[](usize idx) const { return m_data[idx]; }
	constexpr T* begin() const { return m_data; }
	constexpr T* end() const { return m_data + m_size; }
};

template <typename T> constexpr bool isSigned() {
	return T(-1) < T(0);
}

inline const char* data(ex_string_view s) {
	return s.begin;
}

inline usize size(ex_string_view s) {
	return s.length;
}

inline bool empty(ex_string_view s) {
	return s.length == 0;
}

inline usize stringLength(const char* cstr) {
	usize len = 0;
	if (!cstr) return 0;
	while (cstr[len] != '\0') ++len;
	return len;
}

inline void copyMemory(void* dst, const void* src, usize count) {
	char* d = (char*)dst;
	const char* s = (const char*)src;
	for (usize i = 0; i < count; ++i) d[i] = s[i];
}

inline int compareMemory(const void* lhs, const void* rhs, usize count) {
	const unsigned char* a = (const unsigned char*)lhs;
	const unsigned char* b = (const unsigned char*)rhs;
	for (usize i = 0; i < count; ++i) {
		if (a[i] != b[i]) return a[i] < b[i] ? -1 : 1;
	}
	return 0;
}

inline ex_string_view makeStringView(const char* cstr) {
	return cstr ? ex_string_view{cstr, (i64)stringLength(cstr)} : ex_string_view{};
}

struct OutputFormatter {
	const ex_host* host = nullptr;
	const ex_module* module = nullptr;
	bool has_error = false;

	void print(i32 v);
	void print(const char* s) { print(makeStringView(s)); }
	void print(ex_string_view s) {
		if (!host->print) return;
		host->print(host->diagnostics_userdata, s);
	}

	const SourceLocTable::Entry* resolve(const Token& token) const;
	ex_string_view sourceName(const SourceLocTable::Entry* location) const;

	template <typename... Args> void errorAt(const Token& token, Args&&... args) {
		if (has_error) return;
		has_error = true;

		const SourceLocTable::Entry* loc = resolve(token);
		const ex_string_view source_name = sourceName(loc);
		if (!empty(source_name)) {
			print(source_name);
			print(": ");
		}
		print("line ");
		print(loc ? (i32)loc->line : 0);
		print(": ");
		int dummy[] = {
			(print(static_cast<Args&&>(args)), 0)...,
		};
		(void)dummy;
		print("\n");
	}

	template <typename... Args> void error(Args&&... args) {
		if (has_error) return;
		has_error = true;
		int dummy[] = {
			(print(static_cast<Args&&>(args)), 0)...,
		};
		print("\n");
		(void)dummy;
	}
};

inline bool equalStrings(ex_string_view lhs, const char* rhs) {
	if (!rhs) return empty(lhs);
	const usize rhs_size = stringLength(rhs);
	return size(lhs) == rhs_size && compareMemory(data(lhs), rhs, rhs_size) == 0;
}

inline bool equalStrings(ex_string_view lhs, ex_string_view rhs) {
	return size(lhs) == size(rhs) && compareMemory(data(lhs), data(rhs), size(lhs)) == 0;
}

inline bool equalStrings(const char* lhs, ex_string_view rhs) {
	return equalStrings(makeStringView(lhs), rhs);
}

inline bool contains(ex_string_view haystack, char needle) {
	const char* it = data(haystack);
	const char* const end = it + size(haystack);
	for (; it != end; ++it) {
		if (*it == needle) return true;
	}
	return false;
}

inline bool isLetter(char c) {
	return (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z');
}

inline bool isNumeric(char c) {
	return c >= '0' && c <= '9';
}

inline bool isWhitespace(char c) {
	return c == ' ' || c == '\t' || c == '\n' || c == '\r' || c == '\f' || c == '\v';
}

inline const char* parseInteger(ex_string_view input, i32& value) {
	const char* p = data(input);
	const char* const end = p + size(input);
	bool negative = false;
	if (p != end && (*p == '-' || *p == '+')) {
		negative = *p == '-';
		++p;
	}
	i64 result = 0;
	while (p != end && *p >= '0' && *p <= '9') {
		result = result * 10 + (*p - '0');
		++p;
	}
	value = negative ? (i32)-result : (i32)result;
	return p;
}

inline const char* fromCString(ex_string_view input, i32& value) {
	return parseInteger(input, value);
}

inline const char* fromCString(ex_string_view input, i64& value) {
	const char* p = data(input);
	const char* const end = p + size(input);
	bool negative = false;
	if (p != end && (*p == '-' || *p == '+')) {
		negative = *p == '-';
		++p;
	}
	i64 result = 0;
	while (p != end && *p >= '0' && *p <= '9') {
		result = result * 10 + (*p - '0');
		++p;
	}
	value = negative ? -result : result;
	return p;
}

inline const char* fromCString(ex_string_view input, u64& value) {
	const char* p = data(input);
	const char* const end = p + size(input);
	u64 base = 10;
	if (end - p >= 2 && p[0] == '0' && (p[1] == 'x' || p[1] == 'X')) {
		base = 16;
		p += 2;
	}
	u64 result = 0;
	while (p != end) {
		if (*p == '_') { ++p; continue; }
		u64 digit;
		if (*p >= '0' && *p <= '9') digit = (u64)(*p - '0');
		else if (base == 16 && *p >= 'a' && *p <= 'f') digit = (u64)(*p - 'a' + 10);
		else if (base == 16 && *p >= 'A' && *p <= 'F') digit = (u64)(*p - 'A' + 10);
		else break;
		if (digit >= base || result > (~0ull - digit) / base) return nullptr;
		result = result * base + digit;
		++p;
	}
	value = result;
	return p;
}

inline const char* parseDouble(ex_string_view input, double& value) {
	const char* p = data(input);
	const char* const end = p + size(input);
	bool negative = false;
	if (p != end && (*p == '-' || *p == '+')) {
		negative = *p == '-';
		++p;
	}

	double whole = 0.0;
	while (p != end && ((*p >= '0' && *p <= '9') || *p == '_')) {
		if (*p == '_') { ++p; continue; }
		whole = whole * 10.0 + double(*p - '0');
		++p;
	}

	double frac = 0.0;
	double scale = 1.0;
	if (p != end && *p == '.') {
		++p;
		while (p != end && ((*p >= '0' && *p <= '9') || *p == '_')) {
			if (*p == '_') { ++p; continue; }
			frac = frac * 10.0 + double(*p - '0');
			scale *= 10.0;
			++p;
		}
	}

	double result = whole + (scale > 1.0 ? frac / scale : 0.0);

	if (p != end && (*p == 'e' || *p == 'E')) {
		++p;
		bool exp_negative = false;
		if (p != end && (*p == '-' || *p == '+')) {
			exp_negative = *p == '-';
			++p;
		}
		i32 exponent = 0;
		while (p != end && *p >= '0' && *p <= '9') {
			exponent = exponent * 10 + (*p - '0');
			++p;
		}
		double factor = 1.0;
		for (i32 i = 0; i < exponent; ++i) factor *= 10.0;
		result = exp_negative ? result / factor : result * factor;
	}

	value = negative ? -result : result;
	return p;
}

inline const char* fromCString(ex_string_view input, double& value) {
	return parseDouble(input, value);
}

inline char* toCString(i32 value, char* output, usize output_size) {
	if (!output || output_size == 0) return nullptr;
	char tmp[32];
	usize idx = 0;
	bool negative = value < 0;
	i64 n = negative ? -(i64)value : (i64)value;
	do {
		tmp[idx++] = char('0' + (n % 10));
		n /= 10;
	} while (n != 0 && idx < sizeof(tmp));
	if (negative) tmp[idx++] = '-';
	if (idx + 1 > output_size) return nullptr;
	for (usize i = 0; i < idx; ++i) output[i] = tmp[idx - 1 - i];
	output[idx] = '\0';
	return output + idx;
}
