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


bool testHoverPseudoclass() {
	MockDocument doc;
	ASSERT_PARSE(doc, R"(
		[style] {
			.some_class:hover {
				bg-color: #ff0000;
			}
		}
	)");
	Span<u32> root_indices = doc.m_roots;
	ASSERT_EQ(0, root_indices.size());
	Span<ui::StyleRule> rules = doc.m_stylesheet.getRules();
	ASSERT_EQ(1, rules.size());
	ASSERT_EQ(1, rules[0].attributes.size());
	ASSERT_EQ((int)ui::AttributeName::BG_COLOR, (int)rules[0].attributes[0].type);
	ASSERT_EQ("#ff0000", rules[0].attributes[0].value);
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
		[box .some_class] {
		}
	)");
	Span<u32> roots = doc.m_roots;
	ASSERT_EQ(1, (int)roots.size());
	const ui::Element& elem = doc.m_elements[roots[0]];

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

bool testInlineOverridesStylesheet() {
	MockDocument doc;
	ASSERT_PARSE(doc, R"(
		[style] {
			.some_class {
				width: 50%;
			}
		}
		[box .some_class width=75%] {
		}
	)");
	Span<u32> roots = doc.m_roots;
	ASSERT_EQ(1, (int)roots.size());
	const ui::Element& elem = doc.m_elements[roots[0]];

	bool has_correct_width = false;
	for (const ui::Attribute& attr : elem.attributes) {
		if (attr.type == ui::AttributeName::WIDTH && attr.value.size() == 3 && memcmp(attr.value.begin, "75%", 3) == 0) {
			has_correct_width = true;
			break;
		}
	}
	ASSERT_EQ(true, has_correct_width);
	return true;
}

bool testMultipleClassesMatching() {
	MockDocument doc;
	ASSERT_PARSE(doc, R"(
		[style] {
			.class1 {
				width: 50%;
			}
			.class2 {
				height: 100;
			}
		}
		[box .class1 .class2] {
		}
	)");
	Span<u32> roots = doc.m_roots;
	ASSERT_EQ(1, (int)roots.size());
	const ui::Element& elem = doc.m_elements[roots[0]];

	bool has_width = false;
	bool has_height = false;
	for (const ui::Attribute& attr : elem.attributes) {
		if (attr.type == ui::AttributeName::WIDTH && equalStrings(attr.value, "50%")) {
			has_width = true;
		}
		if (attr.type == ui::AttributeName::HEIGHT && equalStrings(attr.value, "100")) {
			has_height = true;
		}
	}
	ASSERT_EQ(true, has_width);
	ASSERT_EQ(true, has_height);
	return true;
}

bool testClassNotMatching() {
	MockDocument doc;
	ASSERT_PARSE(doc, R"(
		[style] {
			.missing {
				width: 50%;
			}
		}
		[box .present] {
		}
	)");
	Span<u32> roots = doc.m_roots;
	ASSERT_EQ(1, (int)roots.size());
	const ui::Element& elem = doc.m_elements[roots[0]];
	bool has_width = false;
	for (const ui::Attribute& attr : elem.attributes) {
		if (attr.type == ui::AttributeName::WIDTH) {
			has_width = true;
		}
	}
	ASSERT_EQ(false, has_width);
	return true;
}

bool testRecomputeIdempotence() {
	MockDocument doc;
	ASSERT_PARSE(doc, R"(
		[style] {
			.some_class {
				width: 50%;
				height: 100;
			}
		}
		[box .some_class] {
		}
	)");
	Span<u32> roots = doc.m_roots;
	ASSERT_EQ(1, (int)roots.size());
	const ui::Element& elem = doc.m_elements[roots[0]];
	
	Array<ui::Attribute> original_attrs(doc.m_allocator);
	for (const ui::Attribute& attr : elem.attributes) {
		original_attrs.emplace(attr);
	}
	
	doc.recomputeStyles();
	
	ASSERT_EQ(original_attrs.size(), elem.attributes.size());
	for (size_t i = 0; i < original_attrs.size(); ++i) {
		ASSERT_EQ((int)original_attrs[(u32)i].type, (int)elem.attributes[(u32)i].type);
		ASSERT_EQ(original_attrs[(u32)i].value, elem.attributes[(u32)i].value);
	}
	
	return true;
}

