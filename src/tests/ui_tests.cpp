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
	ASSERT_EQ(1, doc.m_roots.size());
	
	ASSERT_PARSE(doc, "[image]");
	ASSERT_EQ(1, doc.m_roots.size());

	ASSERT_PARSE(doc, "[span]");
	ASSERT_EQ(1, doc.m_roots.size());

	ASSERT_PARSE(doc, "text");
	ASSERT_EQ(1, doc.m_roots.size());

	return true;
}

bool testDocumentParseInvalidClosingBrace() {
	MockDocument doc;
	doc.m_suppress_logging = true;
	bool res = doc.parse("}", "test.ui");
	ASSERT_EQ(false, res);
	res = doc.parse("]", "test.ui");
	ASSERT_EQ(false, res);
	return true;
}

bool testDocumentParseNested() {
	MockDocument doc;
	ASSERT_PARSE(doc, "[panel] { [panel] }");
	ASSERT_EQ(1, doc.m_roots.size());
	ui::Element* root = doc.getElement(doc.m_roots[0]);
	ASSERT_EQ(1, root->children.size());
	ASSERT_TAG(root, PANEL);
	ui::Element* child = doc.getElement(root->children[0]);
	ASSERT_TAG(child, PANEL);
	return true;
}

bool testAttributes() {
	MockDocument doc;
	ASSERT_PARSE(doc, "[panel width=800 height=640 id=\"someid\"] { [panel] {} }");
	ASSERT_EQ(1, doc.m_roots.size());
	ui::Element* root = doc.getElement(doc.m_roots[0]);
	ASSERT_TAG(root, PANEL);
	ASSERT_EQ(1, root->children.size());
	Span<ui::Attribute> attrs = root->attributes;
	ASSERT_EQ(3, attrs.size());
	ASSERT_ATTRIBUTE(root, 0, WIDTH);
	ASSERT_ATTRIBUTE(root, 1, HEIGHT);
	ASSERT_ATTRIBUTE(root, 2, ID);
	return true;
}

bool testDocumentParseComplexNesting() {
	MockDocument doc;
	ASSERT_PARSE(doc, "[panel] { [panel] { text } [panel] { other panel } }");
	ASSERT_EQ(1, doc.m_roots.size());
	
	ui::Element* root = doc.getElement(doc.m_roots[0]);
	ASSERT_TAG(root, PANEL);
	ASSERT_EQ(2, root->children.size());
	
	ui::Element* child0 = doc.getElement(root->children[0]);
	ui::Element* child1 = doc.getElement(root->children[1]);
	ASSERT_TAG(child0, PANEL);
	ASSERT_TAG(child1, PANEL);
	ASSERT_EQ(1, child0->children.size());
	ASSERT_EQ(1, child1->children.size());
	
	ui::Element* grandchild0 = doc.getElement(child0->children[0]);
	ui::Element* grandchild1 = doc.getElement(child1->children[0]);
	ASSERT_TAG(grandchild0, SPAN);
	ASSERT_TAG(grandchild1, SPAN);
	
	return true;
}



bool testEveryElementAttributes() {
	MockDocument doc;
	ASSERT_PARSE(doc, "[panel id=\"testid\" .testclass visible=false font-size=14 font=\"arial.ttf\" color=\"#ffffff\"]");
	ASSERT_EQ(1, doc.m_roots.size());
	ui::Element* root = doc.getElement(doc.m_roots[0]);
	Span<ui::Attribute> attrs = root->attributes;
	ASSERT_EQ(5, attrs.size());
	ASSERT_ATTRIBUTE(root, 0, ID);
	ASSERT_EQ("testid", attrs[0].value);
	ASSERT_ATTRIBUTE(root, 1, VISIBLE);
	ASSERT_EQ("false", attrs[1].value);
	ASSERT_ATTRIBUTE(root, 2, FONT_SIZE);
	ASSERT_EQ("14", attrs[2].value);
	ASSERT_ATTRIBUTE(root, 3, FONT);
	ASSERT_EQ("arial.ttf", attrs[3].value);
	ASSERT_ATTRIBUTE(root, 4, COLOR);
	ASSERT_EQ("#ffffff", attrs[4].value);
	ASSERT_TRUE(root->style_class == "testclass");
	return true;
}

