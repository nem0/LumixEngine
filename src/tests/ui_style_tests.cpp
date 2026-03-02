#include "core/log.h"
#include "tests/common.h"

using namespace Lumix;

namespace {

bool testEmptyStyleBlock() {
	MockDocument doc;
	ASSERT_PARSE(doc, "[style] {}");
	Span<u32> root_indices = doc.m_roots;
	ASSERT_EQ(0, root_indices.size());

	return true;
}

bool testStyleWithRules() {
	MockDocument doc;
	ASSERT_PARSE(doc, R"(
		[style] {
			.some_class {
				width: 50%;
				height: 100;
			}
		}
	)");
	Span<u32> root_indices = doc.m_roots;
	ASSERT_EQ(0, root_indices.size());
	Span<ui::StyleRule> rules = doc.m_stylesheet.getRules();
	ASSERT_EQ(1, rules.size());
	ASSERT_EQ(2, rules[0].attributes.size());
	ASSERT_EQ((int)ui::AttributeName::WIDTH, (int)rules[0].attributes[0].type);
	ASSERT_EQ("50%", rules[0].attributes[0].value);
	ASSERT_EQ((int)ui::AttributeName::HEIGHT, (int)rules[0].attributes[1].type);
	ASSERT_EQ("100", rules[0].attributes[1].value);
	return true;
}

bool testStyleApplication() {
	MockDocument doc;
	ASSERT_PARSE(doc, R"(
		[style] {
			.some_class {
				width: 50%;
			}
		}
		[panel .some_class] {
		}
	)");
	Span<u32> roots = doc.m_roots;
	ASSERT_EQ(1, (int)roots.size());
	const ui::Element& elem = doc.m_elements[roots[0]];
	// Check attributes
	bool has_width = false;
	for (const ui::Attribute& attr : elem.attributes) {
		if (attr.type == ui::AttributeName::WIDTH && attr.value.size() == 3 && memcmp(attr.value.begin, "50%", 3) == 0) {
			has_width = true;
			break;
		}
	}
	ASSERT_EQ(true, has_width);
	return true;
}

}

void runUIStyleTests() {
	RUN_TEST(testEmptyStyleBlock);
	RUN_TEST(testStyleWithRules);
	RUN_TEST(testStyleApplication);
}