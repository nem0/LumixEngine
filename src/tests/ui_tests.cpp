#include "core/log.h"
#include "tests/common.h"

using namespace Lumix;

namespace {

bool testDocumentParseEmpty() {
	MockDocument doc;
	ASSERT_PARSE(doc, "");
	return true;
}

bool testDocumentParseSimple() {
	MockDocument doc;
	ASSERT_PARSE(doc, "[panel] {}");
	ASSERT_EQ(1, doc.m_roots.size(), "Parse should parse 1 element");
	
	ASSERT_PARSE(doc, "[input]");
	ASSERT_EQ(1, doc.m_roots.size(), "Parse should parse 1 element");

	ASSERT_PARSE(doc, "[canvas]");
	ASSERT_EQ(1, doc.m_roots.size(), "Parse should parse 1 element");

	ASSERT_PARSE(doc, "[image]");
	ASSERT_EQ(1, doc.m_roots.size(), "Parse image markup should parse 1 element");

	ASSERT_PARSE(doc, "\"text\"");
	ASSERT_EQ(1, doc.m_roots.size(), "Parse text markup should parse 1 element");

	return true;
}

bool testDocumentParseInvalidClosingBrace() {
	MockDocument doc;
	doc.m_suppress_logging = true;
	bool res = doc.parse("}", "test.ui");
	ASSERT_EQ(false, res, "Parse should fail for lone closing brace");
	res = doc.parse("]", "test.ui");
	ASSERT_EQ(false, res, "Parse should fail for lone closing brace");
	return true;
}

bool testDocumentParseNested() {
	MockDocument doc;
	ASSERT_PARSE(doc, "[panel] { [panel] }");
	ASSERT_EQ(1, doc.m_roots.size(), "Parse nested markup should parse 1 root");
	ui::Element* root = doc.getElement(doc.m_roots[0]);
	ASSERT_EQ(1, root->children.size(), "Root should have 1 child");
	ASSERT_TAG(root, PANEL);
	ui::Element* child = doc.getElement(root->children[0]);
	ASSERT_TAG(child, PANEL);
	return true;
}

bool testAttributes() {
	MockDocument doc;
	ASSERT_PARSE(doc, "[panel width=800 height=640 id=\"someid\"] { [panel] {} }");
	ASSERT_EQ(1, doc.m_roots.size(), "Parse attributes should parse 1 root");
	ui::Element* root = doc.getElement(doc.m_roots[0]);
	ASSERT_TAG(root, PANEL);
	ASSERT_EQ(1, root->children.size(), "Root should have 1 child");
	Span<ui::Attribute> attrs = root->attributes;
	ASSERT_EQ(3, attrs.size(), "Root should have 3 attributes");
	ASSERT_ATTRIBUTE(root, 0, WIDTH);
	ASSERT_ATTRIBUTE(root, 1, HEIGHT);
	ASSERT_ATTRIBUTE(root, 2, ID);
	return true;
}

bool testDocumentParseComplexNesting() {
	MockDocument doc;
	ASSERT_PARSE(doc, "[panel] { [panel] { \"text\" } [panel] { \"other panel\" } }");
	ASSERT_EQ(1, doc.m_roots.size(), "Parse complex nested markup should parse 1 root");
	
	ui::Element* root = doc.getElement(doc.m_roots[0]);
	ASSERT_TAG(root, PANEL);
	ASSERT_EQ(2, root->children.size(), "Root should have 2 children");
	
	ui::Element* child0 = doc.getElement(root->children[0]);
	ui::Element* child1 = doc.getElement(root->children[1]);
	ASSERT_TAG(child0, PANEL);
	ASSERT_TAG(child1, PANEL);
	ASSERT_EQ(1, child0->children.size(), "panel > panel[0] should have 1 child");
	ASSERT_EQ(1, child1->children.size(), "panel > panel[1] should have 1 child");
	
	ui::Element* grandchild0 = doc.getElement(child0->children[0]);
	ui::Element* grandchild1 = doc.getElement(child1->children[0]);
	ASSERT_TAG(grandchild0, SPAN);
	ASSERT_TAG(grandchild1, SPAN);
	
	return true;
}



bool testEveryElementAttributes() {
	MockDocument doc;
	ASSERT_PARSE(doc, "[panel id=\"testid\" class=\"testclass\" visible=false font-size=14 font=\"arial.ttf\" color=\"#ffffff\"]");
	ASSERT_EQ(1, doc.m_roots.size(), "Should parse 1 element");
	ui::Element* root = doc.getElement(doc.m_roots[0]);
	Span<ui::Attribute> attrs = root->attributes;
	ASSERT_EQ(5, attrs.size(), "Should have 5 attributes");
	ASSERT_ATTRIBUTE(root, 0, ID);
	ASSERT_EQ("testid", attrs[0].value, "ID value should be testid");
	ASSERT_ATTRIBUTE(root, 1, VISIBLE);
	ASSERT_EQ("false", attrs[1].value, "Visible value should be false");
	ASSERT_ATTRIBUTE(root, 2, FONT_SIZE);
	ASSERT_EQ("14", attrs[2].value, "Font size should be 14");
	ASSERT_ATTRIBUTE(root, 3, FONT);
	ASSERT_EQ("arial.ttf", attrs[3].value, "Font should be arial.ttf");
	ASSERT_ATTRIBUTE(root, 4, COLOR);
	ASSERT_EQ("#ffffff", attrs[4].value, "Color should be #ffffff");
	ASSERT_TRUE(root->style_class == "testclass", "Root css_class should be testclass");
	return true;
}

bool testBlockAttributes() {
	MockDocument doc;
	ASSERT_PARSE(doc, "[panel width=50% height=200 margin=10 padding=5]");
	ASSERT_EQ(1, doc.m_roots.size(), "Should parse 1 element");
	ui::Element* root = doc.getElement(doc.m_roots[0]);
	Span<ui::Attribute> attrs = root->attributes;
	ASSERT_EQ(4, attrs.size(), "Should have 4 attributes");
	ASSERT_ATTRIBUTE(root, 0, WIDTH);
	ASSERT_EQ("50%", attrs[0].value, "Width should be 50%");
	ASSERT_ATTRIBUTE(root, 1, HEIGHT);
	ASSERT_EQ("200", attrs[1].value, "Height should be 200");
	ASSERT_ATTRIBUTE(root, 2, MARGIN);
	ASSERT_EQ("10", attrs[2].value, "Margin should be 10");
	ASSERT_ATTRIBUTE(root, 3, PADDING);
	ASSERT_EQ("5", attrs[3].value, "Padding should be 5");
	return true;
}

bool testPanelAttributes() {
	MockDocument doc;
	ASSERT_PARSE(doc, "[panel background-image=\"bg.png\" background-fit=cover bg-color=#000000 direction=column wrap=true justify-content=center]");
	ASSERT_EQ(1, doc.m_roots.size(), "Should parse 1 element");
	ui::Element* root = doc.getElement(doc.m_roots[0]);
	Span<ui::Attribute> attrs = root->attributes;
	ASSERT_EQ(6, attrs.size(), "Should have 6 attributes");
	ASSERT_ATTRIBUTE(root, 0, BACKGROUND_IMAGE);
	ASSERT_EQ("bg.png", attrs[0].value, "Background image should be bg.png");
	ASSERT_ATTRIBUTE(root, 1, BACKGROUND_FIT);
	ASSERT_EQ("cover", attrs[1].value, "Background fit should be cover");
	ASSERT_ATTRIBUTE(root, 2, BG_COLOR);
	ASSERT_EQ("#000000", attrs[2].value, "Background color should be #000000");
	ASSERT_ATTRIBUTE(root, 3, DIRECTION);
	ASSERT_EQ("column", attrs[3].value, "Direction should be column");
	ASSERT_ATTRIBUTE(root, 4, WRAP);
	ASSERT_EQ("true", attrs[4].value, "Wrap should be true");
	ASSERT_ATTRIBUTE(root, 5, JUSTIFY_CONTENT);
	ASSERT_EQ("center", attrs[5].value, "Justify content should be center");
	return true;
}

bool testImageAttributes() {
	MockDocument doc;
	ASSERT_PARSE(doc, "[image src=\"img.png\" fit=cover]");
	ASSERT_EQ(1, doc.m_roots.size(), "Should parse 1 element");
	ui::Element* root = doc.getElement(doc.m_roots[0]);
	Span<ui::Attribute> attrs = root->attributes;
	ASSERT_EQ(2, attrs.size(), "Should have 2 attributes");
	ASSERT_ATTRIBUTE(root, 0, SRC);
	ASSERT_EQ("img.png", attrs[0].value, "Src should be img.png");
	ASSERT_ATTRIBUTE(root, 1, FIT);
	ASSERT_EQ("cover", attrs[1].value, "Fit should be cover");
	return true;
}

bool testInputAttributes() {
	MockDocument doc;
	ASSERT_PARSE(doc, "[input value=\"val\" placeholder=\"ph\"]");
	ASSERT_EQ(1, doc.m_roots.size(), "Should parse 1 element");
	ui::Element* root = doc.getElement(doc.m_roots[0]);
	Span<ui::Attribute> attrs = root->attributes;
	ASSERT_EQ(1, attrs.size(), "Should have 1 attribute");
	ASSERT_ATTRIBUTE(root, 0, PLACEHOLDER);
	ASSERT_EQ("ph", attrs[0].value, "Placeholder should be ph");
	ASSERT_EQ("val", root->value, "Value should be val");
	return true;
}

} // namespace

