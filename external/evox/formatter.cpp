#include "tokenizer.h"
#include <string>
#include <vector>

struct EvoxFormatter {
    std::string out;
    int indent = 0;
    int parens = 0;
    int brackets = 0;
    bool in_fn_signature = false;
    bool function_pending = false;
    bool function_body = false;
    bool after_function = false;
    bool line_start = true;
    bool space = false;
    Token::Type previous = Token::END_OF_FILE;

    void newline() {
        while (!out.empty() && out.back() == ' ') out.pop_back();
        if (!out.empty() && out.back() != '\n') out.push_back('\n');
        line_start = true;
        space = false;
    }
    void start(bool add_space = true) {
        if (line_start) { out.append((size_t)indent * 4, ' '); line_start = false; }
        if (add_space && space && !out.empty() && out.back() != '\n') out.push_back(' ');
        space = false;
    }
    void gap(const char* begin, const char* end) {
        int newline_count = 0;
        bool newline_before_comment = false;
        for (const char* p = begin; p < end; ++p) {
            if (*p == '\n') { ++newline_count; newline_before_comment = true; }
            if (*p == '/' && p + 1 < end && p[1] == '/') {
                if (newline_before_comment) newline();
                start(!line_start);
                while (p < end && *p != '\n') out.push_back(*p++);
                newline();
                newline_count = 0;
                newline_before_comment = false;
            }
        }
        if (newline_count > 0 && !(previous == Token::RIGHT_BRACE && parens > 0)) {
            if (!line_start) newline();
            for (int i = 1; i < newline_count; ++i) out.push_back('\n');
            line_start = true;
            space = false;
        }
    }
    void token(const Token& t, const char* begin, const char* end) {
        if (after_function) {
            if (out.size() >= 1 && out.back() == '\n' &&
                (out.size() < 2 || out[out.size() - 2] != '\n')) out.push_back('\n');
            after_function = false;
        }
        if (t.type == Token::FN) {
            if (out.size() >= 1 && out.back() == '\n' &&
                (out.size() < 2 || out[out.size() - 2] != '\n')) out.push_back('\n');
            function_pending = true;
        }
        if (previous == Token::RIGHT_BRACE && t.type != Token::ELSE &&
            t.type != Token::SEMICOLON && t.type != Token::COMMA &&
            t.type != Token::RIGHT_BRACE && !line_start) newline();
        const bool word = t.type == Token::IDENTIFIER || t.type == Token::NUMBER ||
            t.type == Token::STRING || t.type == Token::RUNE ||
            t.type >= Token::EXTERN;
        if (t.type == Token::FN) { in_fn_signature = true; function_pending = true; }
        switch (t.type) {
        case Token::LEFT_BRACE:
            if (function_pending) { function_body = true; function_pending = false; }
            if (brackets > 0 && parens == 0) {
                if (line_start) { out.append((size_t)(indent + 1) * 4, ' '); line_start = false; }
                if (space) space = false;
                out.push_back('{'); space = false;
            } else {
                start(true); out.push_back('{');
                if (parens == 0) { ++indent; newline(); } else space = true;
            }
            break;
        case Token::RIGHT_BRACE:
            if (brackets > 0 && parens == 0) {
                start(false); out.push_back('}');
            } else if (parens == 0) {
                newline(); if (indent) --indent; start(false); out.push_back('}');
                if (function_body && indent == 0) { after_function = true; function_body = false; }
            } else {
                start(true); out.push_back('}');
            }
            space = true; break;
        case Token::SEMICOLON:
            start(false); out.push_back(';'); newline(); break;
        case Token::COMMA:
            start(false); out.push_back(','); space = true; break;
        case Token::LEFT_PAREN: {
            const bool keyword_paren = previous == Token::IF || previous == Token::WHILE ||
                previous == Token::FOR || previous == Token::MATCH || previous == Token::SIZEOF ||
                previous == Token::ALIGNOF || previous == Token::TYPEOF;
            start(keyword_paren); out.push_back(begin[0]); ++parens; break;
        }
        case Token::LEFT_BRACKET: {
            const bool bracket_space = previous == Token::EQUAL || previous == Token::COLON ||
                previous == Token::COMMA || previous == Token::LEFT_BRACKET;
            start(bracket_space); out.push_back(begin[0]); ++brackets; break;
        }
        case Token::RIGHT_PAREN:
            start(false); out.push_back(begin[0]); if (parens) --parens; in_fn_signature = false; space = true; break;
        case Token::RIGHT_BRACKET:
            start(false); out.push_back(begin[0]); if (brackets) --brackets; space = true; break;
        case Token::DOT: {
            const bool leading_member = previous == Token::CASE || previous == Token::EQUAL_EQUAL ||
                previous == Token::BANG_EQUAL || previous == Token::GT || previous == Token::LT ||
                previous == Token::GT_EQUAL || previous == Token::LT_EQUAL;
            start(leading_member); out.append(begin, end); break;
        }
        case Token::DOUBLE_COLON:
            start(false); out.append(begin, end); break;
        case Token::COLON:
            start(false); out.push_back(':'); space = brackets == 0; break;
        case Token::EQUAL: case Token::PLUS_EQUAL: case Token::MINUS_EQUAL:
        case Token::STAR_EQUAL: case Token::SLASH_EQUAL:
            start(true); out.append(begin, end); space = true; break;
        case Token::PLUS: case Token::MINUS: {
            const bool unary = previous == Token::LEFT_PAREN || previous == Token::LEFT_BRACKET ||
                previous == Token::COMMA || previous == Token::EQUAL || previous == Token::COLON ||
                previous == Token::PLUS || previous == Token::MINUS || previous == Token::STAR ||
                previous == Token::SLASH;
            start(!unary || previous == Token::COMMA); out.append(begin, end); space = !unary; break;
        }
        default:
            start(word || previous != Token::DOT);
            out.append(begin, end);
            space = true;
            break;
        }
        previous = t.type;
    }
};