bool testBlockAttributes() {
	MockDocument doc;
	ASSERT_PARSE(doc, "[panel width=50% height=200 margin=10 padding=5]");
	ASSERT_EQ(1, doc.m_roots.size());
	ui::Element* root = doc.getElement(doc.m_roots[0]);
	Span<ui::Attribute> attrs = root->attributes;
	ASSERT_EQ(4, attrs.size());
	ASSERT_ATTRIBUTE(root, 0, WIDTH);
	ASSERT_EQ("50%", attrs[0].value);
	ASSERT_ATTRIBUTE(root, 1, HEIGHT);
	ASSERT_EQ("200", attrs[1].value);
	ASSERT_ATTRIBUTE(root, 2, MARGIN);
	ASSERT_EQ("10", attrs[2].value);
	ASSERT_ATTRIBUTE(root, 3, PADDING);
	ASSERT_EQ("5", attrs[3].value);
	return true;
}

bool testPanelAttributes() {
	MockDocument doc;
	ASSERT_PARSE(doc, "[panel bg-image=\"bg.png\" bg-fit=cover bg-color=#000000 direction=column wrap=true justify-content=center]");
	ASSERT_EQ(1, doc.m_roots.size());
	ui::Element* root = doc.getElement(doc.m_roots[0]);
	Span<ui::Attribute> attrs = root->attributes;
	ASSERT_EQ(6, attrs.size());
	ASSERT_ATTRIBUTE(root, 0, BACKGROUND_IMAGE);
	ASSERT_EQ("bg.png", attrs[0].value);
	ASSERT_ATTRIBUTE(root, 1, BACKGROUND_FIT);
	ASSERT_EQ("cover", attrs[1].value);
	ASSERT_ATTRIBUTE(root, 2, BG_COLOR);
	ASSERT_EQ("#000000", attrs[2].value);
	ASSERT_ATTRIBUTE(root, 3, DIRECTION);
	ASSERT_EQ("column", attrs[3].value);
	ASSERT_ATTRIBUTE(root, 4, WRAP);
	ASSERT_EQ("true", attrs[4].value);
	ASSERT_ATTRIBUTE(root, 5, JUSTIFY_CONTENT);
	ASSERT_EQ("center", attrs[5].value);
	return true;
}

bool testImageAttributes() {
	MockDocument doc;
	ASSERT_PARSE(doc, "[image src=\"img.png\" fit=cover]");
	ASSERT_EQ(1, doc.m_roots.size());
	ui::Element* root = doc.getElement(doc.m_roots[0]);
	Span<ui::Attribute> attrs = root->attributes;
	ASSERT_EQ(2, attrs.size());
	ASSERT_ATTRIBUTE(root, 0, SRC);
	ASSERT_EQ("img.png", attrs[0].value);
	ASSERT_ATTRIBUTE(root, 1, FIT);
	ASSERT_EQ("cover", attrs[1].value);
	return true;
}

bool testDefaultValues() {
	MockDocument doc;
	ASSERT_PARSE(doc, "[panel] {}");
	ASSERT_EQ(1, doc.m_roots.size());
	ui::Element* elem = doc.getElement(doc.m_roots[0]);
	
	// Compute layout to apply defaults
	doc.computeLayout(Vec2(800, 600));
	
	// Check default layout values
	ASSERT_FLOAT_EQ(0.0f, elem->margins.top);
	ASSERT_FLOAT_EQ(0.0f, elem->margins.right);
	ASSERT_FLOAT_EQ(0.0f, elem->margins.bottom);
	ASSERT_FLOAT_EQ(0.0f, elem->margins.left);
	ASSERT_FLOAT_EQ(0.0f, elem->paddings.top);
	ASSERT_FLOAT_EQ(0.0f, elem->paddings.right);
	ASSERT_FLOAT_EQ(0.0f, elem->paddings.bottom);
	ASSERT_FLOAT_EQ(0.0f, elem->paddings.left);
	ASSERT_EQ((int)ui::Direction::COLUMN, (int)elem->direction);
	
	// Check that no attributes are stored for defaults
	Span<ui::Attribute> attrs = elem->attributes;
	ASSERT_EQ(0, attrs.size());
	
	return true;
}

