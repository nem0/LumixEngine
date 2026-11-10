#include "core/log.h"
#include "tests/common.h"
#include "ui/editor/ui_evox_expand.inl"
#include <string.h>

using namespace Lumix;

namespace {

const char* findText(StringView haystack, const char* needle) {
	const u64 n = stringLength(needle);
	for (u64 i = 0; i + n <= haystack.length; ++i) {
		if (memcmp(haystack.data + i, needle, n) == 0) return haystack.data + i;
	}
	return nullptr;
}

bool contains(const String& s, const char* needle) {
	return findText(StringView(s.c_str(), s.length()), needle) != nullptr;
}

// errors are collected, not logged: the tests that expect a failure don't print errors
bool expandSource(const char* source, String& out, String& errors) {
	return expandEvox(getGlobalAllocator(), StringView(source), "test.ui", out, &errors);
}

bool testExpandWithoutBlocks() {
	const char* source = "[box $a] {\n\t[box $b] {}\n}\n";
	String out(getGlobalAllocator());
	String errors(getGlobalAllocator());
	ASSERT_TRUE(expandSource(source, out, errors));
	ASSERT_TRUE(out == StringView(source));
	ASSERT_EQ(0u, errors.length());
	return true;
}

bool testExpandRings() {
	const char* source = R"X([box $root] {
	[evox] {
		for i in 0..4 {
			emit(`
				[box $wr_{i} position=absolute left=0 top=0 pivot-x=50% pivot-y=50% width=52 height=52 clipping=true visible=false] {{
					[box $wrf_{i} position=absolute left=0 top=0 width=364 height=156 bg-image="ui/ring_atlas.spr"] {{}
					[box $wrs_{i} position=absolute left=16 top=16 width=20 height=20 bg-image="ui/scav_icon.spr" bg-fit=contain visible=false] {{}
					[box $wrw_{i} position=absolute left=16 top=16 width=20 height=20 bg-image="ui/wrench_icon.spr" bg-fit=contain visible=false] {{}
				}
			`);
		}
	}
}
)X";
	String out(getGlobalAllocator());
	String errors(getGlobalAllocator());
	ASSERT_TRUE(expandSource(source, out, errors));
	ASSERT_EQ(0u, errors.length());

	MockDocument doc;
	ASSERT_PARSE(doc, out.c_str());
	ASSERT_EQ(17, (int)doc.m_elements.size()); // the root and 4 x 4
	const char* ids[] = { "wr_0", "wr_3", "wrf_1", "wrs_2", "wrw_3" };
	for (const char* id : ids) {
		ASSERT_TRUE(doc.getElementByID(id) != nullptr);
	}
	ASSERT_TRUE(doc.getElementByID("wr_4") == nullptr);
	ASSERT_TRUE(doc.getElementByID("wrf_-1") == nullptr);
	return true;
}

bool testExpandKeepsLines() {
	const char* source = R"X([box $root] {
	[evox] {
		for i in 0..3 {
			emit(`
				[box $a_{i}] {{}
				[box $b_{i}] {{}
			`);
		}
	}
	[box $after] {}
}
)X";
	String out(getGlobalAllocator());
	String errors(getGlobalAllocator());
	ASSERT_TRUE(expandSource(source, out, errors));
	ASSERT_EQ(countNewlines(StringView(source)), countNewlines(StringView(out.c_str(), out.length())));
	const StringView expanded(out.c_str(), out.length());
	const char* in_source = findText(StringView(source), "$after");
	const char* in_expanded = findText(expanded, "$after");
	ASSERT_TRUE(in_source && in_expanded);
	ASSERT_EQ(lineAt(StringView(source), in_source), lineAt(expanded, in_expanded));
	return true;
}

bool testExpandDropsComments() {
	const char* source = R"X([box $root] {
	[evox] {
		emit(`
			// line comment
			[box $a] {{} /* block comment */ [box $b] {{} // trailing
			[box $c] {{}
		`);
	}
}
)X";
	String out(getGlobalAllocator());
	String errors(getGlobalAllocator());
	ASSERT_TRUE(expandSource(source, out, errors));
	ASSERT_TRUE(!contains(out, "comment"));
	ASSERT_TRUE(!contains(out, "trailing"));

	MockDocument doc;
	ASSERT_PARSE(doc, out.c_str());
	const char* ids[] = { "a", "b", "c" };
	for (const char* id : ids) {
		ASSERT_TRUE(doc.getElementByID(id) != nullptr);
	}
	return true;
}