/*
	TODO: Missing UI Tests (based on docs/ui/)

  - [ ] Selector matching (type, .class, #id, parent > child)
  - [ ] Style precedence rules (inline > #id > .class > type > stylesheet order)
  - [ ] Style inheritance for attributes like font, font-size, color, visibility
  - [ ] Style application to elements and attribute overrides
  - [ ] wrap behavior for overflowing children
  - [ ] Z-order and implicit stacking based on tree order
  - [ ] Canvas element functionality
  - [ ] Button element behavior (though interaction may require runtime tests)
  - [ ] Error handling for invalid syntax
  - [ ] Visibility attribute effects
  - [ ] Font-related attributes (though may require rendering context)
  - [ ] Layout algorithm step-by-step verification
  - [ ] Hit-testing order based on z-order rules
*/

bool testDefaultValues() {
	MockDocument doc;
	ASSERT_PARSE(doc, "[panel] {}");
	ASSERT_EQ(1, doc.m_roots.size(), "Should parse 1 element");
	ui::Element* elem = doc.getElement(doc.m_roots[0]);
	
	// Compute layout to apply defaults
	doc.computeLayout(Vec2(800, 600));
	
	// Check default layout values
	ASSERT_TRUE(elem->fit_content_width, "Default width should be fit-content");
	ASSERT_TRUE(elem->fit_content_height, "Default height should be fit-content");
	ASSERT_FLOAT_EQ(0.0f, elem->margins[0], "Default margin top should be 0");
	ASSERT_FLOAT_EQ(0.0f, elem->margins[1], "Default margin right should be 0");
	ASSERT_FLOAT_EQ(0.0f, elem->margins[2], "Default margin bottom should be 0");
	ASSERT_FLOAT_EQ(0.0f, elem->margins[3], "Default margin left should be 0");
	ASSERT_FLOAT_EQ(0.0f, elem->paddings[0], "Default padding top should be 0");
	ASSERT_FLOAT_EQ(0.0f, elem->paddings[1], "Default padding right should be 0");
	ASSERT_FLOAT_EQ(0.0f, elem->paddings[2], "Default padding bottom should be 0");
	ASSERT_FLOAT_EQ(0.0f, elem->paddings[3], "Default padding left should be 0");
	ASSERT_EQ((int)ui::Direction::COLUMN, (int)elem->direction, "Default direction should be column");
	
	// Check that no attributes are stored for defaults
	Span<ui::Attribute> attrs = elem->attributes;
	ASSERT_EQ(0, attrs.size(), "Element with no attributes should have 0 stored attributes");
	
	return true;
}