bool testSpanAndQuotedStringEquivalence() {
	MockDocument doc1;
	ASSERT_PARSE(doc1, "[panel] { [span value=\"hello\"] }");
	ASSERT_EQ(1, doc1.m_roots.size());
	ui::Element* root1 = doc1.getElement(doc1.m_roots[0]);
	ASSERT_EQ(1, root1->children.size());
	ui::Element* child1 = doc1.getElement(root1->children[0]);
	ASSERT_TAG(child1, SPAN);
	Span<ui::Attribute> attrs1 = child1->attributes;
	ASSERT_EQ(0, attrs1.size());
	ASSERT_EQ("hello", child1->value);

	MockDocument doc2;
	ASSERT_PARSE(doc2, "[panel] { hello }");
	ASSERT_EQ(1, doc2.m_roots.size());
	ui::Element* root2 = doc2.getElement(doc2.m_roots[0]);
	ASSERT_EQ(1, root2->children.size());
	ui::Element* child2 = doc2.getElement(root2->children[0]);
	ASSERT_TAG(child2, SPAN);
	Span<ui::Attribute> attrs2 = child2->attributes;
	ASSERT_EQ(0, attrs2.size());
	ASSERT_EQ("hello", child2->value);

	return true;
}

bool testSpanEmptyValue() {
	MockDocument doc;
	ASSERT_PARSE(doc, "[panel] { [span value=\"\"] }");
	ASSERT_EQ(1, doc.m_roots.size());
	ui::Element* root = doc.getElement(doc.m_roots[0]);
	ASSERT_EQ(1, root->children.size());
	ui::Element* child = doc.getElement(root->children[0]);
	ASSERT_TAG(child, SPAN);
	Span<ui::Attribute> attrs = child->attributes;
	ASSERT_EQ(0, attrs.size());
	ASSERT_EQ("", child->value);

	return true;
}

bool testFontAttribute() {
	// Test font attribute on span element
	MockDocument doc;
	ASSERT_PARSE(doc, "[panel font=\"arial.ttf\"] { [span value=\"hello\" font=\"times.ttf\"] }");
	ASSERT_EQ(1, doc.m_roots.size());
	ui::Element* root = doc.getElement(doc.m_roots[0]);
	ASSERT_EQ(1, root->children.size());
	
	// Check root has font attribute
	Span<ui::Attribute> root_attrs = root->attributes;
	ASSERT_EQ(1, root_attrs.size());
	ASSERT_ATTRIBUTE(root, 0, FONT);
	ASSERT_EQ("arial.ttf", root_attrs[0].value);
	
	// Check child span has its own font attribute
	ui::Element* span = doc.getElement(root->children[0]);
	ASSERT_TAG(span, SPAN);
	Span<ui::Attribute> span_attrs = span->attributes;
	ASSERT_EQ(1, span_attrs.size());
	ASSERT_ATTRIBUTE(span, 0, FONT);
	ASSERT_EQ("times.ttf", span_attrs[0].value);
	ASSERT_EQ("hello", span->value);
	
	return true;
}