bool testAddClassWithFontAttribute() {
	MockDocument doc;
	ASSERT_PARSE(doc, R"(
		[style] {
			.large_font {
				font: "/engine/editor/fonts/JetBrainsMono-Regular.ttf";
				font-size: 28;
			}
		}
		[box] {
		}
	)");
	Span<u32> roots = doc.m_roots;
	ASSERT_EQ(1, (int)roots.size());
	ui::Element& elem = doc.m_elements[roots[0]];
	
	float initial_font_size = elem.font_size;
	
	doc.addClass(roots[0], "large_font");
	
	ASSERT_FLOAT_EQ(28.0f, elem.font_size);
	
	return true;
}

bool testRemoveClassWithFontAttribute() {
	MockDocument doc;
	ASSERT_PARSE(doc, R"(
		[style] {
			.large_font {
				font: "/engine/editor/fonts/JetBrainsMono-Regular.ttf";
				font-size: 28;
			}
		}
		[box .large_font] {
		}
	)");
	Span<u32> roots = doc.m_roots;
	ASSERT_EQ(1, (int)roots.size());
	ui::Element& elem = doc.m_elements[roots[0]];
	
	ASSERT_FLOAT_EQ(28.0f, elem.font_size);
	
	doc.removeClass(roots[0], "large_font");
	
	ASSERT_TRUE(elem.font_size != 28.0f);
	
	return true;
}

bool testAddClassDuplicateNoOp() {
	MockDocument doc;
	ASSERT_PARSE(doc, R"(
		[style] {
			.test_class {
				width: 50%;
			}
		}
		[box] {
		}
	)");
	Span<u32> roots = doc.m_roots;
	ASSERT_EQ(1, (int)roots.size());
	ui::Element& elem = doc.m_elements[roots[0]];
	
	bool has_width = false;
	for (const ui::Attribute& attr : elem.attributes) {
		if (attr.type == ui::AttributeName::WIDTH) {
			has_width = true;
		}
	}
	ASSERT_EQ(false, has_width);
	
	doc.addClass(roots[0], "test_class");
	
	has_width = false;
	for (const ui::Attribute& attr : elem.attributes) {
		if (attr.type == ui::AttributeName::WIDTH && equalStrings(attr.value, "50%")) {
			has_width = true;
		}
	}
	ASSERT_EQ(true, has_width);
	
	doc.addClass(roots[0], "test_class");
	
	has_width = false;
	for (const ui::Attribute& attr : elem.attributes) {
		if (attr.type == ui::AttributeName::WIDTH && equalStrings(attr.value, "50%")) {
			has_width = true;
		}
	}
	ASSERT_EQ(true, has_width);
	
	return true;
}

bool testRemoveClassRemovesEffect() {
	MockDocument doc;
	ASSERT_PARSE(doc, R"(
		[style] {
			.test_class {
				width: 50%;
			}
		}
		[box .test_class] {
		}
	)");
	Span<u32> roots = doc.m_roots;
	ASSERT_EQ(1, (int)roots.size());
	ui::Element& elem = doc.m_elements[roots[0]];
	
	bool has_width = false;
	for (const ui::Attribute& attr : elem.attributes) {
		if (attr.type == ui::AttributeName::WIDTH && equalStrings(attr.value, "50%")) {
			has_width = true;
		}
	}
	ASSERT_EQ(true, has_width);
	
	doc.removeClass(roots[0], "test_class");
	
	has_width = false;
	for (const ui::Attribute& attr : elem.attributes) {
		if (attr.type == ui::AttributeName::WIDTH) {
			has_width = true;
		}
	}
	ASSERT_EQ(false, has_width);
	
	return true;
}

bool testRemoveAbsentClassNoOp() {
	MockDocument doc;
	ASSERT_PARSE(doc, R"(
		[style] {
			.present {
				width: 50%;
			}
		}
		[box .present] {
		}
	)");
	Span<u32> roots = doc.m_roots;
	ASSERT_EQ(1, (int)roots.size());
	ui::Element& elem = doc.m_elements[roots[0]];
	
	bool has_width = false;
	for (const ui::Attribute& attr : elem.attributes) {
		if (attr.type == ui::AttributeName::WIDTH && equalStrings(attr.value, "50%")) {
			has_width = true;
		}
	}
	ASSERT_EQ(true, has_width);
	
	doc.removeClass(roots[0], "absent");
	
	has_width = false;
	for (const ui::Attribute& attr : elem.attributes) {
		if (attr.type == ui::AttributeName::WIDTH && equalStrings(attr.value, "50%")) {
			has_width = true;
		}
	}
	ASSERT_EQ(true, has_width);
	
	return true;
}