bool testExpandValues() {
	const char* source = R"X([box $root] {
	[evox] {
		for i in 0..3 {
			emit(`[box $s_{i} left={i * 60} top={(i as f32) * 0.5}] {{}`);
		}
		emit(`[box $flag visible={true}] {{}`);
	}
}
)X";
	String out(getGlobalAllocator());
	String errors(getGlobalAllocator());
	ASSERT_TRUE(expandSource(source, out, errors));
	ASSERT_TRUE(contains(out, "$s_0 left=0 top=0]"));
	ASSERT_TRUE(contains(out, "$s_1 left=60 top=0.5]"));
	ASSERT_TRUE(contains(out, "$s_2 left=120 top=1]"));
	ASSERT_TRUE(contains(out, "visible=true]"));

	MockDocument doc;
	ASSERT_PARSE(doc, out.c_str());
	ASSERT_TRUE(doc.getElementByID("s_2") != nullptr);
	return true;
}

bool testExpandIgnoresEvoxInStringsAndComments() {
	const char* source = "[box $a] { \"[evox] { nope }\" }\n// [evox] { nope }\n/* [evox] { nope } */\n[box $b] {}\n";
	String out(getGlobalAllocator());
	String errors(getGlobalAllocator());
	ASSERT_TRUE(expandSource(source, out, errors));
	ASSERT_TRUE(out == StringView(source));
	return true;
}

bool testExpandTwoBlocks() {
	const char* source = R"X([box $root] {
	[evox] {
		emit(`[box $first] {{}`);
	}
	[box $between] {}
	[evox] {
		for i in 0..2 {
			emit(`[box $second_{i}] {{}`);
		}
	}
}
)X";
	String out(getGlobalAllocator());
	String errors(getGlobalAllocator());
	ASSERT_TRUE(expandSource(source, out, errors));
	MockDocument doc;
	ASSERT_PARSE(doc, out.c_str());
	ASSERT_EQ(5, (int)doc.m_elements.size());
	// the order of the source is kept
	const ui::Element* root = doc.getElementByID("root");
	ASSERT_TRUE(root != nullptr);
	ASSERT_EQ(4, (int)root->children.size());
	ASSERT_TRUE(doc.getElementByID("first") == &doc.m_elements[root->children[0]]);
	ASSERT_TRUE(doc.getElementByID("between") == &doc.m_elements[root->children[1]]);
	ASSERT_TRUE(doc.getElementByID("second_1") == &doc.m_elements[root->children[3]]);
	return true;
}

// braces inside Evox strings, runes and comments are not the end of the block
bool testExpandBracesInEvoxCode() {
	const char* source = R"X([box $root] {
	[evox] {
		var text : []const u8 = "}{"; // }
		var brace : u8 = '}';
		emit(`[box $a] {{}`);
	}
}
)X";
	String out(getGlobalAllocator());
	String errors(getGlobalAllocator());
	ASSERT_TRUE(expandSource(source, out, errors));
	MockDocument doc;
	ASSERT_PARSE(doc, out.c_str());
	ASSERT_TRUE(doc.getElementByID("a") != nullptr);
	return true;
}

bool testExpandErrors() {
	String out(getGlobalAllocator());
	String errors(getGlobalAllocator());

	// a type error, reported at the line of the file
	ASSERT_TRUE(!expandSource("[box $root] {\n\t[evox] {\n\t\tvar x : i32 = \"text\";\n\t}\n}\n", out, errors));
	ASSERT_TRUE(contains(errors, "test.ui(3): [evox] type error"));
	// the details have the position in the file too, not the one of the generated script
	ASSERT_TRUE(contains(errors, "test.ui(3:3): Cannot convert"));

	errors.resize(0);
	out.resize(0);
	ASSERT_TRUE(!expandSource("[box $root] {\n\t[evox] {\n\t\tvar ;\n\t}\n}\n", out, errors));
	ASSERT_TRUE(contains(errors, "test.ui(3): [evox] syntax error"));

	// fails while running
	errors.resize(0);
	out.resize(0);
	ASSERT_TRUE(!expandSource("[evox] {\n\tfor i in 0..1 {\n\t\tvar y : i32 = 10 / i;\n\t}\n}\n", out, errors));
	ASSERT_TRUE(contains(errors, "[evox] script failed"));

	errors.resize(0);
	out.resize(0);
	ASSERT_TRUE(!expandSource("[box $root] {\n\t[evox] {\n\t\temit(`[box] {{}`);\n", out, errors));
	ASSERT_TRUE(contains(errors, "test.ui(2): unterminated [evox] block"));

	errors.resize(0);
	out.resize(0);
	ASSERT_TRUE(!expandSource("[box $root] {\n\t[evox]\n}\n", out, errors));
	ASSERT_TRUE(contains(errors, "test.ui(2): [evox] needs a body"));

	errors.resize(0);
	out.resize(0);
	ASSERT_TRUE(!expandSource("[evox width=1] {}\n", out, errors));
	ASSERT_TRUE(contains(errors, "test.ui(1): [evox] takes no attributes"));
	return true;
}