bool testFontSizeAttribute() {
	// Test font-size attribute on span element
	MockDocument doc;
	ASSERT_PARSE(doc, "[panel font-size=16] { [span value=\"hello\" font-size=24] }");
	ASSERT_EQ(1, doc.m_roots.size());
	ui::Element* root = doc.getElement(doc.m_roots[0]);
	ASSERT_EQ(1, root->children.size());

	// Check root has font-size attribute
	Span<ui::Attribute> root_attrs = root->attributes;
	ASSERT_EQ(1, root_attrs.size());
	ASSERT_ATTRIBUTE(root, 0, FONT_SIZE);
	ASSERT_EQ("16", root_attrs[0].value);

	// Check child span has its own font-size attribute
	ui::Element* span = doc.getElement(root->children[0]);
	ASSERT_TAG(span, SPAN);
	Span<ui::Attribute> span_attrs = span->attributes;
	ASSERT_EQ(1, span_attrs.size());
	ASSERT_ATTRIBUTE(span, 0, FONT_SIZE);
	ASSERT_EQ("24", span_attrs[0].value);
	ASSERT_EQ("hello", span->value);

	return true;
}

bool testFontInheritance() {
	MockDocument doc;
	ASSERT_PARSE(doc, "[panel font=\"arial.ttf\"] { hello }");
	ASSERT_EQ(1, doc.m_roots.size());
	ui::Element* root = doc.getElement(doc.m_roots[0]);
	doc.computeLayout(Vec2(800, 600));

	// Check if child span inherits font
	ui::Element* span = doc.getElement(root->children[0]);
	ASSERT_TRUE(span->font_handle != nullptr);

	return true;
}

bool testFontInheritanceDeep() {
	MockDocument doc;
	ASSERT_PARSE(doc, "[panel font=\"arial.ttf\"] { [panel] { hello } }");
	ASSERT_EQ(1, doc.m_roots.size());
	ui::Element* root = doc.getElement(doc.m_roots[0]);
	doc.computeLayout(Vec2(800, 600));

	ui::Element* child_panel = doc.getElement(root->children[0]);
	ui::Element* grandchild_span = doc.getElement(child_panel->children[0]);
	ASSERT_TRUE(grandchild_span->font_handle != nullptr);

	return true;
}

bool testColorInheritance() {
	MockDocument doc;
	ASSERT_PARSE(doc, "[panel color=\"#ff0000\"] { \"hello\" }");
	ASSERT_EQ(1, doc.m_roots.size());
	ui::Element* root = doc.getElement(doc.m_roots[0]);
	doc.computeLayout(Vec2(800, 600));

	// Check if root has color set
	ASSERT_TRUE(root->color.r == 255 && root->color.g == 0 && root->color.b == 0 && root->color.a == 255);

	// Check if child span inherits color
	ui::Element* span = doc.getElement(root->children[0]);
	ASSERT_TRUE(span->color.r == 255 && span->color.g == 0 && span->color.b == 0 && span->color.a == 255);
	return true;
}

bool testColorInheritanceDeep() {
	MockDocument doc;
	ASSERT_PARSE(doc, "[panel color=\"#00ff00\"] { [panel] { \"hello\" } }");
	ASSERT_EQ(1, doc.m_roots.size());
	ui::Element* root = doc.getElement(doc.m_roots[0]);
	doc.computeLayout(Vec2(800, 600));

	// Check if root has color set
	ASSERT_TRUE(root->color.r == 0 && root->color.g == 255 && root->color.b == 0 && root->color.a == 255);

	// Check if child panel inherits color
	ui::Element* child_panel = doc.getElement(root->children[0]);
	ASSERT_TRUE(child_panel->color.r == 0 && child_panel->color.g == 255 && child_panel->color.b == 0 && child_panel->color.a == 255);

	// Check if grandchild span inherits color
	ui::Element* grandchild_span = doc.getElement(child_panel->children[0]);
	ASSERT_TRUE(grandchild_span->color.r == 0 && grandchild_span->color.g == 255 && grandchild_span->color.b == 0 && grandchild_span->color.a == 255);

	return true;
}

