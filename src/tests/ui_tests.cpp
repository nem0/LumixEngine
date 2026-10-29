#include "core/log.h"
#include "renderer/draw2d.h"
#include <stddef.h>
#include "tests/common.h"

using namespace Lumix;
using namespace ui;

namespace {

bool testDocumentParseEmpty() {
	MockDocument doc;
	ASSERT_PARSE(doc, "");
	return true;
}

bool testInternTableSemantics() {
	MockDocument doc;
	
	u32 id1 = (u32)doc.m_intern_table.intern("class1");
	u32 id2 = (u32)doc.m_intern_table.intern("class1");
	ASSERT_EQ(id1, id2);
	
	u32 id3 = (u32)doc.m_intern_table.intern("class2");
	ASSERT_TRUE(id1 != id3);
	
	StringView resolved = doc.m_intern_table.resolve((InternString)id1);
	ASSERT_EQ(resolved, "class1");
	
	StringView invalid = doc.m_intern_table.resolve(InternString::INVALID);
	ASSERT_EQ(invalid.size(), 0);
	
	return true;
}

bool testDocumentParseSimple() {
	MockDocument doc;
	ASSERT_PARSE(doc, "[box] {}");
	ASSERT_EQ(1, doc.m_root.children.size());

	ASSERT_PARSE(doc, "[span]");
	ASSERT_EQ(1, doc.m_root.children.size());

	ASSERT_PARSE(doc, "[image]");
	ASSERT_EQ(1, doc.m_root.children.size());

	ASSERT_PARSE(doc, "text");
	ASSERT_EQ(1, doc.m_root.children.size());

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
	ASSERT_PARSE(doc, "[box] { [box] }");
	ASSERT_EQ(1, doc.m_root.children.size());
	ui::Element* root = doc.getElement(doc.m_root.children[0]);
	ASSERT_EQ(1, root->children.size());
	ASSERT_TAG(root, BOX);
	ui::Element* child = doc.getElement(root->children[0]);
	ASSERT_TAG(child, BOX);
	return true;
}

bool testAttributes() {
	MockDocument doc;
	ASSERT_PARSE(doc, "[box width=800 height=640 id=\"someid\"] { [box] {} }");
	ASSERT_EQ(1, doc.m_root.children.size());
	ui::Element* root = doc.getElement(doc.m_root.children[0]);
	ASSERT_TAG(root, BOX);
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
	ASSERT_PARSE(doc, "[box] { [box] { text } [box] { other box } }");
	ASSERT_EQ(1, doc.m_root.children.size());
	
	ui::Element* root = doc.getElement(doc.m_root.children[0]);
	ASSERT_TAG(root, BOX);
	ASSERT_EQ(2, root->children.size());
	
	ui::Element* child0 = doc.getElement(root->children[0]);
	ui::Element* child1 = doc.getElement(root->children[1]);
	ASSERT_TAG(child0, BOX);
	ASSERT_TAG(child1, BOX);
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
	ASSERT_PARSE(doc, "[box id=\"testid\" .testclass visible=false opacity=0.75 font-size=14 font=\"arial.ttf\" color=#ffffff]");
	ASSERT_EQ(1, doc.m_root.children.size());
	ui::Element* root = doc.getElement(doc.m_root.children[0]);
	Span<ui::Attribute> attrs = root->attributes;
	ASSERT_EQ(6, attrs.size());
	ASSERT_ATTRIBUTE(root, 0, ID);
	ASSERT_EQ("testid", attrs[0].value);
	ASSERT_ATTRIBUTE(root, 1, VISIBLE);
	ASSERT_EQ(false, attrs[1].visible);
	ASSERT_ATTRIBUTE(root, 2, OPACITY);
	ASSERT_FLOAT_EQ(0.75f, attrs[2].opacity);
	ASSERT_ATTRIBUTE(root, 3, FONT_SIZE);
	ASSERT_EQ(14.f, attrs[3].font_size);
	ASSERT_ATTRIBUTE(root, 4, FONT);
	ASSERT_EQ("arial.ttf", attrs[4].value);
	ASSERT_ATTRIBUTE(root, 5, COLOR);
	ASSERT_EQ(0xff, attrs[5].color.r);
	ASSERT_EQ(0xff, attrs[5].color.g);
	ASSERT_EQ(0xff, attrs[5].color.b);
	ASSERT_EQ(0xff, attrs[5].color.a);
	return true;
}

bool testMultipleClasses() {
	MockDocument doc;
	ASSERT_PARSE(doc, "[box .class1 .class2 .class3]");
	ASSERT_EQ(1, doc.m_root.children.size());
	ui::Element* root = doc.getElement(doc.m_root.children[0]);
	ASSERT_EQ(3, root->classes.size());
	StringView class1 = doc.m_intern_table.resolve(root->classes[0]);
	StringView class2 = doc.m_intern_table.resolve(root->classes[1]);
	StringView class3 = doc.m_intern_table.resolve(root->classes[2]);
	ASSERT_EQ(".class1", class1);
	ASSERT_EQ(".class2", class2);
	ASSERT_EQ(".class3", class3);
	
	return true;
}

bool testSingleClass() {
	MockDocument doc;
	ASSERT_PARSE(doc, "[box .singleclass]");
	ASSERT_EQ(1, doc.m_root.children.size());
	ui::Element* root = doc.getElement(doc.m_root.children[0]);
	ASSERT_EQ(1, root->classes.size());
	StringView class_name = doc.m_intern_table.resolve(root->classes[0]);
	ASSERT_EQ(".singleclass", class_name);
	return true;
}

bool testDuplicateClasses() {
	MockDocument doc;
	ASSERT_PARSE(doc, "[box .dup .other .dup]");
	ASSERT_EQ(1, doc.m_root.children.size());
	ui::Element* root = doc.getElement(doc.m_root.children[0]);
	ASSERT_EQ(2, root->classes.size());
	StringView class1 = doc.m_intern_table.resolve(root->classes[0]);
	StringView class2 = doc.m_intern_table.resolve(root->classes[1]);
	ASSERT_EQ(".dup", class1);
	ASSERT_EQ(".other", class2);
	return true;
}

bool testBlockAttributes() {
	MockDocument doc;
	ASSERT_PARSE(doc, "[box width=50% height=200 margin=10 padding=5]");
	ASSERT_EQ(1, doc.m_root.children.size());
	ui::Element* root = doc.getElement(doc.m_root.children[0]);
	Span<ui::Attribute> attrs = root->attributes;
	ASSERT_EQ(4, attrs.size());
	ASSERT_ATTRIBUTE(root, 0, WIDTH);
	ASSERT_FLOAT_EQ(50.0f, attrs[0].parsed_unit.value);
	ASSERT_EQ((int)ui::Unit::PERCENT, (int)attrs[0].parsed_unit.unit);
	ASSERT_ATTRIBUTE(root, 1, HEIGHT);
	ASSERT_FLOAT_EQ(200.0f, attrs[1].parsed_unit.value);
	ASSERT_EQ((int)ui::Unit::PIXELS, (int)attrs[1].parsed_unit.unit);
	ASSERT_ATTRIBUTE(root, 2, MARGIN);
	ASSERT_FLOAT_EQ(10.0f, attrs[2].parsed_unit.value);
	ASSERT_EQ((int)ui::Unit::PIXELS, (int)attrs[2].parsed_unit.unit);
	ASSERT_ATTRIBUTE(root, 3, PADDING);
	ASSERT_FLOAT_EQ(5.0f, attrs[3].parsed_unit.value);
	ASSERT_EQ((int)ui::Unit::PIXELS, (int)attrs[3].parsed_unit.unit);
	return true;
}

bool testTopLeftAttributesParse() {
	MockDocument doc;
	ASSERT_PARSE(doc, "[box top=12 left=34]");
	ASSERT_EQ(1, doc.m_root.children.size());
	ui::Element* root = doc.getElement(doc.m_root.children[0]);
	Span<ui::Attribute> attrs = root->attributes;
	ASSERT_EQ(2, attrs.size());
	ASSERT_ATTRIBUTE(root, 0, TOP);
	ASSERT_FLOAT_EQ(12.0f, attrs[0].parsed_unit.value);
	ASSERT_EQ((int)ui::Unit::PIXELS, (int)attrs[0].parsed_unit.unit);
	ASSERT_ATTRIBUTE(root, 1, LEFT);
	ASSERT_FLOAT_EQ(34.0f, attrs[1].parsed_unit.value);
	ASSERT_EQ((int)ui::Unit::PIXELS, (int)attrs[1].parsed_unit.unit);
	return true;
}

bool testPivotAttributesParse() {
	MockDocument doc;
	ASSERT_PARSE(doc, "[box pivot-x=50% pivot-y=2em]");
	ASSERT_EQ(1, doc.m_root.children.size());
	ui::Element* root = doc.getElement(doc.m_root.children[0]);
	Span<ui::Attribute> attrs = root->attributes;
	ASSERT_EQ(2, attrs.size());
	ASSERT_ATTRIBUTE(root, 0, PIVOT_X);
	ASSERT_FLOAT_EQ(50.0f, attrs[0].parsed_unit.value);
	ASSERT_EQ((int)ui::Unit::PERCENT, (int)attrs[0].parsed_unit.unit);
	ASSERT_ATTRIBUTE(root, 1, PIVOT_Y);
	ASSERT_FLOAT_EQ(2.0f, attrs[1].parsed_unit.value);
	ASSERT_EQ((int)ui::Unit::EM, (int)attrs[1].parsed_unit.unit);
	return true;
}

bool testPositionAttributeParse() {
	{
		MockDocument doc;
		ASSERT_PARSE(doc, "[box position=relative]");
		ASSERT_EQ(1, doc.m_root.children.size());
		ui::Element* root = doc.getElement(doc.m_root.children[0]);
		Span<ui::Attribute> attrs = root->attributes;
		ASSERT_EQ(1, attrs.size());
		ASSERT_ATTRIBUTE(root, 0, POSITION);
		ASSERT_EQ("relative", attrs[0].value);
	}
	{
		MockDocument doc;
		ASSERT_PARSE(doc, "[box position=absolute]");
		ASSERT_EQ(1, doc.m_root.children.size());
		ui::Element* root = doc.getElement(doc.m_root.children[0]);
		Span<ui::Attribute> attrs = root->attributes;
		ASSERT_EQ(1, attrs.size());
		ASSERT_ATTRIBUTE(root, 0, POSITION);
		ASSERT_EQ("absolute", attrs[0].value);
	}
	return true;
}

bool testPositionPresetAttributeParse() {
	MockDocument doc;
	ASSERT_PARSE(doc, "[box position=center]");
	ASSERT_EQ(1, doc.m_root.children.size());
	ui::Element* root = doc.getElement(doc.m_root.children[0]);
	Span<ui::Attribute> attrs = root->attributes;
	ASSERT_EQ(1, attrs.size());
	ASSERT_ATTRIBUTE(root, 0, POSITION);
	ASSERT_EQ("center", attrs[0].value);
	return true;
}

bool testClippingAttributeParse() {
	{
		MockDocument doc;
		ASSERT_PARSE(doc, "[box clipping=true]");
		ASSERT_EQ(1, doc.m_root.children.size());
		ui::Element* root = doc.getElement(doc.m_root.children[0]);
		Span<ui::Attribute> attrs = root->attributes;
		ASSERT_EQ(1, attrs.size());
		ASSERT_ATTRIBUTE(root, 0, CLIPPING);
		ASSERT_EQ(true, attrs[0].clip);
	}
	{
		MockDocument doc;
		ASSERT_PARSE(doc, "[box clipping=false]");
		ASSERT_EQ(1, doc.m_root.children.size());
		ui::Element* root = doc.getElement(doc.m_root.children[0]);
		Span<ui::Attribute> attrs = root->attributes;
		ASSERT_EQ(1, attrs.size());
		ASSERT_ATTRIBUTE(root, 0, CLIPPING);
		ASSERT_EQ(false, attrs[0].clip);
	}
	return true;
}

bool testPositionPresetsLayout() {
	struct PositionCase {
		const char* value;
		float expected_x;
		float expected_y;
	};

	const PositionCase cases[] = {
		{"top-left", 0.0f, 0.0f},
		{"top-center", 350.0f, 0.0f},
		{"top-right", 700.0f, 0.0f},
		{"middle-left", 0.0f, 275.0f},
		{"middle-center", 350.0f, 275.0f},
		{"middle-right", 700.0f, 275.0f},
		{"bottom-left", 0.0f, 550.0f},
		{"bottom-center", 350.0f, 550.0f},
		{"bottom-right", 700.0f, 550.0f},
		{"center", 350.0f, 275.0f}
	};

	for (const PositionCase& position_case : cases) {
		MockDocument doc;
		StaticString<256> source("[box width=800 height=600] { [box width=100 height=50 position=", position_case.value, "] }");
		ASSERT_PARSE(doc, source);
		doc.computeLayout(Vec2(800, 600));

		ASSERT_EQ(1, doc.m_root.children.size());
		ui::Element* root = doc.getElement(doc.m_root.children[0]);
		ASSERT_EQ(1, root->children.size());
		ui::Element* child = doc.getElement(root->children[0]);

		ASSERT_EQ((int)ui::PositionMode::ABSOLUTE, (int)child->position_mode);
		ASSERT_FLOAT_EQ(position_case.expected_x, child->position.x);
		ASSERT_FLOAT_EQ(position_case.expected_y, child->position.y);
	}

	return true;
}

// A fit-content parent is as tall as its in-flow children only: absolute children do not take part in its layout (the width pass agrees).
// Regression: both the column sum and the row maximum used to include them, so a column with spacers and absolute cards was taller than its spacers.
bool testFitContentHeightIgnoresAbsoluteChildren() {
	MockDocument doc;
	ASSERT_PARSE(doc,
		"[box $col width=100 direction=column] {"
			"[box width=100 height=50] {}"
			"[box width=100 height=20] {}"
			"[box position=absolute left=0 top=0 width=100 height=500] {}"
		"}"
		"[box $row width=300 direction=row wrap=false] {"
			"[box width=100 height=40] {}"
			"[box position=absolute left=0 top=0 width=100 height=500] {}"
		"}");
	doc.computeLayout(Vec2(800, 600));

	ui::Element* col = doc.getElementByID("col");
	ASSERT_TRUE(col != nullptr);
	ASSERT_FLOAT_EQ(70.0f, col->size.y);

	ui::Element* row = doc.getElementByID("row");
	ASSERT_TRUE(row != nullptr);
	ASSERT_FLOAT_EQ(40.0f, row->size.y);
	return true;
}

bool testPanelAttributes() {
	MockDocument doc;
	ASSERT_PARSE(doc, "[box bg-image=\"bg.spr\" bg-fit=cover bg-color=#000000 direction=column wrap=true justify-content=center]");
	ASSERT_EQ(1, doc.m_root.children.size());
	ui::Element* root = doc.getElement(doc.m_root.children[0]);
	Span<ui::Attribute> attrs = root->attributes;
	ASSERT_EQ(6, attrs.size());
	ASSERT_ATTRIBUTE(root, 0, BG_IMAGE);
	ASSERT_EQ("bg.spr", attrs[0].value);
	ASSERT_ATTRIBUTE(root, 1, BG_FIT);
	ASSERT_EQ("cover", attrs[1].value);
	ASSERT_ATTRIBUTE(root, 2, BG_COLOR);
	ASSERT_EQ(0, attrs[2].color.r);
	ASSERT_EQ(0, attrs[2].color.g);
	ASSERT_EQ(0, attrs[2].color.b);
	ASSERT_EQ(0xff, attrs[2].color.a);
	ASSERT_ATTRIBUTE(root, 3, DIRECTION);
	ASSERT_EQ((i32)Direction::COLUMN, (i32)attrs[3].direction);
	ASSERT_ATTRIBUTE(root, 4, WRAP);
	ASSERT_EQ(true, attrs[4].wrap);
	ASSERT_ATTRIBUTE(root, 5, JUSTIFY_CONTENT);
	ASSERT_EQ((i32)JustifyContent::CENTER, (i32)attrs[5].justify);
	return true;
}

bool testImageAttributes() {
	MockDocument doc;
	ASSERT_PARSE(doc, "[image src=\"img.png\" width=64 height=32]");
	ASSERT_EQ(1, doc.m_root.children.size());
	ui::Element* root = doc.getElement(doc.m_root.children[0]);
	Span<ui::Attribute> attrs = root->attributes;
	ASSERT_EQ(3, attrs.size());
	ASSERT_ATTRIBUTE(root, 0, SRC);
	ASSERT_EQ("img.png", attrs[0].value);
	ASSERT_ATTRIBUTE(root, 1, WIDTH);
	ASSERT_FLOAT_EQ(64.0f, attrs[1].parsed_unit.value);
	ASSERT_EQ((int)ui::Unit::PIXELS, (int)attrs[1].parsed_unit.unit);
	ASSERT_ATTRIBUTE(root, 2, HEIGHT);
	ASSERT_FLOAT_EQ(32.0f, attrs[2].parsed_unit.value);
	ASSERT_EQ((int)ui::Unit::PIXELS, (int)attrs[2].parsed_unit.unit);
	return true;
}

bool testDefaultValues() {
	MockDocument doc;
	ASSERT_PARSE(doc, "[box] {}");
	ASSERT_EQ(1, doc.m_root.children.size());
	ui::Element* elem = doc.getElement(doc.m_root.children[0]);
	
	doc.computeLayout(Vec2(800, 600));
	
	ASSERT_FLOAT_EQ(0.0f, elem->margins.top);
	ASSERT_FLOAT_EQ(0.0f, elem->margins.right);
	ASSERT_FLOAT_EQ(0.0f, elem->margins.bottom);
	ASSERT_FLOAT_EQ(0.0f, elem->margins.left);
	ASSERT_FLOAT_EQ(0.0f, elem->paddings.top);
	ASSERT_FLOAT_EQ(0.0f, elem->paddings.right);
	ASSERT_FLOAT_EQ(0.0f, elem->paddings.bottom);
	ASSERT_FLOAT_EQ(0.0f, elem->paddings.left);
	ASSERT_EQ((int)ui::Direction::COLUMN, (int)elem->direction);
	
	Span<ui::Attribute> attrs = elem->attributes;
	ASSERT_EQ(0, attrs.size());
	
	return true;
}

bool testSpanAndQuotedStringEquivalence() {
	MockDocument doc1;
	ASSERT_PARSE(doc1, "[box] { [span text=\"hello\"] }");
	ASSERT_EQ(1, doc1.m_root.children.size());
	ui::Element* root1 = doc1.getElement(doc1.m_root.children[0]);
	ASSERT_EQ(1, root1->children.size());
	ui::Element* child1 = doc1.getElement(root1->children[0]);
	ASSERT_TAG(child1, SPAN);
	Span<ui::Attribute> attrs1 = child1->attributes;
	ASSERT_EQ(0, attrs1.size());
	ASSERT_EQ("hello", child1->text);

	MockDocument doc2;
	ASSERT_PARSE(doc2, "[box] { hello }");
	ASSERT_EQ(1, doc2.m_root.children.size());
	ui::Element* root2 = doc2.getElement(doc2.m_root.children[0]);
	ASSERT_EQ(1, root2->children.size());
	ui::Element* child2 = doc2.getElement(root2->children[0]);
	ASSERT_TAG(child2, SPAN);
	Span<ui::Attribute> attrs2 = child2->attributes;
	ASSERT_EQ(0, attrs2.size());
	ASSERT_EQ("hello", child2->text);

	return true;
}

bool testSpanEmptyValue() {
	MockDocument doc;
	ASSERT_PARSE(doc, "[box] { [span text=\"\"] }");
	ASSERT_EQ(1, doc.m_root.children.size());
	ui::Element* root = doc.getElement(doc.m_root.children[0]);
	ASSERT_EQ(1, root->children.size());
	ui::Element* child = doc.getElement(root->children[0]);
	ASSERT_TAG(child, SPAN);
	Span<ui::Attribute> attrs = child->attributes;
	ASSERT_EQ(0, attrs.size());
	ASSERT_EQ("", child->text);

	return true;
}

bool testTopLevelSpanWithoutContainer() {
	MockDocument doc;
	ASSERT_PARSE(doc, "[span text=\"hello\"]");
	ASSERT_EQ(1, doc.m_root.children.size());

	doc.computeLayout(Vec2(800, 600));

	ui::Element* span = doc.getElement(doc.m_root.children[0]);
	ASSERT_TAG(span, SPAN);
	ASSERT_EQ("hello", span->text);
	ASSERT_TRUE(!span->lines.empty());
	ASSERT_TRUE(span->size.x > 0);
	ASSERT_TRUE(span->size.y > 0);
	return true;
}

bool testFontAttribute() {
	MockDocument doc;
	ASSERT_PARSE(doc, "[box font=\"arial.ttf\"] { [span text=\"hello\" font=\"times.ttf\"] }");
	ASSERT_EQ(1, doc.m_root.children.size());
	ui::Element* root = doc.getElement(doc.m_root.children[0]);
	ASSERT_EQ(1, root->children.size());
	
	Span<ui::Attribute> root_attrs = root->attributes;
	ASSERT_EQ(1, root_attrs.size());
	ASSERT_ATTRIBUTE(root, 0, FONT);
	ASSERT_EQ("arial.ttf", root_attrs[0].value);
	
	ui::Element* span = doc.getElement(root->children[0]);
	ASSERT_TAG(span, SPAN);
	Span<ui::Attribute> span_attrs = span->attributes;
	ASSERT_EQ(1, span_attrs.size());
	ASSERT_ATTRIBUTE(span, 0, FONT);
	ASSERT_EQ("times.ttf", span_attrs[0].value);
	ASSERT_EQ("hello", span->text);
	
	return true;
}

bool testFontSizeAttribute() {
	MockDocument doc;
	ASSERT_PARSE(doc, "[box font-size=16] { [span text=\"hello\" font-size=24] }");
	ASSERT_EQ(1, doc.m_root.children.size());
	ui::Element* root = doc.getElement(doc.m_root.children[0]);
	ASSERT_EQ(1, root->children.size());

	Span<ui::Attribute> root_attrs = root->attributes;
	ASSERT_EQ(1, root_attrs.size());
	ASSERT_ATTRIBUTE(root, 0, FONT_SIZE);
	ASSERT_EQ(16.f, root_attrs[0].font_size);

	ui::Element* span = doc.getElement(root->children[0]);
	ASSERT_TAG(span, SPAN);
	Span<ui::Attribute> span_attrs = span->attributes;
	ASSERT_EQ(1, span_attrs.size());
	ASSERT_ATTRIBUTE(span, 0, FONT_SIZE);
	ASSERT_EQ(24.f, span_attrs[0].font_size);
	ASSERT_EQ("hello", span->text);

	return true;
}

bool testFontInheritance() {
	MockDocument doc;
	ASSERT_PARSE(doc, "[box font=\"arial.ttf\"] { hello }");
	ASSERT_EQ(1, doc.m_root.children.size());
	ui::Element* root = doc.getElement(doc.m_root.children[0]);
	doc.computeLayout(Vec2(800, 600));

	ui::Element* span = doc.getElement(root->children[0]);
	ASSERT_TRUE(span->getFontHandle() != nullptr);

	return true;
}

bool testFontInheritanceDeep() {
	MockDocument doc;
	ASSERT_PARSE(doc, "[box font=\"arial.ttf\"] { [box] { hello } }");
	ASSERT_EQ(1, doc.m_root.children.size());
	ui::Element* root = doc.getElement(doc.m_root.children[0]);
	doc.computeLayout(Vec2(800, 600));

	ui::Element* child_panel = doc.getElement(root->children[0]);
	ui::Element* grandchild_span = doc.getElement(child_panel->children[0]);
	ASSERT_TRUE(grandchild_span->getFontHandle() != nullptr);

	return true;
}

bool testFontRefsDoNotLeakAcrossStyleRecompute() {
	MockDocument doc;
	ASSERT_PARSE(doc, "[box font=\"arial.ttf\"] { [span text=\"hello\"] }");
	ASSERT_EQ(1, doc.m_root.children.size());
	ASSERT_TRUE(!doc.m_font_manager.hasLeakedRefs(doc));

	for (int i = 0; i < 5; ++i) {
		doc.addClass(0, "tmp");
		doc.removeClass(0, "tmp");
	}

	ASSERT_TRUE(!doc.m_font_manager.hasLeakedRefs(doc));
	return true;
}

bool testImageRefsDoNotLeakAcrossStyleRecompute() {
	MockDocument doc;
	ASSERT_PARSE(doc, "[box] { [image src=\"img.png\"] }");
	ASSERT_EQ(1, doc.m_root.children.size());
	ASSERT_TRUE(!doc.m_image_manager.hasLeakedRefs(doc));

	for (int i = 0; i < 5; ++i) {
		doc.addClass(0, "tmp");
		doc.removeClass(0, "tmp");
	}

	ASSERT_TRUE(!doc.m_image_manager.hasLeakedRefs(doc));
	return true;
}

bool testSpriteRefsDoNotLeakAcrossStyleRecompute() {
	MockDocument doc;
	ASSERT_PARSE(doc, "[box bg-image=\"bg.spr\"]");
	ASSERT_EQ(1, doc.m_root.children.size());
	ASSERT_TRUE(!doc.m_sprite_manager.hasLeakedRefs(doc));

	for (int i = 0; i < 5; ++i) {
		doc.addClass(0, "tmp");
		doc.removeClass(0, "tmp");
	}

	ASSERT_TRUE(!doc.m_sprite_manager.hasLeakedRefs(doc));
	return true;
}

bool testDocumentDestructorReleasesFontRefs() {
	MockFontManager font_manager;
	MockImageManager image_manager;
	{
		ui::Document doc(&font_manager, getGlobalAllocator(), &image_manager);
		ASSERT_PARSE(doc, "[box font=\"arial.ttf\"] { [span text=\"hello\"] [box font=\"times.ttf\"] { [span text=\"world\"] } }");
		doc.computeLayout(Vec2(800, 600));
		ASSERT_TRUE(!font_manager.hasLeakedRefs(doc));
		ASSERT_TRUE(font_manager.hasAnyRefs());
	}

	ASSERT_TRUE(!font_manager.hasAnyRefs());
	return true;
}

bool testDocumentDestructorReleasesImageRefs() {
	MockFontManager font_manager;
	MockImageManager image_manager;
	{
		ui::Document doc(&font_manager, getGlobalAllocator(), &image_manager);
		ASSERT_PARSE(doc, "[box] { [image src=\"img.png\"] [image src=\"img.png\"] }");
		ASSERT_TRUE(!image_manager.hasLeakedRefs(doc));
		ASSERT_TRUE(image_manager.hasAnyRefs());
	}

	ASSERT_TRUE(!image_manager.hasAnyRefs());
	return true;
}

// A sprite used by many elements is loaded once; a sprite named only by a state rule is loaded with the document; an element whose state rule stops applying
// does not keep that sprite.
bool testSpritesAreSharedPreloadedAndNotStale() {
	MockDocument doc;
	ASSERT_PARSE(doc, R"(
		[style] {
			.hot { bg-image: "bg.spr"; }
		}
		[box .cold] {}
		[box .cold] {}
	)");
	ASSERT_EQ(2, doc.m_root.children.size());
	// only a rule names the sprite: loaded already, once, and no element shows it
	ASSERT_TRUE(doc.m_sprite_manager.hasAnyRefs());
	ASSERT_TRUE(doc.getElement(doc.m_root.children[0])->getSpriteHandle() == nullptr);
	ASSERT_EQ(1, doc.m_sprites.size());

	doc.addClass(doc.m_root.children[0], "hot");
	doc.addClass(doc.m_root.children[1], "hot");
	ASSERT_TRUE(doc.getElement(doc.m_root.children[0])->getSpriteHandle() != nullptr);
	ASSERT_EQ(1, doc.m_sprites.size());   // shared, not loaded again
	ASSERT_TRUE(!doc.m_sprite_manager.hasLeakedRefs(doc));

	doc.removeClass(doc.m_root.children[0], "hot");
	ASSERT_TRUE(doc.getElement(doc.m_root.children[0])->getSpriteHandle() == nullptr);
	ASSERT_TRUE(doc.getElement(doc.m_root.children[1])->getSpriteHandle() != nullptr);
	ASSERT_TRUE(!doc.m_sprite_manager.hasLeakedRefs(doc));
	return true;
}

// a sprite set at runtime is shared with every other element that uses it, and released with the document
bool testSetBGImageSharesSprite() {
	MockDocument doc;
	ASSERT_PARSE(doc, R"(
		[box] {}
		[box] {}
	)");
	ASSERT_EQ(2, doc.m_root.children.size());
	ui::Element* a = doc.getElement(doc.m_root.children[0]);
	ui::Element* b = doc.getElement(doc.m_root.children[1]);
	a->setBGImage(Path("a.spr"));
	b->setBGImage(Path("a.spr"));
	ASSERT_TRUE(a->getSpriteHandle() != nullptr);
	ASSERT_TRUE(a->getSpriteHandle() == b->getSpriteHandle());
	ASSERT_EQ(1, doc.m_sprites.size());
	b->setBGImage(Path("b.spr"));
	ASSERT_TRUE(a->getSpriteHandle() != b->getSpriteHandle());
	ASSERT_EQ(2, doc.m_sprites.size());
	ASSERT_TRUE(!doc.m_sprite_manager.hasLeakedRefs(doc));
	return true;
}

bool testGetElementByIdFindsElements() {
	MockDocument doc;
	ASSERT_PARSE(doc, R"(
		[box $outer] {
			[box $inner] {}
		}
		[box] {}
	)");
	ASSERT_EQ(2, doc.m_root.children.size());
	ui::Element* outer = doc.getElement(doc.m_root.children[0]);
	ASSERT_TRUE(doc.getElementByID("outer") == outer);
	ASSERT_EQ(1, outer->children.size());
	ASSERT_TRUE(doc.getElementByID("inner") == doc.getElement(outer->children[0]));
	ASSERT_TRUE(doc.getElementByID("missing") == nullptr);
	ASSERT_TRUE(doc.getElementByID("") == nullptr);
	ASSERT_TRUE(doc.getElementByID(nullptr) == nullptr);
	return true;
}

// ids identify elements, so a document can't use one twice (also not at different depths)
bool testDuplicateIdIsAParseError() {
	MockDocument doc;
	doc.m_suppress_logging = true;
	ASSERT_EQ(false, doc.parse("[box $same] {} [box $same] {}", "test.ui"));
	ASSERT_TRUE(doc.getElementByID("same") == nullptr);
	ASSERT_EQ(false, doc.parse("[box $same] { [box $same] {} }", "test.ui"));
	ASSERT_EQ(false, doc.parse("[box $a] { [span $b] {} } [box $b] {}", "test.ui"));
	ASSERT_TRUE(doc.parse("[box $a] { [box $b] {} } [box $c] {}", "test.ui"));
	ASSERT_TRUE(doc.getElementByID("a") != nullptr && doc.getElementByID("b") != nullptr && doc.getElementByID("c") != nullptr);
	return true;
}

// the id index follows the document: a new parse replaces it, a failed parse leaves it empty
bool testGetElementByIdFollowsParse() {
	MockDocument doc;
	doc.m_suppress_logging = true;
	ASSERT_PARSE(doc, "[box $first] {}");
	ASSERT_TRUE(doc.getElementByID("first") != nullptr);

	ASSERT_PARSE(doc, "[box $second] {}");
	ASSERT_TRUE(doc.getElementByID("first") == nullptr);
	ASSERT_TRUE(doc.getElementByID("second") == doc.getElement(doc.m_root.children[0]));

	ASSERT_EQ(false, doc.parse("[box $third width=wat]", "test.ui"));
	ASSERT_TRUE(doc.getElementByID("second") == nullptr);
	ASSERT_TRUE(doc.getElementByID("third") == nullptr);
	return true;
}

// mutators only mark the layout dirty, it is recomputed by flushLayout(), computeLayout() or a hit test
bool testLayoutIsDeferredUntilFlush() {
	MockDocument doc;
	ASSERT_PARSE(doc, R"(
		[style] {
			.wide { width: 200; }
		}
		[box $a width=100 height=20] {}
		[box $b height=20] {}
	)");
	ui::Element* a = doc.getElementByID("a");
	ui::Element* b = doc.getElementByID("b");
	ASSERT_TRUE(a != nullptr && b != nullptr);
	doc.computeLayout(Vec2(800, 600));
	ASSERT_TRUE(!doc.m_layout_dirty);
	ASSERT_FLOAT_EQ(100.0f, a->size.x);

	a->setWidth("50");
	ASSERT_TRUE(doc.m_layout_dirty);
	ASSERT_FLOAT_EQ(100.0f, a->size.x);   // not recomputed yet
	doc.flushLayout();
	ASSERT_TRUE(!doc.m_layout_dirty);
	ASSERT_FLOAT_EQ(50.0f, a->size.x);

	doc.flushLayout();   // nothing to do
	ASSERT_FLOAT_EQ(50.0f, a->size.x);

	// a class change is deferred as well (b has no width of its own, so the rule applies)
	doc.addClass(doc.m_root.children[1], "wide");
	ASSERT_TRUE(doc.m_layout_dirty);
	ASSERT_TRUE(b->size.x != 200.0f);
	doc.computeLayout(Vec2(800, 600));   // an explicit layout clears the flag too
	ASSERT_TRUE(!doc.m_layout_dirty);
	ASSERT_FLOAT_EQ(200.0f, b->size.x);
	return true;
}

// hit testing sees the layout of the mutations made before it
bool testHitTestFlushesDeferredLayout() {
	MockDocument doc;
	ASSERT_PARSE(doc, "[box $a width=100 height=20] {}");
	ui::Element* a = doc.getElementByID("a");
	ASSERT_TRUE(a != nullptr);
	doc.computeLayout(Vec2(800, 600));
	ASSERT_TRUE(doc.getElementAt(Vec2(75, 5)) == a);

	a->setWidth("50");
	ASSERT_TRUE(doc.getElementAt(Vec2(75, 5)) == nullptr);
	ASSERT_TRUE(doc.getElementAt(Vec2(25, 5)) == a);
	return true;
}

bool testHitTestPrefersLastRoot() {
	MockDocument doc;
	ASSERT_PARSE(doc,
		"[box $back position=absolute left=0 top=0 width=100 height=100] {}"
		"[box $front position=absolute left=0 top=0 width=100 height=100] {}");
	doc.computeLayout(Vec2(800, 600));
	Element* front = doc.getElementByID("front");
	ASSERT_TRUE(doc.getElementAt(Vec2(50, 50)) == front);
	front->setVisible(false);
	ASSERT_TRUE(doc.getElementAt(Vec2(50, 50)) == doc.getElementByID("back"));
	ASSERT_TRUE(doc.getElementAt(Vec2(150, 50)) == nullptr);
	return true;
}

bool testHitTestPrefersLastNestedChild() {
	MockDocument doc;
	ASSERT_PARSE(doc,
		"[box width=200 height=200] {"
			"[box $back position=absolute left=0 top=0 width=100 height=100] {}"
			"[box $front position=absolute left=0 top=0 width=100 height=100] {"
				"[box $skip position=absolute left=0 top=0 width=100 height=100] {}"
			"}"
		"}");
	doc.computeLayout(Vec2(800, 600));
	ASSERT_TRUE(doc.getElementAt(Vec2(50, 50)) == doc.getElementByID("skip"));
	doc.getElementByID("front")->setVisible(false);
	ASSERT_TRUE(doc.getElementAt(Vec2(50, 50)) == doc.getElementByID("back"));
	return true;
}

bool testDocumentDestructorReleasesSpriteRefs() {
	MockFontManager font_manager;
	MockImageManager image_manager;
	MockSpriteManager sprite_manager;
	{
		ui::Document doc(&font_manager, getGlobalAllocator(), &image_manager, &sprite_manager);
		ASSERT_PARSE(doc, "[box bg-image=\"bg.spr\"] [box bg-image=\"bg.spr\"]");
		ASSERT_TRUE(!sprite_manager.hasLeakedRefs(doc));
		ASSERT_TRUE(sprite_manager.hasAnyRefs());
	}

	ASSERT_TRUE(!sprite_manager.hasAnyRefs());
	return true;
}

bool testColorInheritance() {
	MockDocument doc;
	ASSERT_PARSE(doc, "[box color=#ff0000] { \"hello\" }");
	ASSERT_EQ(1, doc.m_root.children.size());
	ui::Element* root = doc.getElement(doc.m_root.children[0]);
	doc.computeLayout(Vec2(800, 600));

	ASSERT_TRUE(root->color.r == 255 && root->color.g == 0 && root->color.b == 0 && root->color.a == 255);

	ui::Element* span = doc.getElement(root->children[0]);
	ASSERT_TRUE(span->color.r == 255 && span->color.g == 0 && span->color.b == 0 && span->color.a == 255);
	return true;
}

bool testColorInheritanceDeep() {
	MockDocument doc;
	ASSERT_PARSE(doc, "[box color=#00ff00] { [box] { \"hello\" } }");
	ASSERT_EQ(1, doc.m_root.children.size());
	ui::Element* root = doc.getElement(doc.m_root.children[0]);
	doc.computeLayout(Vec2(800, 600));

	ASSERT_TRUE(root->color.r == 0 && root->color.g == 255 && root->color.b == 0 && root->color.a == 255);

	ui::Element* child_panel = doc.getElement(root->children[0]);
	ASSERT_TRUE(child_panel->color.r == 0 && child_panel->color.g == 255 && child_panel->color.b == 0 && child_panel->color.a == 255);

	ui::Element* grandchild_span = doc.getElement(child_panel->children[0]);
	ASSERT_TRUE(grandchild_span->color.r == 0 && grandchild_span->color.g == 255 && grandchild_span->color.b == 0 && grandchild_span->color.a == 255);

	return true;
}

bool testColorAlphaSupport() {
	MockDocument doc;
	ASSERT_PARSE(doc, "[box color=#11223344 bg-color=#55667788] { [box] { \"hello\" } }");
	ASSERT_EQ(1, doc.m_root.children.size());
	doc.computeLayout(Vec2(800, 600));

	ui::Element* root = doc.getElement(doc.m_root.children[0]);
	ASSERT_EQ(0x11, (int)root->color.r);
	ASSERT_EQ(0x22, (int)root->color.g);
	ASSERT_EQ(0x33, (int)root->color.b);
	ASSERT_EQ(0x44, (int)root->color.a);
	ASSERT_EQ(0x55, (int)root->bg_color.r);
	ASSERT_EQ(0x66, (int)root->bg_color.g);
	ASSERT_EQ(0x77, (int)root->bg_color.b);
	ASSERT_EQ(0x88, (int)root->bg_color.a);

	ui::Element* child_panel = doc.getElement(root->children[0]);
	ASSERT_EQ(0x11, (int)child_panel->color.r);
	ASSERT_EQ(0x22, (int)child_panel->color.g);
	ASSERT_EQ(0x33, (int)child_panel->color.b);
	ASSERT_EQ(0x44, (int)child_panel->color.a);

	ui::Element* grandchild_span = doc.getElement(child_panel->children[0]);
	ASSERT_EQ(0x11, (int)grandchild_span->color.r);
	ASSERT_EQ(0x22, (int)grandchild_span->color.g);
	ASSERT_EQ(0x33, (int)grandchild_span->color.b);
	ASSERT_EQ(0x44, (int)grandchild_span->color.a);

	return true;
}

bool testAlignInheritance() {
	MockDocument doc;
	ASSERT_PARSE(doc, "[box align=center] { [box] { \"hello\" } }");
	ASSERT_EQ(1, doc.m_root.children.size());
	ui::Element* root = doc.getElement(doc.m_root.children[0]);
	doc.computeLayout(Vec2(800, 600));
	ASSERT_EQ((i32)ui::Align::CENTER, (i32)root->text_align);

	ui::Element* child_panel = doc.getElement(root->children[0]);
	ASSERT_EQ((i32)ui::Align::CENTER, (i32)child_panel->text_align);

	ui::Element* grandchild_span = doc.getElement(child_panel->children[0]);
	ASSERT_EQ((i32)ui::Align::CENTER, (i32)grandchild_span->text_align);

	return true;
}

bool testOpacityInheritance() {
	MockDocument doc;
	ASSERT_PARSE(doc, "[box opacity=0.5] { [box] { \"hello\" } }");
	ASSERT_EQ(1, doc.m_root.children.size());
	ui::Element* root = doc.getElement(doc.m_root.children[0]);

	ASSERT_FLOAT_EQ(0.5f, root->opacity);

	ui::Element* child_panel = doc.getElement(root->children[0]);
	ASSERT_FLOAT_EQ(0.5f, child_panel->opacity);

	ui::Element* grandchild_span = doc.getElement(child_panel->children[0]);
	ASSERT_FLOAT_EQ(0.5f, grandchild_span->opacity);

	return true;
}

bool testOpacityInheritanceAndOverride() {
	MockDocument doc;
	ASSERT_PARSE(doc, "[box opacity=0.5] { [box opacity=0.5] { [span opacity=0.2 text=\"hello\"] } }");
	ASSERT_EQ(1, doc.m_root.children.size());
	ui::Element* root = doc.getElement(doc.m_root.children[0]);

	ASSERT_FLOAT_EQ(0.5f, root->opacity);

	ui::Element* child_panel = doc.getElement(root->children[0]);
	ASSERT_FLOAT_EQ(0.25f, child_panel->opacity);

	ui::Element* grandchild_span = doc.getElement(child_panel->children[0]);
	ASSERT_FLOAT_EQ(0.05f, grandchild_span->opacity);

	return true;
}

bool testMultilineStringLayout() {
	MockDocument doc;
	ASSERT_PARSE(doc, R"(
		[box width=fit-content height=fit-content font="arial.ttf" font-size=16] {
			Line 1
			Line 2
			Line 3
		}
	)");
	doc.computeLayout(Vec2(800, 600));
	ASSERT_EQ(1, doc.m_root.children.size());
	ui::Element* panel = doc.getElement(doc.m_root.children[0]);
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
	MockFontManager font_manager;
	MockFontManager::FontHandle font = font_manager.loadFont("test.ttf", 16);

	Vec2 size1 = font_manager.measureTextA(font, "hello");
	ASSERT_FLOAT_EQ(40.0f, size1.x);
	ASSERT_FLOAT_EQ(16.0f, size1.y);

	Vec2 size2 = font_manager.measureTextA(font, "hel\nlo");
	ASSERT_FLOAT_EQ(48.0f, size2.x);
	ASSERT_FLOAT_EQ(16.0f, size2.y);

	Vec2 size3 = font_manager.measureTextA(font, "hel\rlo");
	ASSERT_FLOAT_EQ(40.0f, size3.x);
	ASSERT_FLOAT_EQ(16.0f, size3.y);

	Vec2 size4 = font_manager.measureTextA(font, "h\n\r\nel\rlo");
	ASSERT_FLOAT_EQ(48.0f, size4.x);
	ASSERT_FLOAT_EQ(16.0f, size4.y);

	return true;
}

bool testTextWithSpecialChars() {
	MockDocument doc;
	ASSERT_PARSE(doc, "[box] { ,-=()*&^@! }");
	ASSERT_EQ(1, doc.m_root.children.size());
	ui::Element* root = doc.getElement(doc.m_root.children[0]);
	ASSERT_TAG(root, BOX);
	ASSERT_EQ(1, root->children.size());
	ui::Element* child = doc.getElement(root->children[0]);
	ASSERT_TAG(child, SPAN);
	ASSERT_EQ(",-=()*&^@!", child->text);
	return true;
}

bool testSpaceBetweenSpans() {
	MockDocument doc;
	ASSERT_PARSE(doc, "[box direction=row font=\"arial.ttf\" font-size=16] { [span text=\"hello\"] [span text=\"world\"] }");
	doc.computeLayout(Vec2(800, 600));
	ASSERT_EQ(1, doc.m_root.children.size());
	ui::Element* panel = doc.getElement(doc.m_root.children[0]);
	ASSERT_EQ(2, panel->children.size());
	ui::Element* span1 = doc.getElement(panel->children[0]);
	ui::Element* span2 = doc.getElement(panel->children[1]);
	ASSERT_EQ(1, span1->lines.size());
	ASSERT_EQ(1, span2->lines.size());
	ASSERT_EQ(span1->lines[0].pos.x + span1->size.x, span2->lines[0].pos.x);
	return true;
}

bool testParseAndRuntimeMutation() {
	MockDocument doc;
	ASSERT_PARSE(doc, R"(
		[style] {
			.initial {
				width: 100%;
				color: #ff0000;
			}
			.added {
				height: 200;
			}
		}
		[box .initial] {
		}
	)");
	ASSERT_EQ(1, doc.m_root.children.size());
	ui::Element* root = doc.getElement(doc.m_root.children[0]);
	
	bool has_width_100 = false;
	bool has_color_red = false;
	for (const ui::Attribute& attr : root->attributes) {
		if (attr.type == ui::AttributeName::WIDTH && attr.parsed_unit.unit == ui::Unit::PERCENT && attr.parsed_unit.value == 100.0f) has_width_100 = true;
		if (attr.type == ui::AttributeName::COLOR && attr.color == Color::RED) has_color_red = true;
	}
	ASSERT_TRUE(has_width_100);
	ASSERT_TRUE(has_color_red);
	
	doc.addClass(doc.m_root.children[0], "added");
	
	bool has_height_200 = false;
	for (const ui::Attribute& attr : root->attributes) {
		if (attr.type == ui::AttributeName::HEIGHT && attr.parsed_unit.unit == ui::Unit::PIXELS && attr.parsed_unit.value == 200.0f) has_height_200 = true;
	}
	ASSERT_TRUE(has_height_200);
	has_width_100 = false;
	has_color_red = false;
	for (const ui::Attribute& attr : root->attributes) {
		if (attr.type == ui::AttributeName::WIDTH && attr.parsed_unit.unit == ui::Unit::PERCENT && attr.parsed_unit.value == 100.0f) has_width_100 = true;
		if (attr.type == ui::AttributeName::COLOR && attr.color == Color::RED) has_color_red = true;
	}
	ASSERT_TRUE(has_width_100);
	ASSERT_TRUE(has_color_red);
	
	return true;
}

bool testComplexMutationSequence() {
	MockDocument doc;
	ASSERT_PARSE(doc, R"(
		[style] {
			.a { width: 10%; }
			.b { height: 20; }
			.c { margin: 5; }
			.d { padding: 3; }
		}
		[box .a .b] {
		}
	)");
	ASSERT_EQ(1, doc.m_root.children.size());
	ui::Element* root = doc.getElement(doc.m_root.children[0]);
	
	int initial_attrs = root->attributes.size();
	
	doc.addClass(doc.m_root.children[0], "c");
	int after_add_c = root->attributes.size();
	ASSERT_TRUE(after_add_c > initial_attrs);
	
	doc.addClass(doc.m_root.children[0], "d");
	int after_add_d = root->attributes.size();
	ASSERT_TRUE(after_add_d > after_add_c);
	
	doc.removeClass(doc.m_root.children[0], "b");
	int after_remove_b = root->attributes.size();
	ASSERT_TRUE(after_remove_b < after_add_d);
	
	doc.removeClass(doc.m_root.children[0], "a");
	doc.removeClass(doc.m_root.children[0], "c");
	int final_attrs = root->attributes.size();
	
	bool has_padding = false;
	int attr_count = 0;
	for (const ui::Attribute& attr : root->attributes) {
		attr_count++;
		if (attr.type == ui::AttributeName::PADDING && attr.parsed_unit.unit == ui::Unit::PIXELS && attr.parsed_unit.value == 3.0f) has_padding = true;
	}
	ASSERT_TRUE(has_padding);
	ASSERT_EQ(1, attr_count);
	
	return true;
}

bool testSetWidthUpdatesPixelsAndLayout() {
	MockDocument doc;
	ASSERT_PARSE(doc, R"(
		[box width=100 height=20] {
		}
	)");

	ASSERT_EQ(1, doc.m_root.children.size());
	ui::Element* root = doc.getElement(doc.m_root.children[0]);
	doc.computeLayout(Vec2(800, 600));
	ASSERT_FLOAT_EQ(100.0f, root->size.x);

	root->setWidth("250");

	ASSERT_FLOAT_EQ(250.0f, root->width_unit.value);
	ASSERT_EQ((int)ui::Unit::PIXELS, (int)root->width_unit.unit);
	doc.flushLayout();
	ASSERT_FLOAT_EQ(250.0f, root->size.x);
	return true;
}

bool testSetWidthUpdatesPercentAndLayout() {
	MockDocument doc;
	ASSERT_PARSE(doc, R"(
		[box width=50% height=20] {
		}
	)");

	ASSERT_EQ(1, doc.m_root.children.size());
	ui::Element* root = doc.getElement(doc.m_root.children[0]);
	doc.computeLayout(Vec2(800, 600));
	ASSERT_FLOAT_EQ(400.0f, root->size.x);

	root->setWidth("25%");

	ASSERT_FLOAT_EQ(25.0f, root->width_unit.value);
	ASSERT_EQ((int)ui::Unit::PERCENT, (int)root->width_unit.unit);
	doc.flushLayout();
	ASSERT_FLOAT_EQ(200.0f, root->size.x);
	return true;
}

struct MockMouseDevice : InputSystem::Device {
	MockMouseDevice() { type = InputDeviceType::MOUSE; }
	void update(float dt) override {}
	const char* getName() const override { return "MockMouse"; }
};

static void injectMouseButton(MockDocument& doc, MockMouseDevice& mouse, bool down, float x, float y) {
	InputSystem::Event ev;
	ev.device = &mouse;
	ev.type = InputEventType::MOUSE_BUTTON;
	ev.data.mouse_button.down = down;
	ev.data.mouse_button.button = (os::MouseButton)0;
	ev.data.mouse_button.x = x;
	ev.data.mouse_button.y = y;
	doc.injectEvent(ev);
}

bool testKeyboardInputEvent() {
	MockDocument doc;
	struct MockKeyboardDevice : InputSystem::Device {
		MockKeyboardDevice() { type = InputDeviceType::KEYBOARD; }
		void update(float dt) override {}
		const char* getName() const override { return "MockKeyboard"; }
	} keyboard;

	InputSystem::Event event;
	event.type = InputEventType::KEYBOARD;
	event.device = &keyboard;
	event.data.keyboard.keycode = (os::Keycode)65;
	event.data.keyboard.down = true;
	event.data.keyboard.is_repeat = false;
	doc.injectEvent(event);
	event.data.keyboard.down = false;
	doc.injectEvent(event);

	const auto events = doc.getEvents();
	ASSERT_EQ(2, events.length());
	ASSERT_EQ((int)ui::EventType::KEY_DOWN, (int)events[0].type);
	ASSERT_EQ((int)ui::EventType::KEY_UP, (int)events[1].type);
	ASSERT_EQ(65, (int)events[0].key_code);
	ASSERT_EQ(65, (int)events[1].key_code);
	return true;
}

bool testOnClickAttributeParseOnBox() {
	MockDocument doc;
	ASSERT_PARSE(doc, "[box on-click=foo]");
	ASSERT_EQ(1, doc.m_root.children.size());
	ui::Element* root = doc.getElement(doc.m_root.children[0]);
	ASSERT_EQ(1, root->attributes.size());
	ASSERT_ATTRIBUTE(root, 0, ON_CLICK);
	ASSERT_EQ("foo", root->attributes[0].value);
	return true;
}

bool testOnClickAttributeRejectedOnNonBox() {
	MockDocument doc;
	doc.m_suppress_logging = true;
	ASSERT_EQ(false, doc.parse("[span on-click=foo]", "test.ui"));
	return true;
}

bool testActionEventPrefersLastRoot() {
	MockDocument doc;
	ASSERT_PARSE(doc,
		"[box width=200 height=200 on-click=background] {}"
		"[box position=absolute left=0 top=0 width=200 height=200] {"
			"[box width=100 height=100 on-click=confirm] { [span text=Confirm] }"
		"}");
	doc.computeLayout(Vec2(800, 600));
	MockMouseDevice mouse;
	injectMouseButton(doc, mouse, true, 50, 50);
	injectMouseButton(doc, mouse, false, 50, 50);
	int actions = 0;
	for (const ui::Event& e : doc.getEvents()) {
		if (e.type != ui::EventType::ACTION) continue;
		++actions;
		ASSERT_EQ("confirm", e.action);
	}
	ASSERT_EQ(1, actions);
	return true;
}

bool testActionEventPrefersLastNestedChild() {
	MockDocument doc;
	ASSERT_PARSE(doc,
		"[box width=200 height=200] {"
			"[box width=200 height=200 on-click=background] {}"
			"[box position=absolute left=0 top=0 width=200 height=200] {"
				"[box width=100 height=100 on-click=confirm] { [span text=Confirm] }"
			"}"
		"}");
	doc.computeLayout(Vec2(800, 600));
	MockMouseDevice mouse;
	injectMouseButton(doc, mouse, true, 50, 50);
	injectMouseButton(doc, mouse, false, 50, 50);
	int actions = 0;
	for (const ui::Event& e : doc.getEvents()) {
		if (e.type != ui::EventType::ACTION) continue;
		++actions;
		ASSERT_EQ("confirm", e.action);
	}
	ASSERT_EQ(1, actions);
	return true;
}

bool testActionEventEmittedOnReleaseOverClickableBox() {
	MockDocument doc;
	ASSERT_PARSE(doc, "[box width=100 height=100 on-click=foo]");
	doc.computeLayout(Vec2(800, 600));

	MockMouseDevice mouse;
	injectMouseButton(doc, mouse, true, 50, 50);
	injectMouseButton(doc, mouse, false, 50, 50);

	int action_count = 0;
	for (const ui::Event& e : doc.getEvents()) {
		if (e.type != ui::EventType::ACTION) continue;
		++action_count;
		ASSERT_EQ(0, (int)e.element_index);
		ASSERT_EQ("foo", e.action);
	}
	ASSERT_EQ(1, action_count);
	return true;
}

bool testActionEventNotEmittedWithoutOnClick() {
	MockDocument doc;
	ASSERT_PARSE(doc, "[box width=100 height=100]");
	doc.computeLayout(Vec2(800, 600));

	MockMouseDevice mouse;
	injectMouseButton(doc, mouse, true, 50, 50);
	injectMouseButton(doc, mouse, false, 50, 50);

	for (const ui::Event& e : doc.getEvents()) {
		if (e.type == ui::EventType::ACTION) {
			logError("Unexpected ACTION event for box without on-click");
			return false;
		}
	}
	return true;
}

bool testActionEventNotEmittedForSpan() {
	MockDocument doc;
	ASSERT_PARSE(doc, "[box width=200 height=100] { [span text=hello] }");
	doc.computeLayout(Vec2(800, 600));

	MockMouseDevice mouse;
	injectMouseButton(doc, mouse, true, 10, 10);
	injectMouseButton(doc, mouse, false, 10, 10);

	for (const ui::Event& e : doc.getEvents()) {
		if (e.type == ui::EventType::ACTION) {
			logError("Unexpected ACTION event for span click");
			return false;
		}
	}
	return true;
}

bool testActionEventNotEmittedOutside() {
	MockDocument doc;
	ASSERT_PARSE(doc, "[box width=100 height=100 on-click=foo]");
	doc.computeLayout(Vec2(800, 600));

	MockMouseDevice mouse;
	injectMouseButton(doc, mouse, true, 400, 400);
	injectMouseButton(doc, mouse, false, 400, 400);

	for (const ui::Event& e : doc.getEvents()) {
		if (e.type == ui::EventType::ACTION) {
			logError("Unexpected ACTION event for click outside");
			return false;
		}
	}
	return true;
}

bool testActionEventMultipleBoxesTargeting() {
	MockDocument doc;
	ASSERT_PARSE(doc, "[box width=100 height=100 on-click=foo] [box width=100 height=100 on-click=bar]");
	doc.computeLayout(Vec2(800, 600));

	MockMouseDevice mouse;
	injectMouseButton(doc, mouse, true, 50, 150);
	injectMouseButton(doc, mouse, false, 50, 150);

	int action_count = 0;
	for (const ui::Event& e : doc.getEvents()) {
		if (e.type != ui::EventType::ACTION) continue;
		++action_count;
		ASSERT_EQ(1, (int)e.element_index);
		ASSERT_EQ("bar", e.action);
	}
	ASSERT_EQ(1, action_count);
	return true;
}

bool testActionEventEmittedViaAncestorOnClick() {
	MockDocument doc;
	ASSERT_PARSE(doc, "[box width=200 height=100 on-click=parent] { [span text=hello] }");
	doc.computeLayout(Vec2(800, 600));

	MockMouseDevice mouse;
	injectMouseButton(doc, mouse, true, 10, 10);
	injectMouseButton(doc, mouse, false, 10, 10);

	int action_count = 0;
	for (const ui::Event& e : doc.getEvents()) {
		if (e.type != ui::EventType::ACTION) continue;
		++action_count;
		ASSERT_EQ(0, (int)e.element_index);
		ASSERT_EQ("parent", e.action);
	}
	ASSERT_EQ(1, action_count);
	return true;
}

bool testActionEventEmittedViaGrandparentOnClick() {
	MockDocument doc;
	ASSERT_PARSE(doc, "[box width=200 height=100 on-click=grand] { [box width=200 height=100] { [span text=hello] } }");
	doc.computeLayout(Vec2(800, 600));

	MockMouseDevice mouse;
	injectMouseButton(doc, mouse, true, 10, 10);
	injectMouseButton(doc, mouse, false, 10, 10);

	int action_count = 0;
	for (const ui::Event& e : doc.getEvents()) {
		if (e.type != ui::EventType::ACTION) continue;
		++action_count;
		ASSERT_EQ(0, (int)e.element_index);
		ASSERT_EQ("grand", e.action);
	}
	ASSERT_EQ(1, action_count);
	return true;
}

static void injectMouseMove(MockDocument& doc, MockMouseDevice& mouse, float x, float y) {
	InputSystem::Event ev;
	ev.device = &mouse;
	ev.type = InputEventType::AXIS;
	ev.data.axis.x_abs = x;
	ev.data.axis.y_abs = y;
	doc.injectEvent(ev);
}

static bool hasEvent(MockDocument& doc, ui::EventType type, u32 element_index) {
	for (const ui::Event& e : doc.getEvents()) {
		if (e.type == type && e.element_index == element_index) return true;
	}
	return false;
}

// mouse movement hit tests the layout of the mutations made before it, e.g. a box that was just made narrower is no longer hovered
bool testHoverUsesCurrentLayoutAfterMutation() {
	MockDocument doc;
	doc.m_canvas_size = Vec2(800, 600);
	ASSERT_PARSE(doc, "[box $a width=100 height=20] {}");
	doc.computeLayout(doc.m_canvas_size);
	ui::Element* a = doc.getElementByID("a");
	ASSERT_TRUE(a != nullptr);
	const u32 a_idx = u32(a - doc.m_elements.begin());

	MockMouseDevice mouse;
	injectMouseMove(doc, mouse, 75, 5);
	ASSERT_TRUE(hasEvent(doc, ui::EventType::MOUSE_ENTER, a_idx));
	doc.clearEvents();

	a->setWidth("50");
	injectMouseMove(doc, mouse, 76, 5);   // still over the old bounds, outside the new ones
	ASSERT_TRUE(hasEvent(doc, ui::EventType::MOUSE_LEAVE, a_idx));
	ASSERT_TRUE(!hasEvent(doc, ui::EventType::MOUSE_ENTER, a_idx));
	return true;
}

// a hidden element (and its subtree) keeps its last layout rect, but must not be hit by the mouse
bool testHoverIgnoresHiddenElements() {
	MockDocument doc;
	doc.m_canvas_size = Vec2(800, 600);
	ASSERT_PARSE(doc, "[box $a width=100 height=100] { [box $b width=50 height=50] {} }");
	doc.computeLayout(doc.m_canvas_size);
	ui::Element* a = doc.getElementByID("a");
	ui::Element* b = doc.getElementByID("b");
	ASSERT_TRUE(a != nullptr && b != nullptr);
	const u32 a_idx = u32(a - doc.m_elements.begin());
	const u32 b_idx = u32(b - doc.m_elements.begin());

	MockMouseDevice mouse;
	injectMouseMove(doc, mouse, 10, 10);
	ASSERT_TRUE(hasEvent(doc, ui::EventType::MOUSE_ENTER, a_idx));
	ASSERT_TRUE(hasEvent(doc, ui::EventType::MOUSE_ENTER, b_idx));
	doc.clearEvents();

	a->setVisible(false);
	injectMouseMove(doc, mouse, 11, 10);   // still inside the stale rects of a and b
	ASSERT_TRUE(hasEvent(doc, ui::EventType::MOUSE_LEAVE, a_idx));
	ASSERT_TRUE(hasEvent(doc, ui::EventType::MOUSE_LEAVE, b_idx));
	ASSERT_TRUE(!hasEvent(doc, ui::EventType::MOUSE_ENTER, a_idx));
	ASSERT_TRUE(!hasEvent(doc, ui::EventType::MOUSE_ENTER, b_idx));
	doc.clearEvents();

	injectMouseMove(doc, mouse, 12, 10);
	ASSERT_TRUE(!hasEvent(doc, ui::EventType::MOUSE_ENTER, a_idx));
	ASSERT_TRUE(!hasEvent(doc, ui::EventType::MOUSE_ENTER, b_idx));
	return true;
}

bool testHoverEvents() {
	// TODO better test
	MockDocument doc;
	doc.m_canvas_size = Vec2(800, 600);
	ASSERT_PARSE(doc, "[box .p1 width=100 height=100] {} [box .p2 width=100 height=100] {}");
	doc.computeLayout(doc.m_canvas_size);

	MockMouseDevice mouse;
	InputSystem::Event ev;
	ev.device = &mouse;
	ev.type = InputEventType::AXIS;
	ev.data.axis.x_abs = 50;
	ev.data.axis.y_abs = 50;

	doc.injectEvent(ev);
	
	{
		Span<const ui::Event> events = doc.getEvents();
		bool found_enter = false;
		for (const ui::Event& e : events) {
			if (e.type == ui::EventType::MOUSE_ENTER && e.element_index == 0) {
				found_enter = true;
			}
		}
		if (!found_enter) { logError("Initial MOUSE_ENTER not found for index 0"); return false; }
	}
	doc.clearEvents();

	ev.data.axis.y_abs = 150;
	doc.injectEvent(ev);

	{
		Span<const ui::Event> events = doc.getEvents();
		bool found_leave = false;
		bool found_enter = false;
		u32 leave_idx = 0xFFFF'FFFF;
		u32 enter_idx = 0xFFFF'FFFF;

		for (const ui::Event& e : events) {
			if (e.type == ui::EventType::MOUSE_LEAVE) {
				found_leave = true;
				leave_idx = e.element_index;
			}
			if (e.type == ui::EventType::MOUSE_ENTER) {
				found_enter = true;
				enter_idx = e.element_index;
			}
		}
		if (!found_leave) { logError("MOUSE_LEAVE not found"); return false; }
		ASSERT_EQ(0, (int)leave_idx);
		if (!found_enter) { logError("MOUSE_ENTER not found"); return false; }
		ASSERT_EQ(1, (int)enter_idx);
	}
	doc.clearEvents();

	ev.data.axis.x_abs = 300;
	ev.data.axis.y_abs = 300;
	doc.injectEvent(ev);

	{
		Span<const ui::Event> events = doc.getEvents();
		bool found_leave = false;
		bool found_enter = false;
		u32 leave_idx = 0xFFFF'FFFF;

		for (const ui::Event& e : events) {
			if (e.type == ui::EventType::MOUSE_LEAVE) {
				found_leave = true;
				leave_idx = e.element_index;
			}
			if (e.type == ui::EventType::MOUSE_ENTER) {
				found_enter = true;
			}
		}
		if (!found_leave) { logError("MOUSE_LEAVE not found when leaving elements"); return false; }
		ASSERT_EQ(1, (int)leave_idx);
		if (found_enter) { logError("Unexpected MOUSE_ENTER when moving to empty space"); return false; }
	}

	return true;
}

bool testDPIScaling() {
	MockDocument doc;
	ASSERT_PARSE(doc, "[box width=10 height=10] { [span font-size=10 text=\"abcd\"] }");
	doc.computeLayout(Vec2(200, 200));

	Element* root = doc.getElement(doc.m_root.children[0]);
	ASSERT_FLOAT_EQ(10.0f, root->size.x);
	ASSERT_FLOAT_EQ(10.0f, root->size.y);
	ASSERT_EQ(1, root->children.size());
	Element* span = doc.getElement(root->children[0]);
	ASSERT_FLOAT_EQ(20.0f, span->size.x);

	doc.setDPIScale(2.0f);
	doc.flushLayout();

	ASSERT_FLOAT_EQ(20.0f, root->size.x);
	ASSERT_FLOAT_EQ(20.0f, root->size.y);
	ASSERT_FLOAT_EQ(40.0f, span->size.x);
	return true;
}

bool testDocumentReferenceSize() {
	MockDocument doc;
	const char* body = "[box width=10 height=10 font-size=20] { [box width=2em height=1em] {} }";

	// canvas matching the reference size: scale 1
	ASSERT_PARSE(doc, "[doc reference-width=100 reference-height=50]\n[box width=10 height=10 font-size=20] { [box width=2em height=1em] {} }");
	doc.computeLayout(Vec2(100, 50));
	Element* root = doc.getElement(doc.m_root.children[0]);
	Element* child = doc.getElement(root->children[0]);
	ASSERT_FLOAT_EQ(1.0f, doc.getScale());
	ASSERT_FLOAT_EQ(10.0f, root->size.x);
	ASSERT_FLOAT_EQ(40.0f, child->size.x);

	// twice the reference: px and em values double, the OS DPI scale is ignored
	doc.setDPIScale(3.0f);
	doc.computeLayout(Vec2(200, 100));
	ASSERT_FLOAT_EQ(2.0f, doc.getScale());
	ASSERT_FLOAT_EQ(20.0f, root->size.x);
	ASSERT_FLOAT_EQ(80.0f, child->size.x);

	// match=width with a different aspect ratio
	ASSERT_PARSE(doc, "[doc reference-width=100 reference-height=50 match=width]\n[box width=10 height=10] {}");
	doc.computeLayout(Vec2(300, 100));
	ASSERT_FLOAT_EQ(3.0f, doc.getScale());

	// only a reference height: height is matched, and max-scale clamps
	ASSERT_PARSE(doc, "[doc reference-height=50 max-scale=1.5]\n[box width=10 height=10] {}");
	doc.computeLayout(Vec2(300, 200));
	ASSERT_FLOAT_EQ(1.5f, doc.getScale());

	// with a body its content are the root elements, [style] included
	ASSERT_PARSE(doc, "[doc reference-height=50] { [style] { .a { width: 10; } } [box .a height=10] {} [box] {} }");
	ASSERT_EQ(2, doc.m_root.children.size());
	doc.computeLayout(Vec2(100, 100));
	ASSERT_FLOAT_EQ(2.0f, doc.getScale());
	ASSERT_FLOAT_EQ(20.0f, doc.getElement(doc.m_root.children[0])->size.x);

	// no [doc]: the OS scale is used as is
	ASSERT_PARSE(doc, body);
	doc.computeLayout(Vec2(300, 200));
	ASSERT_FLOAT_EQ(3.0f, doc.getScale());

	doc.m_suppress_logging = true;
	ASSERT_EQ(false, doc.parse("[doc reference-height=abc]", "test.ui"));
	ASSERT_EQ(false, doc.parse("[doc match=diagonal]", "test.ui"));
	ASSERT_EQ(false, doc.parse("[doc unknown=1]", "test.ui"));
	ASSERT_EQ(false, doc.parse("[doc reference-height=10] [doc reference-height=20]", "test.ui"));
	ASSERT_EQ(false, doc.parse("[box] { [doc reference-height=10] }", "test.ui"));
	ASSERT_EQ(false, doc.parse("[doc reference-height=10] { [box] {} ", "test.ui"));
	ASSERT_EQ(false, doc.parse("[box] {} }", "test.ui"));
	return true;
}

bool testInvalidAttributeValuesRejected() {
	MockDocument doc;
	doc.m_suppress_logging = true;

	// enum-like attributes
	ASSERT_EQ(false, doc.parse("[box direction=diagonal]", "test.ui"));
	ASSERT_EQ(false, doc.parse("[box justify-content=around]", "test.ui"));
	ASSERT_EQ(false, doc.parse("[box align-items=baseline]", "test.ui"));
	ASSERT_EQ(false, doc.parse("[box align=middle]", "test.ui"));
	//ASSERT_EQ(false, doc.parse("[box position=floating]", "test.ui"));

	// boolean attributes
	ASSERT_EQ(false, doc.parse("[box visible=maybe]", "test.ui"));
	ASSERT_EQ(false, doc.parse("[box clipping=yes]", "test.ui"));
	ASSERT_EQ(false, doc.parse("[box wrap=on]", "test.ui"));

	// color and opacity
	ASSERT_EQ(false, doc.parse("[box color=red]", "test.ui"));
	ASSERT_EQ(false, doc.parse("[box color=#12345]", "test.ui"));
	ASSERT_EQ(false, doc.parse("[box color=#zzzzzz]", "test.ui"));
	ASSERT_EQ(false, doc.parse("[box bg-color=#12345]", "test.ui"));
	ASSERT_EQ(false, doc.parse("[box opacity=abc]", "test.ui"));
	ASSERT_EQ(false, doc.parse("[box font-size=\"12abc\"]", "test.ui"));
	ASSERT_EQ(false, doc.parse("[box color=\"#ff0000\"]", "test.ui"));

	// unit-based attributes
	ASSERT_EQ(false, doc.parse("[box left=badem]", "test.ui"));
	ASSERT_EQ(false, doc.parse("[box top=oops]", "test.ui"));
	ASSERT_EQ(false, doc.parse("[box width=wat]", "test.ui"));
	ASSERT_EQ(false, doc.parse("[box height=nope%]", "test.ui"));
	ASSERT_EQ(false, doc.parse("[box margin=bad]", "test.ui"));
	ASSERT_EQ(false, doc.parse("[box padding=bad]", "test.ui"));
	ASSERT_EQ(false, doc.parse("[box pivot-x=wat]", "test.ui"));
	ASSERT_EQ(false, doc.parse("[box pivot-y=wat]", "test.ui"));

	// additional constrained attributes
	ASSERT_EQ(false, doc.parse("[box grow=fast]", "test.ui"));
	ASSERT_EQ(false, doc.parse("[box font-size=big]", "test.ui"));
	//ASSERT_EQ(false, doc.parse("[box bg-fit=stretch]", "test.ui"));

	ASSERT_EQ(false, doc.parse("[box margin-left=bad]", "test.ui"));
	ASSERT_EQ(false, doc.parse("[box margin-right=bad]", "test.ui"));
	ASSERT_EQ(false, doc.parse("[box margin-top=bad]", "test.ui"));
	ASSERT_EQ(false, doc.parse("[box margin-bottom=bad]", "test.ui"));

	ASSERT_EQ(false, doc.parse("[box padding-left=bad]", "test.ui"));
	ASSERT_EQ(false, doc.parse("[box padding-right=bad]", "test.ui"));
	ASSERT_EQ(false, doc.parse("[box padding-top=bad]", "test.ui"));
	ASSERT_EQ(false, doc.parse("[box padding-bottom=bad]", "test.ui"));

	return true;
}

bool testCanvasParse() {
	MockDocument doc;
	ASSERT_PARSE(doc, "[canvas width=10 height=20]");
	doc.computeLayout(Vec2(200, 200));
	Element* canvas = doc.getElement(doc.m_root.children[0]);
	ASSERT_TAG(canvas, CANVAS);
	ASSERT_FLOAT_EQ(10.0f, canvas->size.x);
	ASSERT_FLOAT_EQ(20.0f, canvas->size.y);
	return true;
}

bool testCanvasDefaultSizeIsZero() {
	MockDocument doc;
	ASSERT_PARSE(doc, "[canvas]");
	doc.computeLayout(Vec2(200, 200));
	Element* canvas = doc.getElement(doc.m_root.children[0]);
	ASSERT_FLOAT_EQ(0.0f, canvas->size.x);
	ASSERT_FLOAT_EQ(0.0f, canvas->size.y);
	return true;
}

bool testCanvasRejectsOnClick() {
	MockDocument doc;
	doc.m_suppress_logging = true;
	ASSERT_EQ(false, doc.parse("[canvas on-click=\"go\"]", "test.ui"));
	return true;
}

bool testCanvasSetGeometryEmitsTriangle() {
	MockDocument doc;
	ASSERT_PARSE(doc, "[canvas width=100 height=100]");
	doc.computeLayout(Vec2(200, 200));
	Element* canvas = doc.getElement(doc.m_root.children[0]);

	ui::Vertex vertices[] = {
		{ 0, 0, 0, 0, 255, 0, 0, 255 },
		{ 10, 0, 0, 0, 255, 0, 0, 255 },
		{ 0, 10, 0, 0, 255, 0, 0, 255 },
	};
	u32 indices[] = { 0, 1, 2 };
	canvas->setGeometry(Span(vertices, 3), Span(indices, 3));

	Draw2D draw(getGlobalAllocator());
	doc.render(draw);
	ASSERT_EQ(3, draw.getVertices().size());
	ASSERT_EQ(3, draw.getIndices().size());
	ASSERT_FLOAT_EQ(canvas->position.x + 10.0f * doc.getScale(), draw.getVertices()[1].pos.x);
	ASSERT_FLOAT_EQ(canvas->position.y, draw.getVertices()[1].pos.y);
	ASSERT_EQ(0, draw.getIndices()[0]);
	ASSERT_EQ(1, draw.getIndices()[1]);
	ASSERT_EQ(2, draw.getIndices()[2]);
	ASSERT_EQ(255, draw.getVertices()[0].color.r);
	ASSERT_EQ(0, draw.getVertices()[0].color.g);
	return true;
}

bool testCanvasGeometryRebasesIndices() {
	MockDocument doc;
	ASSERT_PARSE(doc, "[canvas width=50 height=50] [canvas width=50 height=50]");
	doc.computeLayout(Vec2(200, 200));
	ui::Vertex vertices[] = {
		{ 0, 0, 0, 0, 255, 255, 255, 255 },
		{ 10, 0, 0, 0, 255, 255, 255, 255 },
		{ 0, 10, 0, 0, 255, 255, 255, 255 },
	};
	u32 indices[] = { 0, 1, 2 };
	doc.getElement(doc.m_root.children[0])->setGeometry(Span(vertices, 3), Span(indices, 3));
	doc.getElement(doc.m_root.children[1])->setGeometry(Span(vertices, 3), Span(indices, 3));

	Draw2D draw(getGlobalAllocator());
	doc.render(draw);
	ASSERT_EQ(6, draw.getVertices().size());
	ASSERT_EQ(3, draw.getIndices()[3]);
	ASSERT_EQ(4, draw.getIndices()[4]);
	ASSERT_EQ(5, draw.getIndices()[5]);
	return true;
}

bool testCanvasInvalidGeometryRejected() {
	MockDocument doc;
	doc.m_suppress_logging = true;
	ASSERT_PARSE(doc, "[canvas width=100 height=100]");
	doc.computeLayout(Vec2(200, 200));
	Element* canvas = doc.getElement(doc.m_root.children[0]);

	ui::Vertex vertices[] = {
		{ 0, 0, 0, 0, 255, 0, 0, 255 },
		{ 10, 0, 0, 0, 255, 0, 0, 255 },
		{ 0, 10, 0, 0, 255, 0, 0, 255 },
	};
	u32 good[] = { 0, 1, 2 };
	canvas->setGeometry(Span(vertices, 3), Span(good, 3));

	// index count not a multiple of 3: rejected, the previous geometry stays
	u32 bad_count[] = { 0, 1, 2, 0 };
	canvas->setGeometry(Span(vertices, 3), Span(bad_count, 4));

	// index past the vertex count: rejected, the previous geometry stays
	u32 bad_index[] = { 0, 1, 3 };
	canvas->setGeometry(Span(vertices, 3), Span(bad_index, 3));

	Draw2D draw(getGlobalAllocator());
	doc.render(draw);
	ASSERT_EQ(3, draw.getVertices().size());
	ASSERT_EQ(3, draw.getIndices().size());
	return true;
}

bool testCanvasClearGeometry() {
	MockDocument doc;
	ASSERT_PARSE(doc, "[canvas width=100 height=100]");
	doc.computeLayout(Vec2(200, 200));
	Element* canvas = doc.getElement(doc.m_root.children[0]);

	ui::Vertex vertices[] = {
		{ 0, 0, 0, 0, 255, 0, 0, 255 },
		{ 10, 0, 0, 0, 255, 0, 0, 255 },
		{ 0, 10, 0, 0, 255, 0, 0, 255 },
	};
	u32 indices[] = { 0, 1, 2 };
	canvas->setGeometry(Span(vertices, 3), Span(indices, 3));
	canvas->clearGeometry();

	Draw2D draw(getGlobalAllocator());
	doc.render(draw);
	ASSERT_EQ(0, draw.getVertices().size());
	ASSERT_EQ(0, draw.getIndices().size());
	return true;
}

bool testCanvasSetGeometryOnBoxIgnored() {
	MockDocument doc;
	doc.m_suppress_logging = true;
	ASSERT_PARSE(doc, "[box width=100 height=100]");
	doc.computeLayout(Vec2(200, 200));
	Element* box = doc.getElement(doc.m_root.children[0]);

	ui::Vertex vertices[] = {
		{ 0, 0, 0, 0, 255, 0, 0, 255 },
		{ 10, 0, 0, 0, 255, 0, 0, 255 },
		{ 0, 10, 0, 0, 255, 0, 0, 255 },
	};
	u32 indices[] = { 0, 1, 2 };
	box->setGeometry(Span(vertices, 3), Span(indices, 3));

	Draw2D draw(getGlobalAllocator());
	doc.render(draw);
	ASSERT_EQ(0, draw.getVertices().size());
	return true;
}

bool testCanvasGeometryDrawnBeforeChildren() {
	MockDocument doc;
	ASSERT_PARSE(doc, "[canvas width=100 height=100] { [box width=10 height=10 bg-color=#00ff00] }");
	doc.computeLayout(Vec2(200, 200));
	Element* canvas = doc.getElement(doc.m_root.children[0]);
	Element* child = doc.getElement(canvas->children[0]);

	ui::Vertex vertices[] = {
		{ 0, 0, 0, 0, 255, 0, 0, 255 },
		{ 10, 0, 0, 0, 255, 0, 0, 255 },
		{ 0, 10, 0, 0, 255, 0, 0, 255 },
	};
	u32 indices[] = { 0, 1, 2 };
	canvas->setGeometry(Span(vertices, 3), Span(indices, 3));

	Draw2D draw(getGlobalAllocator());
	doc.render(draw);
	// the 3 geometry vertices come first, then the child box's quad
	ASSERT_EQ(7, draw.getVertices().size());
	ASSERT_FLOAT_EQ(child->position.x, draw.getVertices()[3].pos.x);
	ASSERT_FLOAT_EQ(child->position.y, draw.getVertices()[3].pos.y);
	return true;
}

bool testCanvasGeometryScalesWithDPI() {
	MockDocument doc;
	ASSERT_PARSE(doc, "[canvas width=100 height=100]");
	doc.setDPIScale(2.0f);
	doc.computeLayout(Vec2(200, 200));
	Element* canvas = doc.getElement(doc.m_root.children[0]);

	ui::Vertex vertices[] = {
		{ 0, 0, 0, 0, 255, 0, 0, 255 },
		{ 10, 0, 0, 0, 255, 0, 0, 255 },
		{ 0, 10, 0, 0, 255, 0, 0, 255 },
	};
	u32 indices[] = { 0, 1, 2 };
	canvas->setGeometry(Span(vertices, 3), Span(indices, 3));

	Draw2D draw(getGlobalAllocator());
	doc.render(draw);
	ASSERT_FLOAT_EQ(2.0f, doc.getScale());
	ASSERT_FLOAT_EQ(canvas->position.x + 20.0f, draw.getVertices()[1].pos.x);
	return true;
}

bool testCanvasSrcLoadsImage() {
	MockDocument doc;
	ASSERT_PARSE(doc, "[canvas width=10 height=10 src=\"img.png\"]");
	doc.computeLayout(Vec2(200, 200));
	Element* canvas = doc.getElement(doc.m_root.children[0]);
	ASSERT_TRUE(canvas->getImageHandle() != nullptr);
	return true;
}

bool testCanvasTextureNotReadyDrawsNothing() {
	MockDocument doc;
	doc.m_image_manager.ready = false;
	ASSERT_PARSE(doc, "[canvas width=100 height=100 src=\"img.png\"]");
	doc.computeLayout(Vec2(200, 200));
	Element* canvas = doc.getElement(doc.m_root.children[0]);

	ui::Vertex vertices[] = {
		{ 0, 0, 0, 0, 255, 255, 255, 255 },
		{ 10, 0, 1, 0, 255, 255, 255, 255 },
		{ 0, 10, 0, 1, 255, 255, 255, 255 },
	};
	u32 indices[] = { 0, 1, 2 };
	canvas->setGeometry(Span(vertices, 3), Span(indices, 3));

	Draw2D draw(getGlobalAllocator());
	doc.render(draw);
	ASSERT_EQ(0, draw.getVertices().size());
	return true;
}

// [path] inside [canvas], see docs/ui/elements_attributes.md "Shapes"

static u32 renderVertexCount(MockDocument& doc) {
	Draw2D draw(getGlobalAllocator());
	doc.render(draw);
	return draw.getVertices().size();
}

static u32 renderIndexCount(MockDocument& doc) {
	Draw2D draw(getGlobalAllocator());
	doc.render(draw);
	return draw.getIndices().size();
}

bool testPathDrawsStroke() {
	MockDocument doc;
	ASSERT_PARSE(doc, "[canvas width=100 height=100] { [path width=2] { 0,0#ffffff 10,0 } }");
	doc.computeLayout(Vec2(200, 200));

	ASSERT_TRUE(renderVertexCount(doc) > 0);
	ASSERT_TRUE(renderIndexCount(doc) > 0);
	return true;
}

bool testPathClosedAddsSegment() {
	MockDocument open_doc;
	ASSERT_PARSE(open_doc, "[canvas width=100 height=100] { [path width=2] { 0,0#ffffff 10,0 5,10 } }");
	open_doc.computeLayout(Vec2(200, 200));

	MockDocument closed_doc;
	ASSERT_PARSE(closed_doc, "[canvas width=100 height=100] { [path width=2 closed=true] { 0,0#ffffff 10,0 5,10 } }");
	closed_doc.computeLayout(Vec2(200, 200));

	ASSERT_TRUE(renderIndexCount(closed_doc) > renderIndexCount(open_doc));
	return true;
}

// the first count indices of the canvas geometry, the fill triangles, only use vertices that are not feathered
static bool firstIndicesOpaque(MockDocument& doc, u32 count) {
	Element* canvas = doc.getElement(doc.m_root.children[0]);
	if (!canvas->canvas_geometry) return false;
	const CanvasGeometry& geometry = *canvas->canvas_geometry;
	for (u32 i = 0; i < count; ++i) {
		if (geometry.vertices[geometry.indices[i]].color.a != 255) return false;
	}
	return true;
}

bool testPathFeatherFades() {
	MockDocument stroke_doc;
	ASSERT_PARSE(stroke_doc, "[canvas width=100 height=100] { [path width=2] { 0,0#ffffff 10,0 10,10 } }");
	stroke_doc.computeLayout(Vec2(200, 200));
	Element* canvas = stroke_doc.getElement(stroke_doc.m_root.children[0]);
	ASSERT_TRUE(canvas->canvas_geometry != nullptr);

	// the stroke is opaque, its feathered edges are transparent
	u32 opaque = 0, transparent = 0;
	for (const Draw2D::Vertex& v : canvas->canvas_geometry->vertices) {
		if (v.color.a == 255) ++opaque;
		else if (v.color.a == 0) ++transparent;
	}
	ASSERT_TRUE(opaque > 0);
	ASSERT_TRUE(transparent > 0);

	MockDocument fill_doc;
	ASSERT_PARSE(fill_doc, "[canvas width=100 height=100] { [path filled=true] { 0,0#ffffff 10,0 5,10 } }");
	fill_doc.computeLayout(Vec2(200, 200));
	canvas = fill_doc.getElement(fill_doc.m_root.children[0]);
	ASSERT_TRUE(canvas->canvas_geometry != nullptr);
	bool has_transparent = false;
	for (const Draw2D::Vertex& v : canvas->canvas_geometry->vertices) {
		if (v.color.a == 0) has_transparent = true;
	}
	ASSERT_TRUE(has_transparent);
	return true;
}

bool testPathFilled() {
	// a filled triangle is 3 vertices and 1 triangle, a stroke is only added with an explicit width
	MockDocument fill_doc;
	ASSERT_PARSE(fill_doc, "[canvas width=100 height=100] { [path filled=true] { 0,0#ffffff 10,0 5,10 } }");
	fill_doc.computeLayout(Vec2(200, 200));
	ASSERT_TRUE(renderIndexCount(fill_doc) > 3);
	ASSERT_TRUE(firstIndicesOpaque(fill_doc, 3));

	MockDocument stroked_doc;
	ASSERT_PARSE(stroked_doc, "[canvas width=100 height=100] { [path filled=true width=2] { 0,0#ffffff 10,0 5,10 } }");
	stroked_doc.computeLayout(Vec2(200, 200));
	ASSERT_TRUE(renderIndexCount(stroked_doc) > 3);

	// a concave quad (an arrow head) is two triangles
	MockDocument concave_doc;
	ASSERT_PARSE(concave_doc, "[canvas width=100 height=100] { [path filled=true] { 0,0#ffffff 10,5 0,10 4,5 } }");
	concave_doc.computeLayout(Vec2(200, 200));
	ASSERT_TRUE(firstIndicesOpaque(concave_doc, 6));
	return true;
}

bool testPathPointColors() {
	MockDocument doc;
	ASSERT_PARSE(doc, "[canvas width=100 height=100] { [path width=2] { 0,0#ff0000 10,0#00ff00 } }");
	doc.computeLayout(Vec2(200, 200));

	Draw2D draw(getGlobalAllocator());
	doc.render(draw);
	bool has_red = false;
	bool has_green = false;
	for (const Draw2D::Vertex& v : draw.getVertices()) {
		if (v.color.r == 255 && v.color.g == 0 && v.color.b == 0) has_red = true;
		if (v.color.r == 0 && v.color.g == 255 && v.color.b == 0) has_green = true;
	}
	ASSERT_TRUE(has_red);
	ASSERT_TRUE(has_green);
	return true;
}

bool testPathPointUVWithSrc() {
	MockDocument doc;
	// checked on the geometry, rendering a textured canvas needs a real gpu texture
	ASSERT_PARSE(doc, "[canvas width=100 height=100 src=\"img.png\"] { [path width=2] { 0,0#ffffff@0,0 10,0@1,0 } }");
	doc.computeLayout(Vec2(200, 200));

	Element* canvas = doc.getElement(doc.m_root.children[0]);
	ASSERT_TRUE(canvas->canvas_geometry != nullptr);
	bool has_uv_1 = false;
	for (const Draw2D::Vertex& v : canvas->canvas_geometry->vertices) {
		if (v.uv.x == 1.0f && v.uv.y == 0.0f) has_uv_1 = true;
	}
	ASSERT_TRUE(has_uv_1);
	return true;
}

bool testPathMalformedPointIgnored() {
	MockDocument doc;
	ASSERT_PARSE(doc, "[canvas width=100 height=100] { [path width=2] { 0,0#ffffff abc 10,0 } }");
	doc.computeLayout(Vec2(200, 200));

	// the bad token is logged and skipped, the two valid points still draw
	ASSERT_TRUE(renderVertexCount(doc) > 0);
	return true;
}

bool testPathOutsideCanvasRejected() {
	MockDocument doc;
	ASSERT_EQ(false, doc.parse("[path] { 0,0 1,1 }", "test.ui"));
	return true;
}

bool testPathPointWithoutColorInheritsPrevious() {
	MockDocument doc;
	ASSERT_PARSE(doc, "[canvas width=100 height=100] { [path width=2] { 0,0#ff0000 10,0 5,10#00ff00 0,5 } }");
	doc.computeLayout(Vec2(200, 200));

	Element* canvas = doc.getElement(doc.m_root.children[0]);
	ASSERT_TRUE(canvas->canvas_geometry != nullptr);
	// every stroke vertex is red or green, the points without a color took the one before them
	u32 red = 0, green = 0;
	for (const Draw2D::Vertex& v : canvas->canvas_geometry->vertices) {
		if (v.color.r == 255 && v.color.g == 0 && v.color.b == 0) ++red;
		else if (v.color.r == 0 && v.color.g == 255 && v.color.b == 0) ++green;
		else ASSERT_TRUE(false);
	}
	ASSERT_TRUE(red > 0);
	ASSERT_TRUE(green > 0);
	return true;
}

bool testPathFirstPointNeedsColor() {
	MockDocument doc;
	ASSERT_EQ(false, doc.parse("[canvas width=100 height=100] { [path width=2] { 0,0 10,0#ff0000 } }", "test.ui"));
	return true;
}

bool testPathColorAttributeRejected() {
	MockDocument doc;
	ASSERT_EQ(false, doc.parse("[canvas width=100 height=100] { [path color=#ff0000 width=2] { 0,0#ff0000 10,0 } }", "test.ui"));
	return true;
}

bool testDraw2DMeshWithTextureKeepsUV() {
	Draw2D draw(getGlobalAllocator());
	draw.clear(Vec2(64, 64));
	const gpu::TextureHandle tex = (gpu::TextureHandle)0x1;
	const Color white(255, 255, 255, 255);
	Draw2D::Vertex vertices[] = {
		{ Vec2(0, 0), Vec2(0.25f, 0.5f), white },
		{ Vec2(10, 0), Vec2(0.75f, 0.5f), white },
		{ Vec2(0, 10), Vec2(0.25f, 1.0f), white },
	};
	u32 indices[] = { 0, 1, 2 };
	draw.addMesh(tex, Span(vertices, 3), Span(indices, 3), Vec2(5, 5), 2.0f);

	ASSERT_TRUE(draw.getCmds().back().texture == tex);
	ASSERT_FLOAT_EQ(0.25f, draw.getVertices()[0].uv.x);
	ASSERT_FLOAT_EQ(0.5f, draw.getVertices()[0].uv.y);
	ASSERT_FLOAT_EQ(0.75f, draw.getVertices()[1].uv.x);
	ASSERT_FLOAT_EQ(5.0f, draw.getVertices()[0].pos.x);
	ASSERT_FLOAT_EQ(25.0f, draw.getVertices()[1].pos.x);
	return true;
}

bool testCanvasGeometryAllocatedOnDemand() {
	MockDocument doc;
	ASSERT_PARSE(doc, "[box width=10 height=10] [canvas width=10 height=10]");
	doc.computeLayout(Vec2(200, 200));
	Element* box = doc.getElement(doc.m_root.children[0]);
	Element* canvas = doc.getElement(doc.m_root.children[1]);
	ASSERT_TRUE(box->canvas_geometry == nullptr);
	ASSERT_TRUE(canvas->canvas_geometry == nullptr);

	ui::Vertex vertices[] = {
		{ 0, 0, 0, 0, 255, 0, 0, 255 },
		{ 10, 0, 0, 0, 255, 0, 0, 255 },
		{ 0, 10, 0, 0, 255, 0, 0, 255 },
	};
	u32 indices[] = { 0, 1, 2 };
	canvas->setGeometry(Span(vertices, 3), Span(indices, 3));
	ASSERT_TRUE(canvas->canvas_geometry != nullptr);
	ASSERT_EQ(3, canvas->canvas_geometry->vertices.size());

	canvas->clearGeometry();
	ASSERT_TRUE(canvas->canvas_geometry == nullptr);
	return true;
}

bool testCanvasSetGeometryEmptyVerticesClears() {
	MockDocument doc;
	ASSERT_PARSE(doc, "[canvas width=10 height=10]");
	doc.computeLayout(Vec2(200, 200));
	Element* canvas = doc.getElement(doc.m_root.children[0]);

	// empty vertices does not allocate geometry
	canvas->setGeometry(Span<const ui::Vertex>(), Span<const u32>());
	ASSERT_TRUE(canvas->canvas_geometry == nullptr);

	// empty vertices clears geometry set earlier
	ui::Vertex vertices[] = {
		{ 0, 0, 0, 0, 255, 0, 0, 255 },
		{ 10, 0, 0, 0, 255, 0, 0, 255 },
		{ 0, 10, 0, 0, 255, 0, 0, 255 },
	};
	u32 indices[] = { 0, 1, 2 };
	canvas->setGeometry(Span(vertices, 3), Span(indices, 3));
	ASSERT_TRUE(canvas->canvas_geometry != nullptr);
	canvas->setGeometry(Span<const ui::Vertex>(), Span<const u32>());
	ASSERT_TRUE(canvas->canvas_geometry == nullptr);

	Draw2D draw(getGlobalAllocator());
	doc.render(draw);
	ASSERT_EQ(0, draw.getVertices().size());
	return true;
}

// the EVOX vertex and the draw vertex share one layout: floats for the position and uv, then the color bytes
static_assert(sizeof(ui::Vertex) == 20);
static_assert(sizeof(Draw2D::Vertex) == sizeof(ui::Vertex));
static_assert(offsetof(ui::Vertex, r) == 16);

bool testCanvasColorBytesReachDraw() {
	MockDocument doc;
	ASSERT_PARSE(doc, "[canvas width=100 height=100]");
	doc.computeLayout(Vec2(200, 200));
	Element* canvas = doc.getElement(doc.m_root.children[0]);

	ui::Vertex vertices[] = {
		{ 0, 0, 0, 0, 255, 128, 0, 255 },
		{ 10, 0, 0, 0, 255, 128, 0, 255 },
		{ 0, 10, 0, 0, 255, 128, 0, 255 },
	};
	u32 indices[] = { 0, 1, 2 };
	canvas->setGeometry(Span(vertices, 3), Span(indices, 3));

	Draw2D draw(getGlobalAllocator());
	doc.render(draw);
	ASSERT_EQ(255, draw.getVertices()[0].color.r);
	ASSERT_EQ(128, draw.getVertices()[0].color.g);
	ASSERT_EQ(0, draw.getVertices()[0].color.b);
	ASSERT_EQ(255, draw.getVertices()[0].color.a);
	return true;
}

} // namespace

