#include "core/log.h"
#include "core/string.h"
#include "renderer/editor/particle_script_compiler.h"
#include "tests/common.h"

using namespace black;

namespace {

bool testCollectorSimpleDeclarations() {
	IAllocator& allocator = getGlobalAllocator();
	const char* src = R"(
		fn foo(a, b) {
			var x
		}
		const PI = 3.14
		global g_var
		emitter my_emitter {
		}
	)";

	ParticleScriptCompiler::CollectorOptions opts;
	opts.stop_at_cursor_only = false;
	ParticleScriptCompiler::CollectorResult res = ParticleScriptCompiler::collectSymbolsFromBuffer(allocator, src, 0, opts);

	bool found_fn = false;
	bool found_var = false;
	bool found_const = false;
	bool found_global = false;
	bool found_emitter = false;

	for (int i = 0; i < res.symbols.size(); ++i) {
		auto& s = res.symbols[i];
		if (equalStrings(s.name, "foo")) found_fn = true;
		if (equalStrings(s.name, "x")) found_var = true;
		if (equalStrings(s.name, "PI")) found_const = true;
		if (equalStrings(s.name, "g_var")) found_global = true;
		if (equalStrings(s.name, "my_emitter")) found_emitter = true;
	}

	ASSERT_TRUE(found_fn, "function not found");
	ASSERT_TRUE(found_var, "var not found");
	ASSERT_TRUE(found_const, "const not found");
	ASSERT_TRUE(found_global, "global not found");
	ASSERT_TRUE(found_emitter, "emitter not found");

	return true;
}

bool testCollectorStopAtCursor() {
	IAllocator& allocator = getGlobalAllocator();
	const char* src = "fn before() {}\nfn after() {}";
	// place cursor before the second function
	int cursor = 12; // somewhere in between
	ParticleScriptCompiler::CollectorOptions opts;
	opts.stop_at_cursor_only = true;
	ParticleScriptCompiler::CollectorResult res = ParticleScriptCompiler::collectSymbolsFromBuffer(allocator, src, cursor, opts);

	bool found_before = false;
	bool found_after = false;
	for (int i = 0; i < res.symbols.size(); ++i) {
		auto& s = res.symbols[i];
		if (equalStrings(s.name, "before")) found_before = true;
		if (equalStrings(s.name, "after")) found_after = true;
	}

	ASSERT_TRUE(found_before, "before not found");
	ASSERT_TRUE(!found_after, "after should not be found");

	return true;
}

bool testCollectorNestedScope() {
	IAllocator& allocator = getGlobalAllocator();
	const char* src = "var a\n{\n  var b\n}\n";
	ParticleScriptCompiler::CollectorOptions opts;
	opts.stop_at_cursor_only = false;
	ParticleScriptCompiler::CollectorResult res = ParticleScriptCompiler::collectSymbolsFromBuffer(allocator, src, 0, opts);

	bool found_b = false;
	int b_scope = -1;
	for (int i = 0; i < res.symbols.size(); ++i) {
		auto& s = res.symbols[i];
		if (equalStrings(s.name, "b")) {
			found_b = true;
			b_scope = s.scope_id;
			break;
		}
	}

	ASSERT_TRUE(found_b, "symbol b not found");
	ASSERT_TRUE(b_scope != 0, "symbol b should be in inner scope");

	// verify scope exists and contains symbol range
	bool scope_ok = false;
	for (int i = 0; i < res.scopes.size(); ++i) {
		auto& sc = res.scopes[i];
		if (sc.id == b_scope) {
			// sc.start_offset should be less than symbol's start
			for (int j = 0; j < res.symbols.size(); ++j) {
				if (res.symbols[j].scope_id == b_scope) {
					if (sc.start_offset <= res.symbols[j].start_offset && res.symbols[j].end_offset <= sc.end_offset) scope_ok = true;
				}
			}
			break;
		}
	}

	ASSERT_TRUE(scope_ok, "scope for b doesn't enclose symbol");
	return true;
}

bool testCollectorTruncation() {
	IAllocator& allocator = getGlobalAllocator();
	const char* src = "var a\nvar b\nvar c\nvar d\n";
	ParticleScriptCompiler::CollectorOptions opts;
	opts.stop_at_cursor_only = false;
	opts.max_symbols = 1;
	ParticleScriptCompiler::CollectorResult res = ParticleScriptCompiler::collectSymbolsFromBuffer(allocator, src, 0, opts);

	ASSERT_TRUE(res.truncated, "result should be truncated");
	ASSERT_TRUE(res.symbols.size() == 1, "should contain only one symbol when truncated");
	return true;
}

bool testCollectorCursorScope() {
	IAllocator& allocator = getGlobalAllocator();
	const char* src = "fn outer() { var o; { var inner; } }";
	const char* pos = strstr(src, "inner");
	ASSERT_TRUE(pos != nullptr, "failed to find inner");
	int cursor = int(pos - src) + 1; // inside inner block

	ParticleScriptCompiler::CollectorOptions opts;
	opts.stop_at_cursor_only = true;
	ParticleScriptCompiler::CollectorResult res = ParticleScriptCompiler::collectSymbolsFromBuffer(allocator, src, cursor, opts);

	ASSERT_TRUE(res.cursor_scope_id != -1, "cursor scope should be found");

	// find the scope and ensure it's a Block
	bool is_block = false;
	for (int i = 0; i < res.scopes.size(); ++i) {
		auto& sc = res.scopes[i];
		if (sc.id == res.cursor_scope_id) {
			if (sc.kind == ParticleScriptCompiler::ScopeKind::Block) is_block = true;
			break;
		}
	}

	ASSERT_TRUE(is_block, "cursor scope should be a Block");
	return true;
}

bool testCollectorEmitterFields() {
	IAllocator& allocator = getGlobalAllocator();
	const char* src = R"(
		emitter e1 {
			fn emit() {
				let x = {1, 2, 3}
			}			

			out pos
			in vel
			var local_in_emitter
		}
	)";

	ParticleScriptCompiler::CollectorOptions opts;
	opts.stop_at_cursor_only = false;
	ParticleScriptCompiler::CollectorResult res = ParticleScriptCompiler::collectSymbolsFromBuffer(allocator, src, 0, opts);

	bool found_pos = false;
	bool found_vel = false;
	bool found_local = false;
	for (int i = 0; i < res.symbols.size(); ++i) {
		auto& s = res.symbols[i];
		if (equalStrings(s.name, "pos") && s.kind == ParticleScriptCompiler::SymbolKind::EmitterField) found_pos = true;
		if (equalStrings(s.name, "vel") && s.kind == ParticleScriptCompiler::SymbolKind::EmitterField) found_vel = true;
		if (equalStrings(s.name, "local_in_emitter") && s.kind == ParticleScriptCompiler::SymbolKind::EmitterField) found_local = true;
	}

	ASSERT_TRUE(found_pos, "out pos missing");
	ASSERT_TRUE(found_vel, "in vel missing");
	ASSERT_TRUE(found_local, "var inside emitter missing");
	return true;
}

} // anonymous namespace

void runParticleScriptCollectorTests() {
	logInfo("=== Running Particle Script Collector Tests ===");
	RUN_TEST(testCollectorSimpleDeclarations);
	RUN_TEST(testCollectorStopAtCursor);
	RUN_TEST(testCollectorNestedScope);
	RUN_TEST(testCollectorTruncation);
	RUN_TEST(testCollectorCursorScope);
	RUN_TEST(testCollectorEmitterFields);
}
