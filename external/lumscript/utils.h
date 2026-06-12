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

inline const char* data(ls_string_view s) {
	return s.begin;
}

inline usize size(ls_string_view s) {
	return (usize)(s.end - s.begin);
}

inline bool empty(ls_string_view s) {
	return s.begin == s.end;
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

inline ls_string_view makeStringView(const char* cstr) {
	return cstr ? ls_string_view{cstr, cstr + stringLength(cstr)} : ls_string_view{};
}

struct OutputFormatter {
	const ls_host* host = nullptr;
	bool has_error = false;

	void print(int v);
	void print(const char* s) { print(makeStringView(s)); }
	void print(ls_string_view s) {
		if (!host->print) return;
		host->print(host->diagnostics_userdata, s);
	}

	template <typename... Args> void errorAt(const Token& token, Args&&... args) {
		if (has_error) return;
		has_error = true;

		if (!empty(token.source_name)) {
			print(token.source_name);
			print(": ");
		}
		print("line ");
		print(token.line);
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
		(void)dummy;
	}
};

inline bool equalStrings(ls_string_view lhs, const char* rhs) {
	if (!rhs) return empty(lhs);
	const usize rhs_size = stringLength(rhs);
	return size(lhs) == rhs_size && compareMemory(data(lhs), rhs, rhs_size) == 0;
}

inline bool equalStrings(ls_string_view lhs, ls_string_view rhs) {
	return size(lhs) == size(rhs) && compareMemory(data(lhs), data(rhs), size(lhs)) == 0;
}

inline bool equalStrings(const char* lhs, ls_string_view rhs) {
	return equalStrings(makeStringView(lhs), rhs);
}

inline bool contains(ls_string_view haystack, char needle) {
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

inline const char* parseInteger(ls_string_view input, i32& value) {
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

inline const char* fromCString(ls_string_view input, i32& value) {
	return parseInteger(input, value);
}

inline const char* fromCString(ls_string_view input, i64& value) {
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

inline const char* parseDouble(ls_string_view input, double& value) {
	const char* p = data(input);
	const char* const end = p + size(input);
	bool negative = false;
	if (p != end && (*p == '-' || *p == '+')) {
		negative = *p == '-';
		++p;
	}

	double whole = 0.0;
	while (p != end && *p >= '0' && *p <= '9') {
		whole = whole * 10.0 + double(*p - '0');
		++p;
	}

	double frac = 0.0;
	double scale = 1.0;
	if (p != end && *p == '.') {
		++p;
		while (p != end && *p >= '0' && *p <= '9') {
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

inline const char* fromCString(ls_string_view input, double& value) {
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