void runUITests() {
	logInfo("=== Running UI Tests ===");
	
	RUN_TEST(testDocumentParseEmpty);
	RUN_TEST(testInternTableSemantics);
	RUN_TEST(testDocumentParseSimple);
	RUN_TEST(testDocumentParseInvalidClosingBrace);
	RUN_TEST(testDocumentParseNested);
	RUN_TEST(testDocumentParseComplexNesting);
	RUN_TEST(testAttributes);
	RUN_TEST(testEveryElementAttributes);
	RUN_TEST(testMultipleClasses);
	RUN_TEST(testSingleClass);
	RUN_TEST(testDuplicateClasses);
	RUN_TEST(testBlockAttributes);
	RUN_TEST(testTopLeftAttributesParse);
	RUN_TEST(testPivotAttributesParse);
	RUN_TEST(testPositionAttributeParse);
	RUN_TEST(testPositionPresetAttributeParse);
	RUN_TEST(testClippingAttributeParse);
	RUN_TEST(testPositionPresetsLayout);
	RUN_TEST(testFitContentHeightIgnoresAbsoluteChildren);
	RUN_TEST(testPanelAttributes);
	RUN_TEST(testImageAttributes);
	RUN_TEST(testDefaultValues);
	RUN_TEST(testSpanAndQuotedStringEquivalence);
	RUN_TEST(testSpanEmptyValue);
	RUN_TEST(testTopLevelSpanWithoutContainer);
	RUN_TEST(testFontAttribute);
	RUN_TEST(testFontSizeAttribute);
	RUN_TEST(testFontInheritance);
	RUN_TEST(testFontInheritanceDeep);
	RUN_TEST(testFontRefsDoNotLeakAcrossStyleRecompute);
	RUN_TEST(testImageRefsDoNotLeakAcrossStyleRecompute);
	RUN_TEST(testSpriteRefsDoNotLeakAcrossStyleRecompute);
	RUN_TEST(testDocumentDestructorReleasesFontRefs);
	RUN_TEST(testDocumentDestructorReleasesImageRefs);
	RUN_TEST(testDocumentDestructorReleasesSpriteRefs);
	RUN_TEST(testSpritesAreSharedPreloadedAndNotStale);
	RUN_TEST(testSetBGImageSharesSprite);
	RUN_TEST(testGetElementByIdFindsElements);
	RUN_TEST(testDuplicateIdIsAParseError);
	RUN_TEST(testGetElementByIdFollowsParse);
	RUN_TEST(testLayoutIsDeferredUntilFlush);
	RUN_TEST(testHitTestFlushesDeferredLayout);
	RUN_TEST(testHitTestPrefersLastRoot);
	RUN_TEST(testHitTestPrefersLastNestedChild);
	RUN_TEST(testColorInheritance);
	RUN_TEST(testColorInheritanceDeep);
	RUN_TEST(testColorAlphaSupport);
	RUN_TEST(testAlignInheritance);
	RUN_TEST(testOpacityInheritance);
	RUN_TEST(testOpacityInheritanceAndOverride);
	RUN_TEST(testMultilineStringMeasurement);
	RUN_TEST(testMultilineStringLayout);
	RUN_TEST(testTextWithSpecialChars);
	RUN_TEST(testSpaceBetweenSpans);
	RUN_TEST(testParseAndRuntimeMutation);
	RUN_TEST(testComplexMutationSequence);
	RUN_TEST(testSetWidthUpdatesPixelsAndLayout);
	RUN_TEST(testSetWidthUpdatesPercentAndLayout);
	RUN_TEST(testKeyboardInputEvent);
	RUN_TEST(testOnClickAttributeParseOnBox);
	RUN_TEST(testOnClickAttributeRejectedOnNonBox);
	RUN_TEST(testActionEventEmittedOnReleaseOverClickableBox);
	RUN_TEST(testActionEventPrefersLastRoot);
	RUN_TEST(testActionEventPrefersLastNestedChild);
	RUN_TEST(testActionEventNotEmittedWithoutOnClick);
	RUN_TEST(testActionEventNotEmittedForSpan);
	RUN_TEST(testActionEventNotEmittedOutside);
	RUN_TEST(testActionEventMultipleBoxesTargeting);
	RUN_TEST(testActionEventEmittedViaAncestorOnClick);
	RUN_TEST(testActionEventEmittedViaGrandparentOnClick);
	RUN_TEST(testHoverEvents);
	RUN_TEST(testHoverUsesCurrentLayoutAfterMutation);
	RUN_TEST(testHoverIgnoresHiddenElements);
	RUN_TEST(testDPIScaling);
	RUN_TEST(testDocumentReferenceSize);
	RUN_TEST(testInvalidAttributeValuesRejected);
	RUN_TEST(testCanvasParse);
	RUN_TEST(testCanvasDefaultSizeIsZero);
	RUN_TEST(testCanvasRejectsOnClick);
	RUN_TEST(testCanvasSetGeometryEmitsTriangle);
	RUN_TEST(testCanvasGeometryRebasesIndices);
	RUN_TEST(testCanvasInvalidGeometryRejected);
	RUN_TEST(testCanvasClearGeometry);
	RUN_TEST(testCanvasSetGeometryOnBoxIgnored);
	RUN_TEST(testCanvasGeometryDrawnBeforeChildren);
	RUN_TEST(testCanvasGeometryScalesWithDPI);
	RUN_TEST(testCanvasSrcLoadsImage);
	RUN_TEST(testCanvasTextureNotReadyDrawsNothing);
	RUN_TEST(testDraw2DMeshWithTextureKeepsUV);
	RUN_TEST(testCanvasGeometryAllocatedOnDemand);
	RUN_TEST(testCanvasSetGeometryEmptyVerticesClears);
	RUN_TEST(testPathDrawsStroke);
	RUN_TEST(testPathClosedAddsSegment);
	RUN_TEST(testPathFilled);
	RUN_TEST(testPathPointColors);
	RUN_TEST(testPathPointUVWithSrc);
	RUN_TEST(testPathMalformedPointIgnored);
	RUN_TEST(testPathOutsideCanvasRejected);
	RUN_TEST(testPathFeatherFades);
	RUN_TEST(testPathPointWithoutColorInheritsPrevious);
	RUN_TEST(testPathFirstPointNeedsColor);
	RUN_TEST(testPathColorAttributeRejected);
	RUN_TEST(testCanvasColorBytesReachDraw);
}
