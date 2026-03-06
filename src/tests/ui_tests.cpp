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
	
	// Test same string returns same id
	u32 id1 = (u32)doc.m_intern_table.intern("class1");
	u32 id2 = (u32)doc.m_intern_table.intern("class1");
	ASSERT_EQ(id1, id2);
	
	// Test different strings return different ids
	u32 id3 = (u32)doc.m_intern_table.intern("class2");
	ASSERT_TRUE(id1 != id3);
	
	// Test resolve
	StringView resolved = doc.m_intern_table.resolve((InternString)id1);
	ASSERT_EQ(resolved, "class1");
	
	// Test invalid id
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
	ASSERT_PARSE(doc, "[box id=\"testid\" .testclass visible=false font-size=14 font=\"arial.ttf\" color=\"#ffffff\"]");
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
	ASSERT_EQ("class1", class1);
	ASSERT_EQ("class2", class2);
	ASSERT_EQ("class3", class3);
	
	return true;
}

bool testSingleClass() {
	MockDocument doc;
	ASSERT_PARSE(doc, "[box .singleclass]");
	ASSERT_EQ(1, doc.m_roots.size());
	ui::Element* root = doc.getElement(doc.m_roots[0]);
	ASSERT_EQ(1, root->classes.size());
	StringView class_name = doc.m_intern_table.resolve(root->classes[0]);
	ASSERT_EQ("singleclass", class_name);
	return true;
}

bool testDuplicateClasses() {
	MockDocument doc;
	ASSERT_PARSE(doc, "[box .dup .other .dup]");
	ASSERT_EQ(1, doc.m_roots.size());
	ui::Element* root = doc.getElement(doc.m_roots[0]);
	// Should dedup, first occurrence wins, order preserved
	ASSERT_EQ(2, root->classes.size());
	StringView class1 = doc.m_intern_table.resolve(root->classes[0]);
	StringView class2 = doc.m_intern_table.resolve(root->classes[1]);
	ASSERT_EQ("dup", class1);
	ASSERT_EQ("other", class2);
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
	ASSERT_PARSE(doc, "[box bg-image=\"bg.png\" bg-fit=cover bg-color=#000000 direction=column wrap=true justify-content=center]");
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
	ASSERT_PARSE(doc, "[box] {}");
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
	ASSERT_PARSE(doc1, "[box] { [span value=\"hello\"] }");
	ASSERT_EQ(1, doc1.m_roots.size());
	ui::Element* root1 = doc1.getElement(doc1.m_roots[0]);
	ASSERT_EQ(1, root1->children.size());
	ui::Element* child1 = doc1.getElement(root1->children[0]);
	ASSERT_TAG(child1, SPAN);
	Span<ui::Attribute> attrs1 = child1->attributes;
	ASSERT_EQ(0, attrs1.size());
	ASSERT_EQ("hello", child1->value);

	MockDocument doc2;
	ASSERT_PARSE(doc2, "[box] { hello }");
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
	ASSERT_PARSE(doc, "[box] { [span value=\"\"] }");
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
	ASSERT_PARSE(doc, "[box font=\"arial.ttf\"] { [span value=\"hello\" font=\"times.ttf\"] }");
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
	ASSERT_PARSE(doc, "[box font-size=16] { [span value=\"hello\" font-size=24] }");
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
	ASSERT_PARSE(doc, "[box font=\"arial.ttf\"] { hello }");
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
	ASSERT_PARSE(doc, "[box color=\"#ff0000\"] { \"hello\" }");
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
	ASSERT_PARSE(doc, "[box color=\"#00ff00\"] { [box] { \"hello\" } }");
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
	ASSERT_PARSE(doc, "[box] { ,-=()*&^@! }");
	ASSERT_EQ(1, doc.m_roots.size());
	ui::Element* root = doc.getElement(doc.m_roots[0]);
	ASSERT_TAG(root, BOX);
	ASSERT_EQ(1, root->children.size());
	ui::Element* child = doc.getElement(root->children[0]);
	ASSERT_TAG(child, SPAN);
	ASSERT_EQ(",-=()*&^@!", child->value);
	return true;
}

