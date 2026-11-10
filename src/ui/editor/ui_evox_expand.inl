#pragma once

#include "core/allocator.h"
#include "core/log.h"
#include "core/string.h"
#include "ui/ui_tokenizer.h"
#include "evox/capi.h"
#include "evox/arena.h"
#include <string.h> // memcpy of EX_ARG

// A [evox] { ... } block in a .ui file is Evox code that emits markup. It is replaced by the emitted markup when the
// asset is compiled and when the editor parses the file, so the runtime never sees it:
//
//   [evox] {
//       for i in 0..4 {
//           emit(`[box $slot_{i} left={i * 60}] {{}`);
//       }
//   }
//
// - the body is the body of `fn main(ctx : cptr)`, `emit` takes any number of values. A backtick string is split by Evox into
//   its text pieces and the interpolated values, which are joined again here. `{{` is a literal `{`, `}` is literal
// - what a block emits is re-tokenized as markup: whitespace and comments between tokens collapse into one space, so the
//   expansion keeps the line count of the original file and errors point to the right line. Every emit() call ends with
//   a line break, so a comment can't swallow the next call and the calls are separated
// - no imports, the body is inside a function
//
// Errors are logged, or appended to `errors` (one per line) if it's not null.

namespace Lumix {

static u32 countNewlines(StringView s) {
	u32 n = 0;
	for (u64 i = 0; i < s.length; ++i) {
		if (s.data[i] == '\n') ++n;
	}
	return n;
}

static int lineAt(StringView source, const char* p) {
	int line = 1;
	for (const char* c = source.data; c < p; ++c) {
		if (*c == '\n') ++line;
	}
	return line;
}

static void evoxError(String* errors, const char* filename, int line, const char* message, StringView details = StringView()) {
	if (errors) {
		char number[16];
		const char* number_end = toCString(line, Span(number));
		errors->append(StringView(filename), StringView("("), StringView(number, number_end), StringView("): "), StringView(message), StringView("\n"));
		if (!details.empty()) errors->append(details, StringView("\n"));
		return;
	}
	logError(filename, "(", line, "): ", message);
	if (!details.empty()) logError("  ", details);
}

// p is just after the opening backtick, returns the position after the closing one (mirrors the Evox tokenizer)
static const char* skipEvoxInterpolatedString(const char* p, const char* end) {
	while (p < end) {
		if (*p == '`') return p + 1;
		if (*p == '{') {
			if (p + 1 < end && p[1] == '{') {
				p += 2;
				continue;
			}
			++p; // interpolated expression, up to the matching '}', braces can't nest in it
			while (p < end && *p != '}') {
				if (*p == '"') {
					++p;
					while (p < end && *p != '"') ++p;
				}
				else if (*p == '\'') {
					++p;
					while (p < end && *p != '\'') ++p;
				}
				else if (*p == '{') return nullptr;
				if (p < end) ++p;
			}
			if (p == end) return nullptr;
			++p; // '}'
			continue;
		}
		++p;
	}
	return nullptr;
}

// text starts just after the '{' of a block, returns the offset of the matching '}' or -1.
// Braces in Evox strings are not structural (`{{` and a lone '}' in backtick strings), so this has to follow Evox's lexical rules
static i64 findEvoxBlockEnd(StringView text) {
	const char* p = text.data;
	const char* end = text.end();
	int depth = 1;
	while (p < end) {
		const char c = *p;
		if (c == '/' && p + 1 < end && p[1] == '/') {
			while (p < end && *p != '\n') ++p;
			continue;
		}
		if (c == '"') {
			++p;
			while (p < end && *p != '"') ++p;
			if (p == end) return -1;
			++p;
			continue;
		}
		if (c == '\'') {
			++p;
			while (p < end && *p != '\'') {
				if (*p == '\n') return -1;
				++p;
			}
			if (p == end) return -1;
			++p;
			continue;
		}
		if (c == '`') {
			p = skipEvoxInterpolatedString(p + 1, end);
			if (!p) return -1;
			continue;
		}
		if (c == '{') ++depth;
		else if (c == '}' && --depth == 0) return p - text.data;
		++p;
	}
	return -1;
}

// What a script emits, appended raw. The native functions have no context argument, so the script passes the String
// as the first argument (the host hands it to main, the prelude keeps it in a global of the script).
static void evoxEmitBytes(ex_runtime*, ex_call_frame frame) {
	EX_ARG(frame, String*, out);
	EX_STRING_ARG(frame, text);
	out->append(StringView(text.begin, (u64)text.length));
}

static void evoxEmitI64(ex_runtime*, ex_call_frame frame) {
	EX_ARG(frame, String*, out);
	EX_ARG(frame, i64, value);
	char tmp[32];
	const char* end = toCString(value, Span(tmp));
	out->append(StringView(tmp, end));
}

static void evoxEmitU64(ex_runtime*, ex_call_frame frame) {
	EX_ARG(frame, String*, out);
	EX_ARG(frame, u64, value);
	char tmp[32];
	const char* end = toCString(value, Span(tmp));
	out->append(StringView(tmp, end));
}

static void evoxEmitF64(ex_runtime*, ex_call_frame frame) {
	EX_ARG(frame, String*, out);
	EX_ARG(frame, double, value);
	char tmp[64];
	const char* end = toCString(value, Span(tmp), 3);
	// 52.000 -> 52, 0.500 -> 0.5
	bool has_dot = false;
	for (const char* c = tmp; c < end; ++c) has_dot = has_dot || *c == '.';
	if (has_dot) {
		while (end > tmp && end[-1] == '0') --end;
		if (end > tmp && end[-1] == '.') --end;
	}
	out->append(StringView(tmp, end));
}

// markup is re-tokenized, comments and line breaks between tokens are replaced by a single space. That keeps what a
// block emits on one line, so the expansion has the line count of the original file
static void collapseEvoxMarkup(StringView text, String& out) {
	UITokenizer t;
	t.m_document = text;
	t.m_filename = "";
	t.m_current = text.data;
	const char* prev_end = text.data;
	bool first = true;
	for (;;) {
		const UITokenizer::Token token = t.nextToken();
		if (token.type == UIToken::EOF) break;
		if (!first && t.m_token_start != prev_end) out.append(StringView(" "));
		// invalid tokens are copied too, Document::parse reports them
		out.append(StringView(t.m_token_start, t.m_current));
		prev_end = t.m_current;
		first = false;
	}
}

static ex_native_fn resolveEvoxEmitNative(ex_runtime*, ex_native_function_desc function, void*) {
	const StringView name(function.name.begin, (u64)function.name.length);
	if (name == "emit_bytes") return &evoxEmitBytes;
	if (name == "emit_i64") return &evoxEmitI64;
	if (name == "emit_u64") return &evoxEmitU64;
	if (name == "emit_f64") return &evoxEmitF64;
	return nullptr;
}

static int noEvoxImports(void*, ex_string_view, ex_string_view, ex_string_view*) { return 0; }

// The body of the block is appended right after the last `{`, so a line of the body is a line of the unit offset by the
// number of lines before it.
static constexpr const char* EVOX_BLOCK_PRELUDE = R"EVOX(var emit_target : cptr = null;
extern fn emit_bytes(ctx : cptr, text : []const u8) : void;
extern fn emit_i64(ctx : cptr, value : i64) : void;
extern fn emit_u64(ctx : cptr, value : u64) : void;
extern fn emit_f64(ctx : cptr, value : f64) : void;

fn emit(parts : ...any) : void {
	for part in parts {
		match part {
			case []const u8:
				emit_bytes(emit_target, part);
			case bool:
				emit_bytes(emit_target, part ? "true" : "false");
			case i8:
				emit_i64(emit_target, part as i64);
			case i16:
				emit_i64(emit_target, part as i64);
			case i32:
				emit_i64(emit_target, part as i64);
			case i64:
				emit_i64(emit_target, part);
			case u8:
				emit_u64(emit_target, part as u64);
			case u16:
				emit_u64(emit_target, part as u64);
			case u32:
				emit_u64(emit_target, part as u64);
			case u64:
				emit_u64(emit_target, part);
			case f32:
				emit_f64(emit_target, part as f64);
			case f64:
				emit_f64(emit_target, part);
			case:
				emit_bytes(emit_target, "<unsupported value>");
		}
	}
	emit_bytes(emit_target, "
");
}

fn main(ctx : cptr) : void { emit_target = ctx;)EVOX";

// Collects the diagnostics of a block. The compiler reports the position of a diagnostic through `diagnostic` and then
// prints "<name>:<line>:<column>: <message>" in pieces, the line is of the generated script (prelude + body), so the
// position in the printed text is replaced by the one in the file
struct EvoxDiagnostics {
	EvoxDiagnostics(IAllocator& allocator, const char* filename, u32 prelude_lines, int body_line)
		: allocator(allocator), text(allocator), message(allocator), filename(filename), prelude_lines(prelude_lines), body_line(body_line) {}

	// line of the file of a line of the generated script
	int fileLine(u32 script_line) const { return script_line > prelude_lines ? body_line + (int)(script_line - prelude_lines) - 1 : body_line; }

	void onDiagnostic(u32 new_line, u32 new_column) {
		flush();
		line = new_line;
		column = new_column;
	}

	// what is printed since the last diagnostic is one message
	void flush() {
		if (message.length() == 0) return;
		char script_line[16];
		char script_column[16];
		const char* script_line_end = toCString(line, Span(script_line));
		const char* script_column_end = toCString(column, Span(script_column));
		String prefix(allocator);
		prefix.append(StringView(filename), StringView(":"), StringView(script_line, script_line_end), StringView(":"), StringView(script_column, script_column_end), StringView(": "));
		StringView rest = message;
		if (rest.length >= prefix.length() && StringView(rest.data, prefix.length()) == StringView(prefix)) {
			rest = StringView(rest.data + prefix.length(), rest.end());
			char file_line[16];
			const char* file_line_end = toCString(fileLine(line), Span(file_line));
			text.append(StringView(filename), StringView("("), StringView(file_line, file_line_end), StringView(":"), StringView(script_column, script_column_end), StringView("): "));
		}
		text.append(rest);
		message.resize(0);
	}

	IAllocator& allocator;
	String text;
	String message;
	const char* filename;
	u32 prelude_lines;
	int body_line;
	u32 line = 0;
	u32 column = 0;
};

// compiles and runs one block, what it emits is appended to fragment. raw is a scratch buffer for what the script emits
// before it's collapsed. body_line is the line of the block's '{'
static bool runEvoxBlock(IAllocator& allocator, StringView body, const char* filename, int body_line, String& raw, String& fragment, String* errors) {
	const u32 prelude_lines = countNewlines(StringView(EVOX_BLOCK_PRELUDE));
	String source(allocator);
	source.append(StringView(EVOX_BLOCK_PRELUDE), body, StringView("\n}\n"));

	EvoxDiagnostics diag(allocator, filename, prelude_lines, body_line);
	ex_host host = {};
	ex_default_arena_create(&host.arena);
	host.diagnostics_userdata = &diag;
	host.print = [](void* userdata, ex_string_view msg) {
		((EvoxDiagnostics*)userdata)->message.append(StringView(msg.begin, (u64)msg.length));
	};
	host.diagnostic = [](void* userdata, ex_string_view, u32 line, u32 column, u32) {
		((EvoxDiagnostics*)userdata)->onDiagnostic(line, column);
	};
	host.native_resolver = &resolveEvoxEmitNative;

	const char* stage = nullptr;
	ex_bytecode* bytecode = nullptr;
	ex_runtime* runtime = nullptr;
	ex_task* task = nullptr;
	ex_module* module = ex_module_create(&host);
	if (!module) {
		stage = "[evox] failed to create module";
	}
	else if (ex_module_add_root(module, ex_string_view{source.c_str(), (i64)source.length()}, ex_string_view{filename, (i64)stringLength(filename)}, &noEvoxImports, nullptr) != EX_RESULT_OK) {
		stage = "[evox] syntax error";
	}
	else if (ex_module_typecheck(module) != EX_RESULT_OK) {
		stage = "[evox] type error";
	}
	else if (!(bytecode = ex_bytecode_compile(module, &host, nullptr))) {
		stage = "[evox] failed to generate bytecode";
	}
	else if (!(runtime = ex_runtime_create(bytecode, &host))) {
		stage = "[evox] failed to create runtime";
	}
	else if (!(task = ex_task_create(runtime))) {
		stage = "[evox] failed to create task";
	}
	else {
		void* ctx = &raw;
		const ex_call_result result = ex_call(task, ex_string_view{"main", 4}, &ctx, sizeof(ctx));
		if (result != EX_CALL_RESULT_OK) stage = "[evox] script failed";
	}

	if (task) ex_task_destroy(task);
	if (runtime) ex_runtime_destroy(runtime);
	if (bytecode) ex_bytecode_destroy(bytecode);
	if (module) ex_module_destroy(module);
	ex_default_arena_destroy(&host.arena);

	if (!stage) {
		collapseEvoxMarkup(raw, fragment);
		return true;
	}
	diag.flush();
	evoxError(errors, filename, diag.fileLine(diag.line), stage, diag.text);
	return false;
}

// Replaces every [evox] { ... } block of source by the markup it emits, the rest is copied as it is. The result has the
// same lines as source (what a block emits is collapsed to its first line) so errors of the expanded text point to
// the original file.
static bool expandEvox(IAllocator& allocator, StringView source, const char* filename, String& out, String* errors = nullptr) {
	UITokenizer t;
	t.m_document = source;
	t.m_filename = filename;
	t.m_current = source.data;
	t.m_current_token = t.nextToken();
	const char* copied = source.data;
	String raw(allocator);
	String fragment(allocator);
	for (;;) {
		const UITokenizer::Token token = t.consumeToken();
		if (token.type == UIToken::EOF) break;
		if (token.type != UIToken::LBRACKET) continue;
		if (t.peekToken().type != UIToken::IDENTIFIER || !(t.peekToken() == "evox")) continue;

		const char* tag_begin = token.value.data;
		t.consumeToken();
		if (t.peekToken().type != UIToken::RBRACKET) {
			evoxError(errors, filename, lineAt(source, tag_begin), "[evox] takes no attributes");
			return false;
		}
		t.consumeToken();
		if (t.peekToken().type != UIToken::LBRACE) {
			evoxError(errors, filename, lineAt(source, tag_begin), "[evox] needs a body");
			return false;
		}
		const UITokenizer::Token lbrace = t.consumeToken();
		const char* body_begin = lbrace.value.end();
		const i64 body_length = findEvoxBlockEnd(StringView(body_begin, source.end()));
		if (body_length < 0) {
			evoxError(errors, filename, lineAt(source, tag_begin), "unterminated [evox] block");
			return false;
		}
		const char* body_end = body_begin + body_length; // the closing '}'

		out.append(StringView(copied, tag_begin));
		raw.resize(0);
		fragment.resize(0);
		if (!runEvoxBlock(allocator, StringView(body_begin, body_end), filename, lineAt(source, body_begin), raw, fragment, errors)) {
			return false;
		}
		// the block had these lines, keep them. Newlines inside emitted strings are part of fragment_lines, so they reduce
		// the padding
		const u32 block_lines = countNewlines(StringView(tag_begin, body_end + 1));
		const u32 fragment_lines = countNewlines(fragment);
		// TODO: don't depend on the line count. Errors of the expanded text are reported with its line numbers, so the
		// expansion has to have the lines of the source, and that fails when an emitted string contains a newline (it
		// can't be removed, strings are not unescaped) and the fragment has more lines than the block. Emit a directive after the fragment (e.g. `#line N`, a syntax that can't be
		// an element) that the tokenizer uses to set its line number and skips, then collapseEvoxMarkup, the padding and
		// this error are not needed. Another option is a map of output lines to source lines used by error reporting.
		if (fragment_lines > block_lines) {
			// string tokens are not unescaped, so a newline in one can't be replaced and the lines after the block would shift
			evoxError(errors, filename, lineAt(source, tag_begin), "[evox] emits a string with more lines than the block has");
			return false;
		}
		out.append(fragment);
		for (u32 i = fragment_lines; i < block_lines; ++i) out.append(StringView("\n"));

		copied = body_end + 1;
		t.m_current = copied;
		t.m_current_token = t.nextToken();
	}
	out.append(StringView(copied, source.end()));
	return true;
}

} // namespace Lumix
