#pragma once

#include <charconv>
#include <cstring>
#include <cstdlib>
#include <string>
#include <span>

#include "token.h"
#include "capi.h"

inline const char* data(ls_string_view s) {
    return s.begin;
}

inline size_t size(ls_string_view s) {
    return (size_t)(s.end - s.begin);
}

inline bool empty(ls_string_view s) {
    return s.begin == s.end;
}

inline ls_string_view makeStringView(const char* cstr) {
    return cstr ? ls_string_view{cstr, cstr + std::strlen(cstr)} : ls_string_view{};
}

struct OutputFormatter {
    const ls_host* host = nullptr;
    bool has_error = false;
    
    void print(int v);
    void print(const char* s) { print(makeStringView(s)); }
    void print(ls_string_view s) {
        has_error = true;
        if (!host->print) return;
        host->print(host->diagnostics_userdata, s);
    }

    template <typename... Args> void errorAt(const Token& token, Args&&... args) {
        if (has_error) return;
        
        if (!empty(token.source_name)) {
            print(token.source_name);
            print(": ");
        }
        print("line ");
        print(token.line);
        print(", column ");
        print(token.column);
        print(": ");
        int dummy[] = {
            (print(static_cast<Args&&>(args)), 0)...,
        };
        print("\n");
    }
    
    template <typename... Args> void error(Args&&... args) { 
        if (has_error) return;
        int dummy[] = {
            (print(static_cast<Args&&>(args)), 0)...,
        };
    }
};

inline bool equalStrings(ls_string_view lhs, const char* rhs) {
    if (!rhs) return empty(lhs);
    const size_t rhs_size = std::strlen(rhs);
    return size(lhs) == rhs_size && std::memcmp(data(lhs), rhs, rhs_size) == 0;
}

inline bool equalStrings(ls_string_view lhs, ls_string_view rhs) {
    return size(lhs) == size(rhs) && std::memcmp(data(lhs), data(rhs), size(lhs)) == 0;
}

inline bool equalStrings(const char* lhs, ls_string_view rhs) {
    return equalStrings(makeStringView(lhs), rhs);
}

inline bool startsWith(ls_string_view str, ls_string_view prefix) {
    return size(str) >= size(prefix) && std::memcmp(data(str), data(prefix), size(prefix)) == 0;
}

inline bool startsWith(ls_string_view str, const char* prefix) {
    return startsWith(str, makeStringView(prefix));
}

inline bool endsWith(ls_string_view str, ls_string_view suffix) {
    return size(str) >= size(suffix) && std::memcmp(data(str) + size(str) - size(suffix), data(suffix), size(suffix)) == 0;
}

inline bool endsWith(ls_string_view str, const char* suffix) {
    return endsWith(str, makeStringView(suffix));
}

inline bool contains(ls_string_view haystack, char needle) {
    const char* it = data(haystack);
    const char* const end = it + size(haystack);
    for (; it != end; ++it) {
        if (*it == needle) return true;
    }
    return false;
}

inline char toLower(char c) {
	return (c >= 'A' && c <= 'Z') ? char(c - 'A' + 'a') : c;
}

inline bool isLetter(char c) {
	return (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z');
}

inline bool isNumeric(char c) {
	return c >= '0' && c <= '9';
}

inline bool isUpperCase(char c) {
	return c >= 'A' && c <= 'Z';
}

inline bool isWhitespace(char c) {
	return c == ' ' || c == '\t' || c == '\n' || c == '\r' || c == '\f' || c == '\v';
}

inline const char* find(ls_string_view str, char needle) {
    const char* it = data(str);
    const char* const end = it + size(str);
    for (; it != end; ++it) {
        if (*it == needle) return it;
    }
    return nullptr;
}

inline const char* find(ls_string_view str, ls_string_view needle) {
    if (empty(needle)) return data(str);
    if (size(needle) > size(str)) return nullptr;
    const char* const begin = data(str);
    const char* const end = begin + size(str) - size(needle) + 1;
    for (const char* it = begin; it != end; ++it) {
        if (std::memcmp(it, data(needle), size(needle)) == 0) return it;
    }
    return nullptr;
}

inline char* copyString(std::span<char> output, ls_string_view source) {
    ASSERT(output.size() >= size(source) + 1);
    std::memcpy(output.data(), data(source), size(source));
    output.data()[size(source)] = '\0';
    return output.data() + size(source);
}

inline char* catString(std::span<char> output, ls_string_view source) {
    ASSERT(output.size() >= std::strlen(output.data()) + size(source) + 1);
    char* dst = output.data() + std::strlen(output.data());
    std::memcpy(dst, data(source), size(source));
    dst[size(source)] = '\0';
    return dst + size(source);
}

inline bool makeLowercase(std::span<char> output, ls_string_view source) {
    ASSERT(output.size() >= size(source) + 1);
    for (size_t i = 0; i < size(source); ++i) {
        output.data()[i] = toLower(data(source)[i]);
    }
    output.data()[size(source)] = '\0';
    return true;
}

inline const char* fromCString(ls_string_view input, i32& value) {
    auto res = std::from_chars(data(input), data(input) + size(input), value);
    return res.ec == std::errc{} ? res.ptr : data(input);
}

inline const char* fromCString(ls_string_view input, u32& value) {
    auto res = std::from_chars(data(input), data(input) + size(input), value);
    return res.ec == std::errc{} ? res.ptr : data(input);
}

inline const char* fromCString(ls_string_view input, i64& value) {
    auto res = std::from_chars(data(input), data(input) + size(input), value);
    return res.ec == std::errc{} ? res.ptr : data(input);
}

inline const char* fromCString(ls_string_view input, u64& value) {
    auto res = std::from_chars(data(input), data(input) + size(input), value);
    return res.ec == std::errc{} ? res.ptr : data(input);
}

inline const char* fromCString(ls_string_view input, float& value) {
    std::string tmp(data(input), size(input));
    char* end = nullptr;
    value = std::strtof(tmp.c_str(), &end);
    return data(input) + (end - tmp.c_str());
}

inline const char* fromCString(ls_string_view input, double& value) {
    std::string tmp(data(input), size(input));
    char* end = nullptr;
    value = std::strtod(tmp.c_str(), &end);
    return data(input) + (end - tmp.c_str());
}

inline const char* fromCString(ls_string_view input, bool& value) {
    if (equalStrings(input, "true")) {
        value = true;
        return data(input) + 4;
    }
    if (equalStrings(input, "false")) {
        value = false;
        return data(input) + 5;
    }
    i32 iv = 0;
    const char* end = fromCString(input, iv);
    value = iv != 0;
    return end;
}

inline char* toCString(i32 value, std::span<char> output) {
	auto res = std::to_chars(output.data(), output.data() + output.size(), value);
	if (res.ec != std::errc{}) return nullptr;
	if (res.ptr < output.data() + output.size()) *res.ptr = '\0';
	return res.ptr;
}