bool testRemoveClassRetainsOthers() {
	MockDocument doc;
	ASSERT_PARSE(doc, R"(
		[style] {
			.class1 {
				width: 50%;
			}
			.class2 {
				height: 100;
			}
		}
		[box .class1 .class2] {
		}
	)");
	Span<u32> roots = doc.m_roots;
	ASSERT_EQ(1, (int)roots.size());
	ui::Element& elem = doc.m_elements[roots[0]];
	
	bool has_width = false;
	bool has_height = false;
	for (const ui::Attribute& attr : elem.attributes) {
		if (attr.type == ui::AttributeName::WIDTH && equalStrings(attr.value, "50%")) {
			has_width = true;
		}
		if (attr.type == ui::AttributeName::HEIGHT && equalStrings(attr.value, "100")) {
			has_height = true;
		}
	}
	ASSERT_EQ(true, has_width);
	ASSERT_EQ(true, has_height);
	
	doc.removeClass(roots[0], "class1");
	
	has_width = false;
	has_height = false;
	for (const ui::Attribute& attr : elem.attributes) {
		if (attr.type == ui::AttributeName::WIDTH) {
			has_width = true;
		}
		if (attr.type == ui::AttributeName::HEIGHT && equalStrings(attr.value, "100")) {
			has_height = true;
		}
	}
	ASSERT_EQ(false, has_width);
	ASSERT_EQ(true, has_height);
	
	return true;
}

bool testInlineOverridesClass() {
	MockDocument doc;
	ASSERT_PARSE(doc, R"(
		[style] {
			.test_class {
				width: 50%;
			}
		}
		[box .test_class width=75%] {
		}
	)");
	Span<u32> roots = doc.m_roots;
	ASSERT_EQ(1, (int)roots.size());
	const ui::Element& elem = doc.m_elements[roots[0]];
	
	bool has_correct_width = false;
	for (const ui::Attribute& attr : elem.attributes) {
		if (attr.type == ui::AttributeName::WIDTH && equalStrings(attr.value, "75%")) {
			has_correct_width = true;
		}
	}
	ASSERT_EQ(true, has_correct_width);
	return true;
}

bool testMultipleClassPrecedence() {
	MockDocument doc;
	ASSERT_PARSE(doc, R"(
		[style] {
			.class1 {
				width: 50%;
			}
			.class2 {
				width: 75%;
			}
		}
		[box .class1 .class2] {
		}
	)");
	Span<u32> roots = doc.m_roots;
	ASSERT_EQ(1, (int)roots.size());
	const ui::Element& elem = doc.m_elements[roots[0]];
	
	bool has_correct_width = false;
	for (const ui::Attribute& attr : elem.attributes) {
		if (attr.type == ui::AttributeName::WIDTH && equalStrings(attr.value, "75%")) {
			has_correct_width = true;
		}
	}
	ASSERT_EQ(true, has_correct_width);
	return true;
}

bool testAddRemoveStability() {
	MockDocument doc;
	ASSERT_PARSE(doc, R"(
		[style] {
			.class_a {
				width: 50%;
			}
			.class_b {
				height: 100;
			}
		}
		[box] {
		}
	)");
	Span<u32> roots = doc.m_roots;
	ASSERT_EQ(1, (int)roots.size());
	ui::Element& elem = doc.m_elements[roots[0]];
	
	int initial_attr_count = elem.attributes.size();

	doc.addClass(roots[0], "class_a");
	int attr_count_after_add_a = elem.attributes.size();
	bool has_width_50 = false;
	for (const ui::Attribute& attr : elem.attributes) {
		if (attr.type == ui::AttributeName::WIDTH && equalStrings(attr.value, "50%")) {
			has_width_50 = true;
		}
	}
	ASSERT_EQ(true, has_width_50);
	
	doc.addClass(roots[0], "class_b");
	int attr_count_after_add_b = elem.attributes.size();
	bool has_height_100 = false;
	for (const ui::Attribute& attr : elem.attributes) {
		if (attr.type == ui::AttributeName::HEIGHT && equalStrings(attr.value, "100")) {
			has_height_100 = true;
		}
	}
	ASSERT_EQ(true, has_height_100);
	
	doc.removeClass(roots[0], "class_a");
	int attr_count_after_remove_a = elem.attributes.size();
	has_width_50 = false;
	for (const ui::Attribute& attr : elem.attributes) {
		if (attr.type == ui::AttributeName::WIDTH) {
			has_width_50 = true;
		}
	}
	ASSERT_EQ(false, has_width_50);
	has_height_100 = false;
	for (const ui::Attribute& attr : elem.attributes) {
		if (attr.type == ui::AttributeName::HEIGHT && equalStrings(attr.value, "100")) {
			has_height_100 = true;
		}
	}
	ASSERT_EQ(true, has_height_100);
	
	doc.addClass(roots[0], "class_a");
	int attr_count_after_readd_a = elem.attributes.size();
	has_width_50 = false;
	for (const ui::Attribute& attr : elem.attributes) {
		if (attr.type == ui::AttributeName::WIDTH && equalStrings(attr.value, "50%")) {
			has_width_50 = true;
		}
	}
	ASSERT_EQ(true, has_width_50);
	has_height_100 = false;
	for (const ui::Attribute& attr : elem.attributes) {
		if (attr.type == ui::AttributeName::HEIGHT && equalStrings(attr.value, "100")) {
			has_height_100 = true;
		}
	}
	ASSERT_EQ(true, has_height_100);
	
	ASSERT_EQ(attr_count_after_readd_a, attr_count_after_add_b);
	
	return true;
}