extern "C" ex_result evoxc_format_source(ex_string_view source, ex_arena* arena, ex_string_view* formatted) {
    if (!arena || !formatted) return EX_RESULT_INVALID_ARGUMENT;
    SourceLocTable locations(*arena);
    Tokenizer lexer(locations);
    lexer.init(source, EX_DEBUG_UNIT_NONE);
    std::vector<Token> tokens;
    for (;;) {
        Token t = lexer.consumeToken();
        if (t.type == Token::END_OF_FILE) break;
        tokens.push_back(t);
    }

    EvoxFormatter formatter;
    const char* cursor = data(source);
    for (const Token& t : tokens) {
        const char* token_begin = t.value.begin;
        const char* token_end = t.value.begin + t.value.length;
        if (t.type == Token::STRING || t.type == Token::RUNE) {
            --token_begin;
            ++token_end;
        }
        formatter.gap(cursor, token_begin);
        formatter.token(t, token_begin, token_end);
        cursor = token_end;
    }
    formatter.gap(cursor, data(source) + size(source));
    if (!formatter.line_start) formatter.newline();
    // A literal used as a call argument stays on the call line.
    for (size_t pos = 0; (pos = formatter.out.find("}\n", pos)) != std::string::npos;) {
        size_t close = pos + 2;
        while (close < formatter.out.size() &&
               (formatter.out[close] == ' ' || formatter.out[close] == '\t')) ++close;
        if (close + 1 < formatter.out.size() && formatter.out[close] == ')' &&
            formatter.out[close + 1] == ';') {
            formatter.out.erase(pos + 1, close - (pos + 1));
            ++pos;
        } else ++pos;
    }

    char* result = (char*)arena->allocate(arena->user_data, formatter.out.size(), 1);
    if (!result && !formatter.out.empty()) return EX_RESULT_FAILURE;
    copyMemory(result, formatter.out.data(), (u32)formatter.out.size());
    *formatted = ex_string_view{result, (i64)formatter.out.size()};
    return EX_RESULT_OK;
}