// expansion doesn't validate the markup, Document::parse does
bool testExpandedMarkupIsParsed() {
	const char* source = "[box $root] {\n\t[evox] {\n\t\temit(`[box $a bogus=1] {{}`);\n\t}\n}\n";
	String out(getGlobalAllocator());
	String errors(getGlobalAllocator());
	ASSERT_TRUE(expandSource(source, out, errors));
	MockDocument doc;
	doc.m_suppress_logging = true;
	ASSERT_TRUE(!doc.parse(StringView(out.c_str(), out.length()), "test.ui"));
	return true;
}

bool testExpandStringNewlines() {
	// the newline between the emits is inside a string token, the block has one line but the fragment two
	const char* too_many = "[box $root] {\n\t[evox] { emit(`[box $a text=\"a`); emit(`b\"] {{}`); }\n}\n";
	String out(getGlobalAllocator());
	String errors(getGlobalAllocator());
	ASSERT_TRUE(!expandSource(too_many, out, errors));
	ASSERT_TRUE(contains(errors, "more lines than the block has"));

	// the same newline fits in a block that has the lines, the padding is reduced by it
	const char* fits = "[box $root] {\n\t[evox] {\n\t\temit(`[box $a text=\"a`);\n\t\temit(`b\"] {{}`);\n\t}\n}\n";
	String out2(getGlobalAllocator());
	String errors2(getGlobalAllocator());
	ASSERT_TRUE(expandSource(fits, out2, errors2));
	ASSERT_EQ(countNewlines(StringView(fits)), countNewlines(StringView(out2.c_str(), out2.length())));
	return true;
}

bool testFindEvoxBlockEnd() {
	struct Case { const char* text; const char* end; }; // end: the text that starts at the closing brace, null if there is none
	const Case cases[] = {
		{ " emit(`{{ }`); } rest", "} rest" },
		{ " var s = \"}{\"; } rest", "} rest" },
		{ " // }\n } rest", "} rest" },
		{ " var c = '}'; } rest", "} rest" },
		{ " if true { emit(`a`); } } rest", "} rest" },
		{ " emit(`{ \"}\" }`); } rest", "} rest" },
		{ " emit(`a {{ b }} c`); } rest", "} rest" },
		{ " emit(`abc`);", nullptr },
		{ " emit(`abc);\n}", nullptr },
		{ " var s = \"abc; }", nullptr },
		{ "", nullptr },
	};
	for (const Case& c : cases) {
		const StringView text(c.text);
		const i64 end = findEvoxBlockEnd(text);
		if (c.end) {
			const char* expected = findText(text, c.end);
			ASSERT_TRUE(expected != nullptr);
			ASSERT_EQ((i64)(expected - text.data), end);
		}
		else {
			ASSERT_EQ((i64)-1, end);
		}
	}
	return true;
}

} // namespace

void runUIEvoxExpandTests() {
	logInfo("=== Running UI [evox] expansion Tests ===");

	RUN_TEST(testExpandWithoutBlocks);
	RUN_TEST(testExpandRings);
	RUN_TEST(testExpandKeepsLines);
	RUN_TEST(testExpandDropsComments);
	RUN_TEST(testExpandValues);
	RUN_TEST(testExpandIgnoresEvoxInStringsAndComments);
	RUN_TEST(testExpandTwoBlocks);
	RUN_TEST(testExpandBracesInEvoxCode);
	RUN_TEST(testExpandErrors);
	RUN_TEST(testExpandedMarkupIsParsed);
	RUN_TEST(testExpandStringNewlines);
	RUN_TEST(testFindEvoxBlockEnd);
}
