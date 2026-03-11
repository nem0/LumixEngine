#include "core/log.h"
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
	ASSERT_PARSE(doc, "[box] { [box] }");
	ASSERT_EQ(1, doc.m_roots.size());
	ui::Element* root = doc.getElement(doc.m_roots[0]);
	ASSERT_EQ(1, root->children.size());
	ASSERT_TAG(root, BOX);
	ui::Element* child = doc.getElement(root->children[0]);
	ASSERT_TAG(child, BOX);
	return true;
}

bool testAttributes() {
	MockDocument doc;
	ASSERT_PARSE(doc, "[box width=800 height=640 id=\"someid\"] { [box] {} }");
	ASSERT_EQ(1, doc.m_roots.size());
	ui::Element* root = doc.getElement(doc.m_roots[0]);
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
	ASSERT_EQ(1, doc.m_roots.size());
	
	ui::Element* root = doc.getElement(doc.m_roots[0]);
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
	ASSERT_EQ(1, doc.m_roots.size());
	ui::Element* root = doc.getElement(doc.m_roots[0]);
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
	ASSERT_EQ(1, doc.m_roots.size());
	ui::Element* root = doc.getElement(doc.m_roots[0]);
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
	ASSERT_EQ(1, doc.m_roots.size());
	ui::Element* root = doc.getElement(doc.m_roots[0]);
	ASSERT_EQ(1, root->classes.size());
	StringView class_name = doc.m_intern_table.resolve(root->classes[0]);
	ASSERT_EQ(".singleclass", class_name);
	return true;
}

bool testDuplicateClasses() {
	MockDocument doc;
	ASSERT_PARSE(doc, "[box .dup .other .dup]");
	ASSERT_EQ(1, doc.m_roots.size());
	ui::Element* root = doc.getElement(doc.m_roots[0]);
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
	ASSERT_EQ(1, doc.m_roots.size());
	ui::Element* root = doc.getElement(doc.m_roots[0]);
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
	ASSERT_EQ(1, doc.m_roots.size());
	ui::Element* root = doc.getElement(doc.m_roots[0]);
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
	ASSERT_EQ(1, doc.m_roots.size());
	ui::Element* root = doc.getElement(doc.m_roots[0]);
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
		ASSERT_EQ(1, doc.m_roots.size());
		ui::Element* root = doc.getElement(doc.m_roots[0]);
		Span<ui::Attribute> attrs = root->attributes;
		ASSERT_EQ(1, attrs.size());
		ASSERT_ATTRIBUTE(root, 0, POSITION);
		ASSERT_EQ("relative", attrs[0].value);
	}
	{
		MockDocument doc;
		ASSERT_PARSE(doc, "[box position=absolute]");
		ASSERT_EQ(1, doc.m_roots.size());
		ui::Element* root = doc.getElement(doc.m_roots[0]);
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
	ASSERT_EQ(1, doc.m_roots.size());
	ui::Element* root = doc.getElement(doc.m_roots[0]);
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
		ASSERT_EQ(1, doc.m_roots.size());
		ui::Element* root = doc.getElement(doc.m_roots[0]);
		Span<ui::Attribute> attrs = root->attributes;
		ASSERT_EQ(1, attrs.size());
		ASSERT_ATTRIBUTE(root, 0, CLIPPING);
		ASSERT_EQ(true, attrs[0].clip);
	}
	{
		MockDocument doc;
		ASSERT_PARSE(doc, "[box clipping=false]");
		ASSERT_EQ(1, doc.m_roots.size());
		ui::Element* root = doc.getElement(doc.m_roots[0]);
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

		ASSERT_EQ(1, doc.m_roots.size());
		ui::Element* root = doc.getElement(doc.m_roots[0]);
		ASSERT_EQ(1, root->children.size());
		ui::Element* child = doc.getElement(root->children[0]);

		ASSERT_EQ((int)ui::PositionMode::ABSOLUTE, (int)child->position_mode);
		ASSERT_FLOAT_EQ(position_case.expected_x, child->position.x);
		ASSERT_FLOAT_EQ(position_case.expected_y, child->position.y);
	}

	return true;
}