bool testSpanAndQuotedStringEquivalence() {
	MockDocument doc1;
	ASSERT_PARSE(doc1, "[panel] { [span value=\"hello\"] }");
	ASSERT_EQ(1, doc1.m_roots.size(), "Should parse 1 root element");
	ui::Element* root1 = doc1.getElement(doc1.m_roots[0]);
	ASSERT_EQ(1, root1->children.size(), "Root should have 1 child");
	ui::Element* child1 = doc1.getElement(root1->children[0]);
	ASSERT_TAG(child1, SPAN);
	Span<ui::Attribute> attrs1 = child1->attributes;
	ASSERT_EQ(0, attrs1.size(), "Span should have 0 attributes");
	ASSERT_EQ("hello", child1->value, "Value should be hello");

	MockDocument doc2;
	ASSERT_PARSE(doc2, "[panel] { \"hello\" }");
	ASSERT_EQ(1, doc2.m_roots.size(), "Should parse 1 root element");
	ui::Element* root2 = doc2.getElement(doc2.m_roots[0]);
	ASSERT_EQ(1, root2->children.size(), "Root should have 1 child");
	ui::Element* child2 = doc2.getElement(root2->children[0]);
	ASSERT_TAG(child2, SPAN);
	Span<ui::Attribute> attrs2 = child2->attributes;
	ASSERT_EQ(0, attrs2.size(), "Quoted string should have 0 attributes");
	ASSERT_EQ("hello", child2->value, "Value should be hello");

	return true;
}

