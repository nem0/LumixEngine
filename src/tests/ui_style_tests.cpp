#include "core/log.h"
#include "tests/common.h"

using namespace Lumix;

namespace {

bool testEmptyStyleBlock() {
	MockDocument doc;
	ASSERT_PARSE(doc, "[style] {}");
	Span<u32> root_indices = doc.m_roots;
	ASSERT_EQ(0, root_indices.size(), "Style block should not create elements");

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
	ASSERT_EQ(0, root_indices.size(), "Style block should not create elements");
	Span<ui::StyleRule> rules = doc.m_stylesheet.getRules();
	ASSERT_EQ(1, rules.size(), "Stylesheet should contain 1 rule");
	ASSERT_EQ(2, rules[0].attributes.size(), "Rule should have 2 attributes");
	ASSERT_EQ((int)ui::AttributeName::WIDTH, (int)rules[0].attributes[0].type, "First attribute type should be WIDTH");
	ASSERT_EQ("50%", rules[0].attributes[0].value, "First attribute value should be 50%");
	ASSERT_EQ((int)ui::AttributeName::HEIGHT, (int)rules[0].attributes[1].type, "Second attribute type should be HEIGHT");
	ASSERT_EQ("100", rules[0].attributes[1].value, "Second attribute value should be 100");
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
	ASSERT_EQ(1, (int)roots.size(), "Should have 1 root element");
	const ui::Element& elem = doc.m_elements[roots[0]];
	// Check attributes
	bool has_width = false;
	for (const ui::Attribute& attr : elem.attributes) {
		if (attr.type == ui::AttributeName::WIDTH && attr.value.size() == 3 && memcmp(attr.value.begin, "50%", 3) == 0) {
			has_width = true;
			break;
		}
	}
	ASSERT_EQ(true, has_width, "Element should have width from stylesheet");
	return true;
}

}

void runUIStyleTests() {
	RUN_TEST(testEmptyStyleBlock);
	RUN_TEST(testStyleWithRules);
	RUN_TEST(testStyleApplication);
}