bool testPanelAttributes() {
	MockDocument doc;
	ASSERT_PARSE(doc, "[box bg-image=\"bg.png\" bg-fit=cover bg-color=#000000 direction=column wrap=true justify-content=center]");
	ASSERT_EQ(1, doc.m_roots.size());
	ui::Element* root = doc.getElement(doc.m_roots[0]);
	Span<ui::Attribute> attrs = root->attributes;
	ASSERT_EQ(6, attrs.size());
	ASSERT_ATTRIBUTE(root, 0, BG_IMAGE);
	ASSERT_EQ("bg.png", attrs[0].value);
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
	ASSERT_PARSE(doc, "[box] {}");
	ASSERT_EQ(1, doc.m_roots.size());
	ui::Element* elem = doc.getElement(doc.m_roots[0]);
	
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
	ASSERT_EQ(1, doc1.m_roots.size());
	ui::Element* root1 = doc1.getElement(doc1.m_roots[0]);
	ASSERT_EQ(1, root1->children.size());
	ui::Element* child1 = doc1.getElement(root1->children[0]);
	ASSERT_TAG(child1, SPAN);
	Span<ui::Attribute> attrs1 = child1->attributes;
	ASSERT_EQ(0, attrs1.size());
	ASSERT_EQ("hello", child1->text);

	MockDocument doc2;
	ASSERT_PARSE(doc2, "[box] { hello }");
	ASSERT_EQ(1, doc2.m_roots.size());
	ui::Element* root2 = doc2.getElement(doc2.m_roots[0]);
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
	ASSERT_EQ(1, doc.m_roots.size());
	ui::Element* root = doc.getElement(doc.m_roots[0]);
	ASSERT_EQ(1, root->children.size());
	ui::Element* child = doc.getElement(root->children[0]);
	ASSERT_TAG(child, SPAN);
	Span<ui::Attribute> attrs = child->attributes;
	ASSERT_EQ(0, attrs.size());
	ASSERT_EQ("", child->text);

	return true;
}

bool testFontAttribute() {
	MockDocument doc;
	ASSERT_PARSE(doc, "[box font=\"arial.ttf\"] { [span text=\"hello\" font=\"times.ttf\"] }");
	ASSERT_EQ(1, doc.m_roots.size());
	ui::Element* root = doc.getElement(doc.m_roots[0]);
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
	ASSERT_EQ(1, doc.m_roots.size());
	ui::Element* root = doc.getElement(doc.m_roots[0]);
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
	ASSERT_EQ(1, doc.m_roots.size());
	ui::Element* root = doc.getElement(doc.m_roots[0]);
	doc.computeLayout(Vec2(800, 600));

	ui::Element* span = doc.getElement(root->children[0]);
	ASSERT_TRUE(span->font_handle != nullptr);

	return true;
}

bool testFontInheritanceDeep() {
	MockDocument doc;
	ASSERT_PARSE(doc, "[box font=\"arial.ttf\"] { [box] { hello } }");
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
	ASSERT_PARSE(doc, "[box color=#ff0000] { \"hello\" }");
	ASSERT_EQ(1, doc.m_roots.size());
	ui::Element* root = doc.getElement(doc.m_roots[0]);
	doc.computeLayout(Vec2(800, 600));

	ASSERT_TRUE(root->color.r == 255 && root->color.g == 0 && root->color.b == 0 && root->color.a == 255);

	ui::Element* span = doc.getElement(root->children[0]);
	ASSERT_TRUE(span->color.r == 255 && span->color.g == 0 && span->color.b == 0 && span->color.a == 255);
	return true;
}