bool testSpaceBetweenSpans() {
	MockDocument doc;
	ASSERT_PARSE(doc, "[box direction=row font=\"arial.ttf\" font-size=16] { [span value=\"hello\"] [span value=\"world\"] }");
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
				color: red;
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
	
	// Initial: has width and color from .initial
	bool has_width_100 = false;
	bool has_color_red = false;
	for (const ui::Attribute& attr : root->attributes) {
		if (attr.type == ui::AttributeName::WIDTH && equalStrings(attr.value, "100%")) has_width_100 = true;
		if (attr.type == ui::AttributeName::COLOR && equalStrings(attr.value, "red")) has_color_red = true;
	}
	ASSERT_TRUE(has_width_100);
	ASSERT_TRUE(has_color_red);
	
	// Runtime: add .added class
	doc.addClass(roots[0], "added");
	
	// Now should have height too
	bool has_height_200 = false;
	for (const ui::Attribute& attr : root->attributes) {
		if (attr.type == ui::AttributeName::HEIGHT && equalStrings(attr.value, "200")) has_height_200 = true;
	}
	ASSERT_TRUE(has_height_200);
	// And still have the others
	has_width_100 = false;
	has_color_red = false;
	for (const ui::Attribute& attr : root->attributes) {
		if (attr.type == ui::AttributeName::WIDTH && equalStrings(attr.value, "100%")) has_width_100 = true;
		if (attr.type == ui::AttributeName::COLOR && equalStrings(attr.value, "red")) has_color_red = true;
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
	
	// Initial: .a and .b applied
	int initial_attrs = root->attributes.size();
	
	// Add .c
	doc.addClass(roots[0], "c");
	int after_add_c = root->attributes.size();
	ASSERT_TRUE(after_add_c > initial_attrs);
	
	// Add .d
	doc.addClass(roots[0], "d");
	int after_add_d = root->attributes.size();
	ASSERT_TRUE(after_add_d > after_add_c);
	
	// Remove .b
	doc.removeClass(roots[0], "b");
	int after_remove_b = root->attributes.size();
	ASSERT_TRUE(after_remove_b < after_add_d);
	
	// Remove .a and .c
	doc.removeClass(roots[0], "a");
	doc.removeClass(roots[0], "c");
	int final_attrs = root->attributes.size();
	
	// Should only have .d
	bool has_padding = false;
	int attr_count = 0;
	for (const ui::Attribute& attr : root->attributes) {
		attr_count++;
		if (attr.type == ui::AttributeName::PADDING && equalStrings(attr.value, "3")) has_padding = true;
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

bool testHoverEvents() {
	MockDocument doc;
	doc.m_canvas_size = Vec2(800, 600);
	// Create two panels: one at (0,0) with size (100,100), another below it at (0,100) with size (100,100)
	ASSERT_PARSE(doc, "[box .p1 width=100 height=100] {} [box .p2 width=100 height=100] {}");
	doc.computeLayout(doc.m_canvas_size);

	MockMouseDevice mouse;
	InputSystem::Event ev;
	ev.device = &mouse;
	ev.type = InputSystem::Event::AXIS;
	ev.data.axis.x_abs = 50;
	ev.data.axis.y_abs = 50;

	// Initial move to (50,50) - should enter .p1 (index 0)
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

	// Move to (50, 150) - should leave .p1 (index 0) and enter .p2 (index 1)
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

	// Move to (300, 300) - should leave .p2 (index 1) and enter nothing
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

bool testDemoUI() {
	MockDocument doc;
	const char* ui_content = R"(
	[style] {
		.button {
			width: 120;
			bg-color: #ffffFF;
			align: center;
			padding: 0.5em;
			margin: 1em;
		}

		.hovered {
			bg-color: #ff0000;
		}
	}
	[box direction=row padding=20 width=100% font-size=20 bg-color=#00ff00] {
		[box .button] { Button 1 }
		[box .button] { Button 2 }
	}
	)";
	ASSERT_PARSE(doc, ui_content);
	
	Span<u32> roots = doc.m_roots;
	ASSERT_EQ(1, roots.size());
	
	Span<ui::StyleRule> rules = doc.m_stylesheet.getRules();
	ASSERT_EQ(2, rules.size());
	
	ui::Element* root = doc.getElement(roots[0]);
	ASSERT_TAG(root, BOX);
	ASSERT_EQ(2, root->children.size());
	
	doc.computeLayout(Vec2(800, 600));
	
	ASSERT_FLOAT_EQ(800.0f, root->size.x);
	ASSERT_TRUE(root->size.y > 0.0f);
	
	ui::Element* button1 = doc.getElement(root->children[0]);
	ui::Element* button2 = doc.getElement(root->children[1]);
	
	ASSERT_FLOAT_EQ(40.0f, button1->position.x);
	ASSERT_FLOAT_EQ(180.0f, button2->position.x);
	
	ASSERT_FLOAT_EQ(button1->position.y, button2->position.y);
	
	float expected_width = 120.0f;
	ASSERT_FLOAT_EQ(expected_width, button1->size.x);
	ASSERT_FLOAT_EQ(expected_width, button2->size.x);
	
	doc.addClass(root->children[0], "hovered");
	
	bool has_hovered_bg = false;
	for (const ui::Attribute& attr : button1->attributes) {
		if (attr.type == ui::AttributeName::BG_COLOR && equalStrings(attr.value, "#ff0000")) {
			has_hovered_bg = true;
			break;
		}
	}
	ASSERT_TRUE(has_hovered_bg);
	
	// reassert, layout should not change
	ASSERT_FLOAT_EQ(800.0f, root->size.x);
	ASSERT_TRUE(root->size.y > 0.0f);
	
	ASSERT_FLOAT_EQ(40.0f, button1->position.x);
	ASSERT_FLOAT_EQ(180.0f, button2->position.x);
	
	ASSERT_FLOAT_EQ(button1->position.y, button2->position.y);
	
	ASSERT_FLOAT_EQ(expected_width, button1->size.x);
	ASSERT_FLOAT_EQ(expected_width, button2->size.x);

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
	RUN_TEST(testAlignInheritance);
	RUN_TEST(testMultilineStringMeasurement);
	RUN_TEST(testMultilineStringLayout);
	RUN_TEST(testTextWithSpecialChars);
	RUN_TEST(testSpaceBetweenSpans);
	RUN_TEST(testParseAndRuntimeMutation);
	RUN_TEST(testComplexMutationSequence);
	RUN_TEST(testHoverEvents);
	RUN_TEST(testDemoUI);
}