bool testFontAttribute() {
	// Test font attribute on span element
	MockDocument doc;
	ASSERT_PARSE(doc, "[panel font=\"arial.ttf\"] { [span value=\"hello\" font=\"times.ttf\"] }");
	ASSERT_EQ(1, doc.m_roots.size(), "Should parse 1 root element");
	ui::Element* root = doc.getElement(doc.m_roots[0]);
	ASSERT_EQ(1, root->children.size(), "Root should have 1 child");
	
	// Check root has font attribute
	Span<ui::Attribute> root_attrs = root->attributes;
	ASSERT_EQ(1, root_attrs.size(), "Root should have 1 attribute");
	ASSERT_ATTRIBUTE(root, 0, FONT);
	ASSERT_EQ("arial.ttf", root_attrs[0].value, "Root font should be arial.ttf");
	
	// Check child span has its own font attribute
	ui::Element* span = doc.getElement(root->children[0]);
	ASSERT_TAG(span, SPAN);
	Span<ui::Attribute> span_attrs = span->attributes;
	ASSERT_EQ(1, span_attrs.size(), "Span should have 1 attribute");
	ASSERT_ATTRIBUTE(span, 0, FONT);
	ASSERT_EQ("times.ttf", span_attrs[0].value, "Span font should be times.ttf");
	ASSERT_EQ("hello", span->value, "Value should be hello");
	
	return true;
}

bool testFontSizeAttribute() {
	// Test font-size attribute on span element
	MockDocument doc;
	ASSERT_PARSE(doc, "[panel font-size=16] { [span value=\"hello\" font-size=24] }");
	ASSERT_EQ(1, doc.m_roots.size(), "Should parse 1 root element");
	ui::Element* root = doc.getElement(doc.m_roots[0]);
	ASSERT_EQ(1, root->children.size(), "Root should have 1 child");

	// Check root has font-size attribute
	Span<ui::Attribute> root_attrs = root->attributes;
	ASSERT_EQ(1, root_attrs.size(), "Root should have 1 attribute");
	ASSERT_ATTRIBUTE(root, 0, FONT_SIZE);
	ASSERT_EQ("16", root_attrs[0].value, "Root font-size should be 16");

	// Check child span has its own font-size attribute
	ui::Element* span = doc.getElement(root->children[0]);
	ASSERT_TAG(span, SPAN);
	Span<ui::Attribute> span_attrs = span->attributes;
	ASSERT_EQ(1, span_attrs.size(), "Span should have 1 attribute");
	ASSERT_ATTRIBUTE(span, 0, FONT_SIZE);
	ASSERT_EQ("24", span_attrs[0].value, "Span font-size should be 24");
	ASSERT_EQ("hello", span->value, "Value should be hello");

	return true;
}