bool testCompoundClassSelectorMatchesAllClasses() {
	MockDocument doc;
	ASSERT_PARSE(doc, R"(
		[style] {
			.button.hovered {
				width: 75%;
			}
		}
		[box .button .hovered] {
		}
	)");
	Span<u32> roots = doc.m_roots;
	ASSERT_EQ(1, (int)roots.size());
	const ui::Element& elem = doc.m_elements[roots[0]];

	bool has_width = false;
	for (const ui::Attribute& attr : elem.attributes) {
		if (attr.type == ui::AttributeName::WIDTH && equalStrings(attr.value, "75%")) {
			has_width = true;
		}
	}
	ASSERT_EQ(true, has_width);
	return true;
}

bool testCompoundClassSelectorDoesNotMatchWhenMissingClass() {
	MockDocument doc;
	ASSERT_PARSE(doc, R"(
		[style] {
			.button.hovered {
				width: 75%;
			}
		}
		[box .button] {
		}
	)");
	Span<u32> roots = doc.m_roots;
	ASSERT_EQ(1, (int)roots.size());
	const ui::Element& elem = doc.m_elements[roots[0]];

	bool has_width = false;
	for (const ui::Attribute& attr : elem.attributes) {
		if (attr.type == ui::AttributeName::WIDTH) {
			has_width = true;
		}
	}
	ASSERT_EQ(false, has_width);
	return true;
}

bool testCompoundAndSingleClassRulesBothApply() {
	MockDocument doc;
	ASSERT_PARSE(doc, R"(
		[style] {
			.button {
				height: 100;
			}
			.button.hovered {
				width: 75%;
			}
		}
		[box .button .hovered] {
		}
	)");
	Span<u32> roots = doc.m_roots;
	ASSERT_EQ(1, (int)roots.size());
	const ui::Element& elem = doc.m_elements[roots[0]];

	bool has_width = false;
	bool has_height = false;
	for (const ui::Attribute& attr : elem.attributes) {
		if (attr.type == ui::AttributeName::WIDTH && equalStrings(attr.value, "75%")) {
			has_width = true;
		}
		if (attr.type == ui::AttributeName::HEIGHT && equalStrings(attr.value, "100")) {
			has_height = true;
		}
	}
	ASSERT_EQ(true, has_width);
	ASSERT_EQ(true, has_height);
	return true;
}

}

void runUIStyleTests() {
	logInfo("=== Running UI Style Tests ===");
	RUN_TEST(testEmptyStyleBlock);
	RUN_TEST(testHoverPseudoclass);
	RUN_TEST(testStyleWithRules);
	RUN_TEST(testStyleApplication);
	RUN_TEST(testInlineOverridesStylesheet);
	RUN_TEST(testMultipleClassesMatching);
	RUN_TEST(testClassNotMatching);
	RUN_TEST(testRecomputeIdempotence);
	RUN_TEST(testAddClassWithFontAttribute);
	RUN_TEST(testRemoveClassWithFontAttribute);
	RUN_TEST(testAddClassDuplicateNoOp);
	RUN_TEST(testRemoveClassRemovesEffect);
	RUN_TEST(testRemoveAbsentClassNoOp);
	RUN_TEST(testRemoveClassRetainsOthers);
	RUN_TEST(testInlineOverridesClass);
	RUN_TEST(testMultipleClassPrecedence);
	RUN_TEST(testAddRemoveStability);
	RUN_TEST(testCompoundClassSelectorMatchesAllClasses);
	RUN_TEST(testCompoundClassSelectorDoesNotMatchWhenMissingClass);
	RUN_TEST(testCompoundAndSingleClassRulesBothApply);
}