bool testMultilineStringLayout() {
	MockDocument doc;
	ASSERT_PARSE(doc, R"(
		[panel width=fit-content height=fit-content font="arial.ttf" font-size=16] {
			Line 1
			Line 2
			Line 3
		}
	)");
	doc.computeLayout(Vec2(800, 600));
	ASSERT_EQ(1, doc.m_roots.size());
	ui::Element* panel = doc.getElement(doc.m_roots[0]);
	ASSERT_EQ(1, panel->children.size());
	ui::Element* textElem = doc.getElement(panel->children[0]);

	ASSERT_FLOAT_EQ(160.0f, textElem->size.x);
	ASSERT_FLOAT_EQ(16.0f, textElem->size.y);
	ASSERT_EQ(1, textElem->lines.size());
	ASSERT_FLOAT_EQ(0.0f, textElem->lines[0].pos.x);
	ASSERT_FLOAT_EQ(12.8f, textElem->lines[0].pos.y);

	ASSERT_FLOAT_EQ(160.0f, panel->size.x);
	ASSERT_FLOAT_EQ(16.0f, panel->size.y);

	return true;
}

bool testMultilineStringMeasurement() {
	// Test that measureTextA skips \n and \r characters
	MockFontManager font_manager;
	MockFontManager::FontHandle font = font_manager.loadFont("test.ttf", 16);

	// Test normal text
	Vec2 size1 = font_manager.measureTextA(font, "hello");
	ASSERT_FLOAT_EQ(40.0f, size1.x);
	ASSERT_FLOAT_EQ(16.0f, size1.y);

	// Test text with newlines
	Vec2 size2 = font_manager.measureTextA(font, "hel\nlo");
	ASSERT_FLOAT_EQ(48.0f, size2.x);
	ASSERT_FLOAT_EQ(16.0f, size2.y);

	// Test text with carriage return
	Vec2 size3 = font_manager.measureTextA(font, "hel\rlo");
	ASSERT_FLOAT_EQ(40.0f, size3.x);
	ASSERT_FLOAT_EQ(16.0f, size3.y);

	// Test text with both
	Vec2 size4 = font_manager.measureTextA(font, "h\n\r\nel\rlo");
	ASSERT_FLOAT_EQ(48.0f, size4.x);
	ASSERT_FLOAT_EQ(16.0f, size4.y);

	return true;
}

bool testTextWithSpecialChars() {
	MockDocument doc;
	ASSERT_PARSE(doc, "[panel] { ,-=()*&^@! }");
	ASSERT_EQ(1, doc.m_roots.size());
	ui::Element* root = doc.getElement(doc.m_roots[0]);
	ASSERT_TAG(root, PANEL);
	ASSERT_EQ(1, root->children.size());
	ui::Element* child = doc.getElement(root->children[0]);
	ASSERT_TAG(child, SPAN);
	ASSERT_EQ(",-=()*&^@!", child->value);
	return true;
}

bool testSpaceBetweenSpans() {
	MockDocument doc;
	ASSERT_PARSE(doc, "[panel direction=row font=\"arial.ttf\" font-size=16] { [span value=\"hello\"] [span value=\"world\"] }");
	doc.computeLayout(Vec2(800, 600));
	ASSERT_EQ(1, doc.m_roots.size());
	ui::Element* panel = doc.getElement(doc.m_roots[0]);
	ASSERT_EQ(2, panel->children.size());
	ui::Element* span1 = doc.getElement(panel->children[0]);
	ui::Element* span2 = doc.getElement(panel->children[1]);
	ASSERT_EQ(1, span1->lines.size());
	ASSERT_EQ(1, span2->lines.size());
	ASSERT_EQ(span1->lines[0].pos.x + span1->size.x, span2->lines[0].pos.x);
	return true;
}

} // namespace

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
	RUN_TEST(testDefaultValues);
	RUN_TEST(testSpanAndQuotedStringEquivalence);
	RUN_TEST(testSpanEmptyValue);
	RUN_TEST(testFontAttribute);
	RUN_TEST(testFontSizeAttribute);
	RUN_TEST(testFontInheritance);
	RUN_TEST(testFontInheritanceDeep);
	RUN_TEST(testColorInheritance);
	RUN_TEST(testColorInheritanceDeep);
	RUN_TEST(testMultilineStringMeasurement);
	RUN_TEST(testMultilineStringLayout);
	RUN_TEST(testTextWithSpecialChars);
	RUN_TEST(testSpaceBetweenSpans);
}