bool testFontInheritance() {
	MockDocument doc;
	ASSERT_PARSE(doc, "[panel font=\"arial.ttf\"] { \"hello\" }");
	ASSERT_EQ(1, doc.m_roots.size(), "Should parse 1 root element");
	ui::Element* root = doc.getElement(doc.m_roots[0]);
	doc.computeLayout(Vec2(800, 600));

	// Check if root has font
	ASSERT_TRUE(root->font_handle != nullptr, "Root should have font");

	// Check if child span inherits font
	ui::Element* span = doc.getElement(root->children[0]);
	ASSERT_TRUE(span->font_handle != nullptr, "Span should inherit font from parent");
	ASSERT_TRUE(root->font_handle == span->font_handle, "Span should have the same font as parent");

	return true;
}

bool testFontInheritanceDeep() {
	MockDocument doc;
	ASSERT_PARSE(doc, "[panel font=\"arial.ttf\"] { [panel] { \"hello\" } }");
	ASSERT_EQ(1, doc.m_roots.size(), "Should parse 1 root element");
	ui::Element* root = doc.getElement(doc.m_roots[0]);
	doc.computeLayout(Vec2(800, 600));

	// Check if root has font
	ASSERT_TRUE(root->font_handle != nullptr, "Root should have font");

	// Check if child panel inherits font
	ui::Element* child_panel = doc.getElement(root->children[0]);
	ASSERT_TRUE(child_panel->font_handle != nullptr, "Child panel should inherit font from parent");
	ASSERT_TRUE(root->font_handle == child_panel->font_handle, "Child panel should have the same font as root");

	// Check if grandchild span inherits font
	ui::Element* grandchild_span = doc.getElement(child_panel->children[0]);
	ASSERT_TRUE(grandchild_span->font_handle != nullptr, "Grandchild span should inherit font from root");
	ASSERT_TRUE(root->font_handle == grandchild_span->font_handle, "Grandchild span should have the same font as root");

	return true;
}

bool testColorInheritance() {
	MockDocument doc;
	ASSERT_PARSE(doc, "[panel color=\"#ff0000\"] { \"hello\" }");
	ASSERT_EQ(1, doc.m_roots.size(), "Should parse 1 root element");
	ui::Element* root = doc.getElement(doc.m_roots[0]);
	doc.computeLayout(Vec2(800, 600));

	// Check if root has color set
	ASSERT_TRUE(root->color.r == 255 && root->color.g == 0 && root->color.b == 0 && root->color.a == 255, "Root should have red color");

	// Check if child span inherits color
	ui::Element* span = doc.getElement(root->children[0]);
	ASSERT_TRUE(span->color.r == 255 && span->color.g == 0 && span->color.b == 0 && span->color.a == 255, "Span should inherit red color from parent");
	return true;
}

bool testColorInheritanceDeep() {
	MockDocument doc;
	ASSERT_PARSE(doc, "[panel color=\"#00ff00\"] { [panel] { \"hello\" } }");
	ASSERT_EQ(1, doc.m_roots.size(), "Should parse 1 root element");
	ui::Element* root = doc.getElement(doc.m_roots[0]);
	doc.computeLayout(Vec2(800, 600));

	// Check if root has color set
	ASSERT_TRUE(root->color.r == 0 && root->color.g == 255 && root->color.b == 0 && root->color.a == 255, "Root should have green color");

	// Check if child panel inherits color
	ui::Element* child_panel = doc.getElement(root->children[0]);
	ASSERT_TRUE(child_panel->color.r == 0 && child_panel->color.g == 255 && child_panel->color.b == 0 && child_panel->color.a == 255, "Child panel should inherit green color from root");

	// Check if grandchild span inherits color
	ui::Element* grandchild_span = doc.getElement(child_panel->children[0]);
	ASSERT_TRUE(grandchild_span->color.r == 0 && grandchild_span->color.g == 255 && grandchild_span->color.b == 0 && grandchild_span->color.a == 255, "Grandchild span should inherit green color from root");

	return true;
}