bool testColorInheritanceDeep() {
	MockDocument doc;
	ASSERT_PARSE(doc, "[box color=#00ff00] { [box] { \"hello\" } }");
	ASSERT_EQ(1, doc.m_roots.size());
	ui::Element* root = doc.getElement(doc.m_roots[0]);
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
	ASSERT_EQ(1, doc.m_roots.size());
	doc.computeLayout(Vec2(800, 600));

	ui::Element* root = doc.getElement(doc.m_roots[0]);
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
	ASSERT_EQ(1, doc.m_roots.size());
	ui::Element* root = doc.getElement(doc.m_roots[0]);
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
	ASSERT_EQ(1, doc.m_roots.size());
	ui::Element* root = doc.getElement(doc.m_roots[0]);

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
	ASSERT_EQ(1, doc.m_roots.size());
	ui::Element* root = doc.getElement(doc.m_roots[0]);

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
	ASSERT_EQ(1, doc.m_roots.size());
	ui::Element* root = doc.getElement(doc.m_roots[0]);
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
	Span<u32> roots = doc.m_roots;
	ASSERT_EQ(1, doc.m_roots.size());
	ui::Element* root = doc.getElement(doc.m_roots[0]);
	
	bool has_width_100 = false;
	bool has_color_red = false;
	for (const ui::Attribute& attr : root->attributes) {
		if (attr.type == ui::AttributeName::WIDTH && attr.parsed_unit.unit == ui::Unit::PERCENT && attr.parsed_unit.value == 100.0f) has_width_100 = true;
		if (attr.type == ui::AttributeName::COLOR && attr.color == Color::RED) has_color_red = true;
	}
	ASSERT_TRUE(has_width_100);
	ASSERT_TRUE(has_color_red);
	
	doc.addClass(roots[0], "added");
	
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
	Span<u32> roots = doc.m_roots;
	ASSERT_EQ(1, doc.m_roots.size());
	ui::Element* root = doc.getElement(doc.m_roots[0]);
	
	int initial_attrs = root->attributes.size();
	
	doc.addClass(roots[0], "c");
	int after_add_c = root->attributes.size();
	ASSERT_TRUE(after_add_c > initial_attrs);
	
	doc.addClass(roots[0], "d");
	int after_add_d = root->attributes.size();
	ASSERT_TRUE(after_add_d > after_add_c);
	
	doc.removeClass(roots[0], "b");
	int after_remove_b = root->attributes.size();
	ASSERT_TRUE(after_remove_b < after_add_d);
	
	doc.removeClass(roots[0], "a");
	doc.removeClass(roots[0], "c");
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

struct MockMouseDevice : InputSystem::Device {
	MockMouseDevice() { type = InputSystem::Device::MOUSE; }
	void update(float dt) override {}
	const char* getName() const override { return "MockMouse"; }
};

static void injectMouseButton(MockDocument& doc, MockMouseDevice& mouse, bool down, float x, float y) {
	InputSystem::Event ev;
	ev.device = &mouse;
	ev.type = InputSystem::Event::BUTTON;
	ev.data.button.down = down;
	ev.data.button.key_id = 0;
	ev.data.button.x = x;
	ev.data.button.y = y;
	doc.injectEvent(ev);
}

bool testOnClickAttributeParseOnBox() {
	MockDocument doc;
	ASSERT_PARSE(doc, "[box on-click=foo]");
	ASSERT_EQ(1, doc.m_roots.size());
	ui::Element* root = doc.getElement(doc.m_roots[0]);
	ASSERT_EQ(1, root->attributes.size());
	ASSERT_ATTRIBUTE(root, 0, ON_CLICK);
	ASSERT_EQ("foo", root->attributes[0].value);
	return true;
}

bool testOnClickAttributeRejectedOnNonBox() {
	MockDocument doc;
	doc.m_suppress_logging = true;
	ASSERT_EQ(false, doc.parse("[span on-click=foo]", "test.ui"));
	ASSERT_EQ(false, doc.parse("[image on-click=foo]", "test.ui"));
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

bool testHoverEvents() {
	// TODO better test
	MockDocument doc;
	doc.m_canvas_size = Vec2(800, 600);
	ASSERT_PARSE(doc, "[box .p1 width=100 height=100] {} [box .p2 width=100 height=100] {}");
	doc.computeLayout(doc.m_canvas_size);

	MockMouseDevice mouse;
	InputSystem::Event ev;
	ev.device = &mouse;
	ev.type = InputSystem::Event::AXIS;
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

	Element* root = doc.getElement(doc.m_roots[0]);
	ASSERT_FLOAT_EQ(10.0f, root->size.x);
	ASSERT_FLOAT_EQ(10.0f, root->size.y);
	ASSERT_EQ(1, root->children.size());
	Element* span = doc.getElement(root->children[0]);
	ASSERT_FLOAT_EQ(20.0f, span->size.x);

	doc.setDPIScale(2.0f);

	ASSERT_FLOAT_EQ(20.0f, root->size.x);
	ASSERT_FLOAT_EQ(20.0f, root->size.y);
	ASSERT_FLOAT_EQ(40.0f, span->size.x);
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

	// TODO
	//ASSERT_EQ(false, doc.parse("[box bg-fit=stretch]", "test.ui"));
	//ASSERT_EQ(false, doc.parse("[image fit=stretch]", "test.ui"));

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
	RUN_TEST(testOnClickAttributeParseOnBox);
	RUN_TEST(testOnClickAttributeRejectedOnNonBox);
	RUN_TEST(testActionEventEmittedOnReleaseOverClickableBox);
	RUN_TEST(testActionEventNotEmittedWithoutOnClick);
	RUN_TEST(testActionEventNotEmittedForSpan);
	RUN_TEST(testActionEventNotEmittedOutside);
	RUN_TEST(testActionEventMultipleBoxesTargeting);
	RUN_TEST(testActionEventEmittedViaAncestorOnClick);
	RUN_TEST(testActionEventEmittedViaGrandparentOnClick);
	RUN_TEST(testHoverEvents);
	RUN_TEST(testDPIScaling);
	RUN_TEST(testInvalidAttributeValuesRejected);
}