bool testMultilineStringLayout() {
	// Test that multiline strings in UI layout are treated HTML-compatible
	MockDocument doc;
	ASSERT_PARSE(doc, R"(
		[panel width=fit-content height=fit-content font="arial.ttf" font-size=16] {
			"Line 1
			Line 2
			Line 3"
		}
	)");
	doc.computeLayout(Vec2(800, 600));
	ASSERT_EQ(1, doc.m_roots.size(), "Should parse 1 root element");
	ui::Element* panel = doc.getElement(doc.m_roots[0]);
	ASSERT_TAG(panel, PANEL);
	Span<ui::Attribute> attrs = panel->attributes;
	ASSERT_EQ(4, attrs.size(), "Panel should have 4 attributes");

	// Check child text element
	ASSERT_EQ(1, panel->children.size(), "Panel should have 1 child");
	ui::Element* textElem = doc.getElement(panel->children[0]);
	ASSERT_TAG(textElem, SPAN);

	// Assert text element layout (HTML-compatible: newlines don't create line breaks)
	// "Line 1\nLine 2\nLine 3", \n treated as spaces, so 20 chars contribute to width
	// 20 * 8 = 160, single line height = 16
	ASSERT_FLOAT_EQ(160.0f, textElem->size.x, "Text width should treat newlines as spaces");
	ASSERT_FLOAT_EQ(16.0f, textElem->size.y, "Text height should be single line (HTML-compatible)");
	ASSERT_FLOAT_EQ(0.0f, textElem->position.x, "Text x position should be 0");
	ASSERT_FLOAT_EQ(0.0f, textElem->position.y, "Text y position should be 0");

	// Assert panel layout (fits the text)
	ASSERT_FLOAT_EQ(160.0f, panel->size.x, "Panel width should fit text width");
	ASSERT_FLOAT_EQ(16.0f, panel->size.y, "Panel height should fit text height");

	return true;
}

bool testMultilineStringMeasurement() {
	// Test that measureTextA skips \n and \r characters
	MockFontManager font_manager;
	MockFontManager::FontHandle font = font_manager.loadFont("test.ttf", 16);

	// Test normal text
	Vec2 size1 = font_manager.measureTextA(font, "hello");
	ASSERT_FLOAT_EQ(40.0f, size1.x, "Normal text width should be 5 * 8");
	ASSERT_FLOAT_EQ(16.0f, size1.y, "Height should be font size");

	// Test text with newlines
	Vec2 size2 = font_manager.measureTextA(font, "hel\nlo");
	ASSERT_FLOAT_EQ(48.0f, size2.x, "Text with newline treated as space, width = 6 * 8");
	ASSERT_FLOAT_EQ(16.0f, size2.y, "Height should be font size");

	// Test text with carriage return
	Vec2 size3 = font_manager.measureTextA(font, "hel\rlo");
	ASSERT_FLOAT_EQ(40.0f, size3.x, "Text with \\r skipped, width = 5 * 8");
	ASSERT_FLOAT_EQ(16.0f, size3.y, "Height should be font size");

	// Test text with both
	Vec2 size4 = font_manager.measureTextA(font, "h\n\r\nel\rlo");
	ASSERT_FLOAT_EQ(48.0f, size4.x, "Text with \\r skipped, \\n treated as space, width = 6 * 8");
	ASSERT_FLOAT_EQ(16.0f, size4.y, "Height should be font size");

	return true;
}

void runUITests() {
	RUN_TEST(testDocumentParseEmpty);
	RUN_TEST(testDocumentParseSimple);
	RUN_TEST(testDocumentParseInvalidClosingBrace);
	RUN_TEST(testDocumentParseNested);
	RUN_TEST(testDocumentParseComplexNesting);
	RUN_TEST(testAttributes);
	RUN_TEST(testEveryElementAttributes);
	RUN_TEST(testBlockAttributes);
	RUN_TEST(testPanelAttributes);
	RUN_TEST(testImageAttributes);
	RUN_TEST(testInputAttributes);
	RUN_TEST(testDefaultValues);
	RUN_TEST(testSpanAndQuotedStringEquivalence);
	RUN_TEST(testFontAttribute);
	RUN_TEST(testFontSizeAttribute);
	RUN_TEST(testFontInheritance);
	RUN_TEST(testFontInheritanceDeep);
	RUN_TEST(testColorInheritance);
	RUN_TEST(testColorInheritanceDeep);
	RUN_TEST(testMultilineStringMeasurement);
	RUN_TEST(testMultilineStringLayout);
}