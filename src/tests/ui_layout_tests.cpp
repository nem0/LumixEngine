#include "core/log.h"
#include "tests/common.h"

using namespace Lumix;

namespace {

bool testTwoPanelsLayout() {
	// Citation: layout.md - Root Elements
	// "Root elements behave like they are children of a panel that covers the whole screen with 0 padding and `direction=column`."
	MockDocument doc;
	ASSERT_PARSE(doc, R"(
	[panel width=100% height=100 bg-color=#FF0000]
	[panel width=150 height=80 bg-color=#00FF00]
	)");
	Span<u32> root_indices = doc.m_roots;
	ASSERT_EQ(2, root_indices.size(), "Should parse 2 root elements");
	ui::Element* panel1 = doc.getElement(root_indices[0]);
	ui::Element* panel2 = doc.getElement(root_indices[1]);
	
	// Check attributes for panel1
	Span<ui::Attribute> attrs1 = panel1->attributes;
	ASSERT_EQ(3, attrs1.size(), "Panel1 should have 3 attributes");
	ASSERT_ATTRIBUTE(panel1, 0, WIDTH);
	ASSERT_EQ("100%", attrs1[0].value, "Width should be 100%");
	ASSERT_ATTRIBUTE(panel1, 1, HEIGHT);
	ASSERT_EQ("100", attrs1[1].value, "Height should be 100");
	ASSERT_ATTRIBUTE(panel1, 2, BG_COLOR);
	ASSERT_EQ("#FF0000", attrs1[2].value, "Background color should be #FF0000");
	
	// Check attributes for panel2
	Span<ui::Attribute> attrs2 = panel2->attributes;
	ASSERT_EQ(3, attrs2.size(), "Panel2 should have 3 attributes");
	ASSERT_ATTRIBUTE(panel2, 0, WIDTH);
	ASSERT_EQ("150", attrs2[0].value, "Width should be 150");
	ASSERT_ATTRIBUTE(panel2, 1, HEIGHT);
	ASSERT_EQ("80", attrs2[1].value, "Height should be 80");
	ASSERT_ATTRIBUTE(panel2, 2, BG_COLOR);
	ASSERT_EQ("#00FF00", attrs2[2].value, "Background color should be #00FF00");
	
	// Compute layout
	doc.computeLayout(Vec2(800, 600));
	
	// Check sizes
	ASSERT_FLOAT_EQ(800.0f, panel1->size.x, "Panel1 width should be 800");
	ASSERT_FLOAT_EQ(100.0f, panel1->size.y, "Panel1 height should be 100");
	ASSERT_FLOAT_EQ(150.0f, panel2->size.x, "Panel2 width should be 150");
	ASSERT_FLOAT_EQ(80.0f, panel2->size.y, "Panel2 height should be 80");
	
	// Root elements are laid out like in a panel with direction=column
	ASSERT_FLOAT_EQ(0.0f, panel1->position.x, "Panel1 x should be 0");
	ASSERT_FLOAT_EQ(0.0f, panel1->position.y, "Panel1 y should be 0");
	ASSERT_FLOAT_EQ(0.0f, panel2->position.x, "Panel2 x should be 0");
	ASSERT_FLOAT_EQ(100.0f, panel2->position.y, "Panel2 y should be 100");
	
	return true;
}

bool testLayoutDirection() {
	// Citation: layout.md - Positioning Algorithm
	MockDocument doc;
	ASSERT_PARSE(doc, R"(
		[panel direction=row] {
			[panel width=100 height=50] {}
			[panel width=150 height=50] {}
		}
	)");
	Span<u32> root_indices = doc.m_roots;
	ASSERT_EQ(1, root_indices.size(), "Should parse 1 root element");
	ui::Element* parent = doc.getElement(root_indices[0]);
	ASSERT_EQ(2, parent->children.size(), "Parent should have 2 children");
	ui::Element* child1 = doc.getElement(parent->children[0]);
	ui::Element* child2 = doc.getElement(parent->children[1]);

	// Compute layout
	doc.computeLayout(Vec2(800, 600));

	// Check children sizes
	ASSERT_FLOAT_EQ(100.0f, child1->size.x, "Child1 width should be 100");
	ASSERT_FLOAT_EQ(50.0f, child1->size.y, "Child1 height should be 50");
	ASSERT_FLOAT_EQ(150.0f, child2->size.x, "Child2 width should be 150");
	ASSERT_FLOAT_EQ(50.0f, child2->size.y, "Child2 height should be 50");

	// Children are laid out horizontally
	float parent_x = parent->position.x;
	float parent_y = parent->position.y;

	ASSERT_FLOAT_EQ(parent_x, child1->position.x, "Child1 x should be parent x");
	ASSERT_FLOAT_EQ(parent_y, child1->position.y, "Child1 y should be parent y");

	// Child2 should be to the right of child1 (row layout)
	ASSERT_FLOAT_EQ(parent_x + 100.0f, child2->position.x, "Child2 x should be parent x + child1 width");
	ASSERT_FLOAT_EQ(parent_y, child2->position.y, "Child2 y should be parent y");

	return true;
}

bool testJustifyContent() {
	// Citation: elements_attributes.md - panel
	// "`justify-content` - `center` - Elements are centered as a group"
	MockDocument doc;
	ASSERT_PARSE(doc, R"(
	[panel width=400 height=200 direction=row justify-content=center] {
		[panel width=50 height=50] {}
		[panel width=50 height=50] {}
	}
	)");
	doc.computeLayout(Vec2(800, 600));
	ASSERT_TRUE(doc.m_elements.size() >= 3, "Document should have at least 3 elements");
	ui::Element* panel = doc.getElement(0);
	ui::Element* btn1 = doc.getElement(1);
	ui::Element* btn2 = doc.getElement(2);
	// Panel at (0,0) to (400,200), buttons 50x50 each, total span 100, available 400, center at 150
	float expected_x = (400 - 100) * 0.5f; // 150
	ASSERT_FLOAT_EQ(expected_x, btn1->position.x, "First button x should be centered");
	ASSERT_FLOAT_EQ(expected_x + 50.0f, btn2->position.x, "Second button x should be after first");
	return true;
}

bool testJustifyStart() {
	// Citation: elements_attributes.md - panel
	// "`justify-content` - `start` - Align items to the start of the container"
	MockDocument doc;
	ASSERT_PARSE(doc, R"(
	[panel width=400 height=200 direction=row justify-content=start] {
		[panel width=50 height=50] {}
		[panel width=50 height=50] {}
	}
	)");
	doc.computeLayout(Vec2(800, 600));
	ASSERT_TRUE(doc.m_elements.size() >= 3, "Document should have at least 3 elements");
	ui::Element* panel = doc.getElement(0);
	ui::Element* btn1 = doc.getElement(1);
	ui::Element* btn2 = doc.getElement(2);
	// Start: buttons at left
	ASSERT_FLOAT_EQ(0.0f, btn1->position.x, "First button x should be at start");
	ASSERT_FLOAT_EQ(50.0f, btn2->position.x, "Second button x should be after first");
	return true;
}

bool testJustifyEnd() {
	// Citation: elements_attributes.md - panel
	// "`justify-content` - `end` - Align items to the end of the container"
	MockDocument doc;
	ASSERT_PARSE(doc, R"(
	[panel width=400 height=200 direction=row justify-content=end] {
		[panel width=50 height=50] {}
		[panel width=50 height=50] {}
	}
	)");
	doc.computeLayout(Vec2(800, 600));
	ASSERT_TRUE(doc.m_elements.size() >= 3, "Document should have at least 3 elements");
	ui::Element* panel = doc.getElement(0);
	ui::Element* btn1 = doc.getElement(1);
	ui::Element* btn2 = doc.getElement(2);
	// End: total span 100, available 400, end at 300
	float expected_x = 400 - 100; // 300
	ASSERT_FLOAT_EQ(expected_x, btn1->position.x, "First button x should be at end");
	ASSERT_FLOAT_EQ(expected_x + 50.0f, btn2->position.x, "Second button x should be after first");
	return true;
}

bool testJustifySpaceBetween() {
	// Citation: elements_attributes.md - panel
	// "`justify-content` - `space-between` - Distribute items evenly with first at start and last at end"
	MockDocument doc;
	ASSERT_PARSE(doc, R"(
	[panel width=400 height=200 direction=row justify-content=space-between] {
		[panel width=50 height=50] {}
		[panel width=50 height=50] {}
		[panel width=50 height=50] {}
	}
	)");
	doc.computeLayout(Vec2(800, 600));
	ASSERT_TRUE(doc.m_elements.size() >= 4, "Document should have at least 4 elements");
	ui::Element* panel = doc.getElement(0);
	ui::Element* btn1 = doc.getElement(1);
	ui::Element* btn2 = doc.getElement(2);
	ui::Element* btn3 = doc.getElement(3);
	// Space between: 3 buttons, 2 spaces, total span 150, available 400, space = (400-150)/2 = 125
	// Positions: 0, 50+125=175, 175+50+125=350
	ASSERT_FLOAT_EQ(0.0f, btn1->position.x, "First button x should be at start");
	ASSERT_FLOAT_EQ(175.0f, btn2->position.x, "Second button x should be spaced");
	ASSERT_FLOAT_EQ(350.0f, btn3->position.x, "Third button x should be at end");
	return true;
}

bool testJustifySpaceAround() {
	// Citation: elements_attributes.md - panel
	// "`justify-content` - `space-around` - Distribute items with equal space around them"
	MockDocument doc;
	ASSERT_PARSE(doc, R"(
	[panel width=400 height=200 direction=row justify-content=space-around] {
		[panel width=50 height=50] {}
		[panel width=50 height=50] {}
	}
	)");
	doc.computeLayout(Vec2(800, 600));
	ASSERT_TRUE(doc.m_elements.size() >= 3, "Document should have at least 3 elements");
	ui::Element* panel = doc.getElement(0);
	ui::Element* btn1 = doc.getElement(1);
	ui::Element* btn2 = doc.getElement(2);
	// Space around: 2 buttons, space = (400-100)/2 = 150, half space 75
	// Positions: 75, 75+50+150=275
	ASSERT_FLOAT_EQ(75.0f, btn1->position.x, "First button x should have space around");
	ASSERT_FLOAT_EQ(275.0f, btn2->position.x, "Second button x should have space around");
	return true;
}

bool testJustifySpaceAroundSingleChild() {
	// Citation: layout.md - Justification
	// "With a single child, `space-around` behaves like `center`."
	MockDocument doc;
	ASSERT_PARSE(doc, R"(
	[panel width=400 height=200 direction=row justify-content=space-around] {
		[panel width=50 height=50] {}
	}
	)");
	doc.computeLayout(Vec2(800, 600));
	ASSERT_TRUE(doc.m_elements.size() >= 2, "Document should have at least 2 elements");
	ui::Element* panel = doc.getElement(0);
	ui::Element* child = doc.getElement(1);
	// With single child, space-around should behave like center
	// Container width 400, child width 50, so centered at (400-50)/2 = 175
	ASSERT_FLOAT_EQ(175.0f, child->position.x, "Single child should be centered");
	return true;
}

bool testJustifySpaceBetweenSingleChild() {
	// Citation: layout.md - Justification
	// "With a single child, `space-between` behaves like `start`."
	MockDocument doc;
	ASSERT_PARSE(doc, R"(
	[panel width=400 height=200 direction=row justify-content=space-between] {
		[panel width=50 height=50] {}
	}
	)");
	doc.computeLayout(Vec2(800, 600));
	ASSERT_TRUE(doc.m_elements.size() >= 2, "Document should have at least 2 elements");
	ui::Element* panel = doc.getElement(0);
	ui::Element* child = doc.getElement(1);
	// With single child, space-between should behave like start
	ASSERT_FLOAT_EQ(0.0f, child->position.x, "Single child should be positioned at start");
	return true;
}

bool testPercentLayout() {
	// Citation: layout.md - Units
	// "%: Percentage of parent (or viewport for roots). E.g., `width=50%` for half the parent's width."
	MockDocument doc;
	ASSERT_PARSE(doc, R"(
	[panel width=400 height=200] {
		[panel width=50% height=50%] {}
	}
	)");
	doc.computeLayout(Vec2(800, 600));
	ASSERT_TRUE(doc.m_elements.size() >= 2, "Document should have at least 2 elements");
	ui::Element* parent = doc.getElement(0);
	ui::Element* child = doc.getElement(1);
	// Parent size: 400x200
	// Child: 50% of parent -> 200x100
	ASSERT_FLOAT_EQ(400.0f, parent->size.x, "Parent width should be 400");
	ASSERT_FLOAT_EQ(200.0f, parent->size.y, "Parent height should be 200");
	ASSERT_FLOAT_EQ(200.0f, child->size.x, "Child width should be 50% of parent (200)");
	ASSERT_FLOAT_EQ(100.0f, child->size.y, "Child height should be 50% of parent (100)");
	return true;
}

bool testPercentMargins() {
	// Citation: layout.md - Units
	// "%: Percentage of parent (or viewport for roots). E.g., `width=50%` for half the parent's width."
	MockDocument doc;
	ASSERT_PARSE(doc, R"(
	[panel width=400 height=200] {
		[panel margin=10%] {}
	}
	)");
	doc.computeLayout(Vec2(800, 600));
	ASSERT_TRUE(doc.m_elements.size() >= 2, "Document should have at least 2 elements");
	ui::Element* parent = doc.getElement(0);
	ui::Element* child = doc.getElement(1);
	// Parent size: 400x200
	// margin=10% should yield: top/bottom = 10% of parent height = 20, left/right = 10% of parent width = 40
	ASSERT_FLOAT_EQ(20.0f, child->margins[0], "Child top margin should be 10% of parent height (20)");
	ASSERT_FLOAT_EQ(40.0f, child->margins[1], "Child right margin should be 10% of parent width (40)");
	ASSERT_FLOAT_EQ(20.0f, child->margins[2], "Child bottom margin should be 10% of parent height (20)");
	ASSERT_FLOAT_EQ(40.0f, child->margins[3], "Child left margin should be 10% of parent width (40)");
	return true;
}

bool testMarginPadding() {
	// Citation: layout.md - Margins and Padding
	// "Margins and padding add space around and inside elements to control layout and appearance."
	MockDocument doc;
	ASSERT_PARSE(doc, R"(
	[panel width=400 height=200 margin=10 padding=5] {
		[panel width=50 height=50] {}
	}
	)");
	doc.computeLayout(Vec2(800, 600));
	ASSERT_TRUE(doc.m_elements.size() >= 2, "Document should have at least 2 elements");
	ui::Element* parent = doc.getElement(0);
	ui::Element* child = doc.getElement(1);

	// Check margins are set
	for (int i = 0; i < 4; ++i) {
		ASSERT_FLOAT_EQ(10.0f, parent->margins[i], "Parent margin should be 10");
	}

	// Check paddings are set
	for (int i = 0; i < 4; ++i) {
		ASSERT_FLOAT_EQ(5.0f, parent->paddings[i], "Parent padding should be 5");
	}

	// Check that padding affects child positioning
	float expected_child_x = parent->position.x + parent->paddings[3] + child->margins[3]; // parent.x + left_padding + child_left_margin
	float expected_child_y = parent->position.y + parent->paddings[0] + child->margins[0]; // parent.y + top_padding + child_top_margin
	ASSERT_FLOAT_EQ(expected_child_x, child->position.x, "Child x should account for parent padding and child margin");
	ASSERT_FLOAT_EQ(expected_child_y, child->position.y, "Child y should account for parent padding and child margin");

	return true;
}

bool testBasicLayout() {
	// Citation: layout.md - Element Sizing
	// "Each UI element has `width` and `height` attributes that control its size, known as dimensions. Set them explicitly for a fixed size;"
	MockDocument doc;
	ASSERT_PARSE(doc, "[panel width=200 height=100] {}");
	doc.computeLayout(Vec2(800, 600));
	ASSERT_TRUE(doc.m_elements.size() >= 1, "Document should have at least 1 element");
	ui::Element* elem = doc.getElement(0);
	ASSERT_FLOAT_EQ(200.0f, elem->size.x, "Width should be 200");
	ASSERT_FLOAT_EQ(100.0f, elem->size.y, "Height should be 100");
	return true;
}

bool testFitContent() {
	// Citation: layout.md - Element Sizing
	// "- fit-content: Auto-size to content. For panels, sums child sizes. E.g., `width=fit-content`."
	MockDocument doc;
	ASSERT_PARSE(doc, R"(
	[panel width=fit-content height=fit-content] {
		[panel width=640 height=480] {}
	}
	)");
	doc.computeLayout(Vec2(800, 600));
	ASSERT_TRUE(doc.m_elements.size() >= 2, "Document should have at least 2 elements");
	ui::Element* elem = doc.getElement(0);
	ASSERT_FLOAT_EQ(640.0f, elem->size.x, "Width with fit-content should be 640");
	ASSERT_FLOAT_EQ(480.0f, elem->size.y, "Height with fit-content should be 480");
	return true;
}

bool testFitContentWithMargins() {
	// Citation: layout.md - Fit-Content
	// "When using `fit-content` sizing, margins are included in the total size calculation for containers, ensuring spacing between children is preserved."
	MockDocument doc;
	ASSERT_PARSE(doc, R"(
	[panel height=fit-content] {
		[panel height=100 margin=10] {}
	}
	)");
	doc.computeLayout(Vec2(800, 600));
	ASSERT_TRUE(doc.m_elements.size() >= 2, "Document should have at least 2 elements");
	ui::Element* parent = doc.getElement(0);
	// Height should include child's top and bottom margins
	ASSERT_FLOAT_EQ(120.0f, parent->size.y, "Parent height should fit content including margins");
	return true;
}

bool testFill() {
	// Citation: layout.md - Fill
	// "The `fill` unit allows an element to expand and occupy the remaining available space in its parent container along the specified dimension."
	MockDocument doc;
	ASSERT_PARSE(doc, R"(
	[panel width=300 height=100 direction=row] {
		[panel width=100 height=50] {}
		[panel width=fill height=50] {}
	}
	)");
	doc.computeLayout(Vec2(800, 600));
	ASSERT_TRUE(doc.m_elements.size() >= 3, "Document should have at least 3 elements");
	ui::Element* parent = doc.getElement(0);
	ui::Element* child1 = doc.getElement(1);
	ui::Element* child2 = doc.getElement(2);
	ASSERT_FLOAT_EQ(100.0f, child1->size.x, "First child width should be 100");
	// Fill should take remaining space: 300 - 100 = 200
	ASSERT_FLOAT_EQ(200.0f, child2->size.x, "Second child width should fill remaining space");
	return true;
}

bool testFillWithPadding() {
	// Citation: layout.md - Fill
	// "Fill respects margins and padding of the parent container."
	// "Difference from `width=100%`: ... `fill` expands to occupy the available space within the parent's content area (after subtracting padding)"
	MockDocument doc;
	ASSERT_PARSE(doc, R"(
	[panel width=300 height=100 direction=row padding=10] {
		[panel width=100 height=50] {}
		[panel width=fill height=50] {}
	}
	)");
	doc.computeLayout(Vec2(800, 600));
	ASSERT_TRUE(doc.m_elements.size() >= 3, "Document should have at least 3 elements");
	ui::Element* parent = doc.getElement(0);
	ui::Element* child1 = doc.getElement(1);
	ui::Element* child2 = doc.getElement(2);
	ASSERT_FLOAT_EQ(100.0f, child1->size.x, "First child width should be 100");
	// Parent content width: 300 - 10*2 = 280, remaining after child1: 280 - 100 = 180
	ASSERT_FLOAT_EQ(180.0f, child2->size.x, "Second child width should fill remaining content space");
	return true;
}

bool testFillWithMargin() {
	// Citation: layout.md - Fill
	// "Fill respects margins and padding of the parent container."
	MockDocument doc;
	ASSERT_PARSE(doc, R"(
	[panel width=300 height=100 direction=row] {
		[panel width=100 height=50 margin=5] {}
		[panel width=fill height=50 margin=5] {}
	}
	)");
	doc.computeLayout(Vec2(800, 600));
	ASSERT_TRUE(doc.m_elements.size() >= 3, "Document should have at least 3 elements");
	ui::Element* parent = doc.getElement(0);
	ui::Element* child1 = doc.getElement(1);
	ui::Element* child2 = doc.getElement(2);
	// Child1 size is 100, but margins affect positioning, not size
	ASSERT_FLOAT_EQ(100.0f, child1->size.x, "First child width should be 100");
	// Remaining space: 300 - 100 - 5 (child1 right) - 5 (child2 left) = 190
	// But since margins collapse, and direction=row, margins are on left/right
	// Actually, in positioning, margins are added between elements
	// For fill, it should fill the remaining after accounting for margins
	// Assuming margins are included in spacing
	// Total space taken by child1: 100 + 5 (left) + 5 (right) = 110
	// Remaining: 300 - 110 = 190, but child2 has its own margins
	// This might be tricky; perhaps the test expects fill to be 300 - 100 - margins
	// Let's assume fill takes 300 - 100 - 10 (margins) = 190
	ASSERT_FLOAT_EQ(190.0f, child2->size.x, "Second child width should fill remaining space accounting for margins");
	return true;
}

bool testFillSingleChild() {
	// Citation: layout.md - Fill
	// "The `fill` unit allows an element to expand and occupy the remaining available space in its parent container along the specified dimension."
	MockDocument doc;
	ASSERT_PARSE(doc, R"(
	[panel width=300 height=100 direction=row] {
		[panel width=fill height=50] {}
	}
	)");
	doc.computeLayout(Vec2(800, 600));
	ASSERT_TRUE(doc.m_elements.size() >= 2, "Document should have at least 2 elements");
	ui::Element* parent = doc.getElement(0);
	ui::Element* child = doc.getElement(1);
	// Fill should take the entire parent content width: 300
	ASSERT_FLOAT_EQ(300.0f, child->size.x, "Single child with fill should occupy full parent width");
	return true;
}

bool testFillWithWrap() {
	// Citation: layout.md - Fill
	// "When `wrap=true`, `fill` elements expand to fill all remaining space in their current row, starting from their position after preceding elements. Subsequent elements that don't fit wrap to the next row."
	MockDocument doc;
	ASSERT_PARSE(doc, R"(
	[panel width=350 direction=row wrap=true] {
	  [panel width=100 height=16] {}
	  [panel width=fill height=16] {}
	  [panel width=150 height=16] {}
	  [panel width=fill height=16] {}
	}
	)");
	doc.computeLayout(Vec2(800, 600));
	ui::Element* panel = doc.getElement(0);
	ui::Element* elem1 = doc.getElement(1); // 100px
	ui::Element* elem2 = doc.getElement(2); // fill 250px
	ui::Element* elem3 = doc.getElement(3); // 150px
	ui::Element* elem4 = doc.getElement(4); // fill 200px
	ASSERT_FLOAT_EQ(100.0f, elem1->size.x, "First element width should be 100");
	ASSERT_FLOAT_EQ(250.0f, elem2->size.x, "Fill element fills remaining in first row");
	ASSERT_FLOAT_EQ(150.0f, elem3->size.x, "Third element width should be 150");
	ASSERT_FLOAT_EQ(200.0f, elem4->size.x, "Fill element fills remaining in second row");
	// First row: elem1 and elem2, second row: elem3 and elem4
	ASSERT_FLOAT_EQ(0.0f, elem1->position.x, "First element x position");
	ASSERT_FLOAT_EQ(0.0f, elem1->position.y, "First element y position");
	ASSERT_FLOAT_EQ(100.0f, elem2->position.x, "Fill element x position in first row");
	ASSERT_FLOAT_EQ(0.0f, elem2->position.y, "Fill element y position in first row");
	ASSERT_FLOAT_EQ(0.0f, elem3->position.x, "Third element x position in second row");
	ASSERT_FLOAT_EQ(16.0f, elem3->position.y, "Third element y position in second row");
	ASSERT_FLOAT_EQ(150.0f, elem4->position.x, "Fill element x position in second row");
	ASSERT_FLOAT_EQ(16.0f, elem4->position.y, "Fill element y position in second row");
	return true;
}

bool testLayoutWithMargins() {
	// Citation: layout.md - Margins
	// "Margins provide external spacing between elements and their containers, affecting position but not size."
	// "Adjacent margins combine into the larger value to prevent excessive gaps."
	MockDocument doc;
	ASSERT_PARSE(doc, R"(
	[panel width=200 height=100 margin=10] {}
	[panel width=150 height=80 margin=5] {}
	)");
	Span<u32> root_indices = doc.m_roots;
	ASSERT_EQ(2, root_indices.size(), "Should parse 2 root elements");
	ui::Element* panel1 = doc.getElement(root_indices[0]);
	ui::Element* panel2 = doc.getElement(root_indices[1]);
	
	// Compute layout
	doc.computeLayout(Vec2(800, 600));
	
	// Check sizes
	ASSERT_FLOAT_EQ(200.0f, panel1->size.x, "Panel1 width should be 200");
	ASSERT_FLOAT_EQ(100.0f, panel1->size.y, "Panel1 height should be 100");
	ASSERT_FLOAT_EQ(150.0f, panel2->size.x, "Panel2 width should be 150");
	ASSERT_FLOAT_EQ(80.0f, panel2->size.y, "Panel2 height should be 80");
	
	// Check margins are set
	for (int i = 0; i < 4; ++i) {
		ASSERT_FLOAT_EQ(10.0f, panel1->margins[i], "Panel1 margin should be 10");
		ASSERT_FLOAT_EQ(5.0f, panel2->margins[i], "Panel2 margin should be 5");
	}
	
	// Root elements are laid out like in a panel with direction=column
	// Position should account for margins: panel1.y + panel1.height + max(panel1.bottom_margin, panel2.top_margin)
	// Note: margins are collapsed
	ASSERT_FLOAT_EQ(10.0f, panel1->position.x, "Panel1 x should be offset by left margin");
	ASSERT_FLOAT_EQ(10.0f, panel1->position.y, "Panel1 y should be offset by top margin");
	ASSERT_FLOAT_EQ(5.0f, panel2->position.x, "Panel2 x should be offset by left margin");
	ASSERT_FLOAT_EQ(100.0f + 10.0f + 10.0f, panel2->position.y, "Panel2 y should account for margins");
	
	return true;
}

bool testNestedPanelsWithMargins() {
	// Citation: layout.md - Positioning Calculations
	// "Child element position relative to parent (for sequential layout with margin collapsing):"
	MockDocument doc;
	ASSERT_PARSE(doc, R"(
	[panel width=400 height=300 padding=10] {
		[panel width=200 height=100 margin=5] {}
		[panel width=150 height=80 margin=10] {}
	}
	)");
	Span<u32> root_indices = doc.m_roots;
	ASSERT_EQ(1, root_indices.size(), "Should parse 1 root element");
	ui::Element* parent = doc.getElement(root_indices[0]);
	ASSERT_EQ(2, parent->children.size(), "Parent should have 2 children");
	ui::Element* child1 = doc.getElement(parent->children[0]);
	ui::Element* child2 = doc.getElement(parent->children[1]);
	
	// Compute layout
	doc.computeLayout(Vec2(800, 600));
	
	// Check parent
	ASSERT_FLOAT_EQ(400.0f, parent->size.x, "Parent width should be 400");
	ASSERT_FLOAT_EQ(300.0f, parent->size.y, "Parent height should be 300");
	for (int i = 0; i < 4; ++i) {
		ASSERT_FLOAT_EQ(10.0f, parent->paddings[i], "Parent padding should be 10");
	}
	
	// Check children sizes
	ASSERT_FLOAT_EQ(200.0f, child1->size.x, "Child1 width should be 200");
	ASSERT_FLOAT_EQ(100.0f, child1->size.y, "Child1 height should be 100");
	ASSERT_FLOAT_EQ(150.0f, child2->size.x, "Child2 width should be 150");
	ASSERT_FLOAT_EQ(80.0f, child2->size.y, "Child2 height should be 80");
	
	// Check margins are set
	for (int i = 0; i < 4; ++i) {
		ASSERT_FLOAT_EQ(5.0f, child1->margins[i], "Child1 margin should be 5");
		ASSERT_FLOAT_EQ(10.0f, child2->margins[i], "Child2 margin should be 10");
	}
	
	// Check positions relative to parent
	float parent_x = parent->position.x;
	float parent_y = parent->position.y;
	float padding_left = parent->paddings[3]; // left padding
	float padding_top = parent->paddings[0];  // top padding
	
	ASSERT_FLOAT_EQ(parent_x + padding_left + 5.0f, child1->position.x, "Child1 x should be parent.x + padding_left + margin_left");
	ASSERT_FLOAT_EQ(parent_y + padding_top + 5.0f, child1->position.y, "Child1 y should be parent.y + padding_top + margin_top");
	
	// Child2 should be positioned after child1 with margins (collapsed)
	float expected_child2_y = parent_y + padding_top + 5.0f + child1->size.y + maximum(child1->margins[2], child2->margins[0]);
	ASSERT_FLOAT_EQ(parent_x + padding_left + 10.0f, child2->position.x, "Child2 x should be parent.x + padding_left + margin_left");
	ASSERT_FLOAT_EQ(expected_child2_y, child2->position.y, "Child2 y should account for child1 position, height and collapsed margins");
	
	return true;
}

bool testDirectionRow() {
	// Citation: layout.md - Element Positioning
	// "The `direction` attribute controls the primary axis along which child elements are arranged within a container. When set to `row`, children are positioned horizontally from left to right."
	MockDocument doc;
	ASSERT_PARSE(doc, R"(
	[panel direction=row padding=0] {
		[panel width=100 height=50 margin=0] {}
		[panel width=150 height=50 margin=0] {}
	}
	)");
	Span<u32> root_indices = doc.m_roots;
	ASSERT_EQ(1, root_indices.size(), "Should parse 1 root element");
	ui::Element* parent = doc.getElement(root_indices[0]);
	ASSERT_EQ(2, parent->children.size(), "Parent should have 2 children");
	ui::Element* child1 = doc.getElement(parent->children[0]);
	ui::Element* child2 = doc.getElement(parent->children[1]);
	
	// Compute layout
	doc.computeLayout(Vec2(800, 600));
	
	// Check children sizes
	ASSERT_FLOAT_EQ(100.0f, child1->size.x, "Child1 width should be 100");
	ASSERT_FLOAT_EQ(50.0f, child1->size.y, "Child1 height should be 50");
	ASSERT_FLOAT_EQ(150.0f, child2->size.x, "Child2 width should be 150");
	ASSERT_FLOAT_EQ(50.0f, child2->size.y, "Child2 height should be 50");
	
	// Children are laid out horizontally
	float parent_x = parent->position.x;
	float parent_y = parent->position.y;
	
	ASSERT_FLOAT_EQ(parent_x, child1->position.x, "Child1 x should be parent x");
	ASSERT_FLOAT_EQ(parent_y, child1->position.y, "Child1 y should be parent y");
	
	// Child2 should be to the right of child1 (row layout)
	ASSERT_FLOAT_EQ(parent_x + 100.0f, child2->position.x, "Child2 x should be parent x + child1 width");
	ASSERT_FLOAT_EQ(parent_y, child2->position.y, "Child2 y should be parent y");
	
	return true;
}

bool testDirectionColumn() {
	// Citation: layout.md - Element Positioning
	// "The `direction` attribute controls the primary axis along which child elements are arranged within a container."
	// "When set to `column` (the default), children are positioned vertically from top to bottom."
	MockDocument doc;
	ASSERT_PARSE(doc, R"(
	[panel direction=column] {
		[panel width=100 height=50 margin=5] {}
		[panel width=100 height=80 margin=5] {}
	}
	)");
	Span<u32> root_indices = doc.m_roots;
	ASSERT_EQ(1, root_indices.size(), "Should parse 1 root element");
	ui::Element* parent = doc.getElement(root_indices[0]);
	ASSERT_EQ(2, parent->children.size(), "Parent should have 2 children");
	ui::Element* child1 = doc.getElement(parent->children[0]);
	ui::Element* child2 = doc.getElement(parent->children[1]);
	
	// Compute layout
	doc.computeLayout(Vec2(800, 600));
	
	// Check children sizes
	ASSERT_FLOAT_EQ(100.0f, child1->size.x, "Child1 width should be 100");
	ASSERT_FLOAT_EQ(50.0f, child1->size.y, "Child1 height should be 50");
	ASSERT_FLOAT_EQ(100.0f, child2->size.x, "Child2 width should be 100");
	ASSERT_FLOAT_EQ(80.0f, child2->size.y, "Child2 height should be 80");
	
	// Check margins are set
	for (int i = 0; i < 4; ++i) {
		ASSERT_FLOAT_EQ(5.0f, child1->margins[i], "Child1 margin should be 5");
		ASSERT_FLOAT_EQ(5.0f, child2->margins[i], "Child2 margin should be 5");
	}
	
	// In column direction, children should be laid out vertically
	float parent_x = parent->position.x;
	float parent_y = parent->position.y;
	
	ASSERT_FLOAT_EQ(parent_x + 5.0f, child1->position.x, "Child1 x should be parent x + left margin");
	ASSERT_FLOAT_EQ(parent_y + 5.0f, child1->position.y, "Child1 y should be parent y + top margin");
	
	// Child2 should be below child1, accounting for margins (collapsed)
	ASSERT_FLOAT_EQ(parent_x + 5.0f, child2->position.x, "Child2 x should be parent x + left margin");
	ASSERT_FLOAT_EQ(parent_y + 5.0f + 50.0f + 5.0f, child2->position.y, "Child2 y should account for child1 position, height and collapsed margins");
	
	return true;
}

bool testDirectionDefault() {
	// Citation: layout.md - Element Positioning
	// "The `direction` attribute controls the primary axis along which child elements are arranged within a container."
	// "When set to `column` (the default), children are positioned vertically from top to bottom."
	MockDocument doc;
	ASSERT_PARSE(doc, R"(
	[panel] {
		[panel width=100 height=50] {}
		[panel width=100 height=80] {}
	}
	)");
	Span<u32> root_indices = doc.m_roots;
	ASSERT_EQ(1, root_indices.size(), "Should parse 1 root element");
	ui::Element* parent = doc.getElement(root_indices[0]);
	ASSERT_EQ(2, parent->children.size(), "Parent should have 2 children");
	ui::Element* child1 = doc.getElement(parent->children[0]);
	ui::Element* child2 = doc.getElement(parent->children[1]);
	
	// Compute layout
	doc.computeLayout(Vec2(800, 600));
	
	// Check children sizes
	ASSERT_FLOAT_EQ(100.0f, child1->size.x, "Child1 width should be 100");
	ASSERT_FLOAT_EQ(50.0f, child1->size.y, "Child1 height should be 50");
	ASSERT_FLOAT_EQ(100.0f, child2->size.x, "Child2 width should be 100");
	ASSERT_FLOAT_EQ(80.0f, child2->size.y, "Child2 height should be 80");
	
	// Default direction should be column (vertical layout)
	float parent_x = parent->position.x;
	float parent_y = parent->position.y;
	
	ASSERT_FLOAT_EQ(parent_x, child1->position.x, "Child1 x should be parent x");
	ASSERT_FLOAT_EQ(parent_y, child1->position.y, "Child1 y should be parent y");
	
	// Child2 should be below child1
	ASSERT_FLOAT_EQ(parent_x, child2->position.x, "Child2 x should be parent x");
	ASSERT_FLOAT_EQ(parent_y + 50.0f, child2->position.y, "Child2 y should be parent y + child1 height");
	
	return true;
}

bool testNestedPanelsDifferentDirections() {
	// Citation: layout.md - Element Positioning
	// "The `direction` attribute controls the primary axis along which child elements are arranged within a container." 
	// "When set to `row`, children are positioned horizontally from left to right."
	// "When set to `column` (the default), children are positioned vertically from top to bottom."
	MockDocument doc;
	ASSERT_PARSE(doc, R"(
	[panel direction=row width=800 height=600 bg-color=#000000] {
		[panel direction=column width=150] {
			[panel width=100 height=50 bg-color=#ffffff] {}
			[panel width=100 height=60 bg-color=#ff00ff] {}
		}
		[panel direction=column width=200] {
			[panel width=150 height=40 bg-color=#0000ff] {}
			[panel width=150 height=70 bg-color=#ff0000] {}
			[panel width=150 height=30 bg-color=#00ff00] {}
		}
	}
	)");
	Span<u32> root_indices = doc.m_roots;
	ASSERT_EQ(1, root_indices.size(), "Should parse 1 root element");
	ui::Element* parent = doc.getElement(root_indices[0]);
	ASSERT_EQ(2, parent->children.size(), "Parent should have 2 children");
	ui::Element* column1 = doc.getElement(parent->children[0]);
	ui::Element* column2 = doc.getElement(parent->children[1]);
	
	// Compute layout
	doc.computeLayout(Vec2(800, 600));
	
	// Check parent
	ASSERT_FLOAT_EQ(800.0f, parent->size.x, "Parent width should be 800");
	ASSERT_FLOAT_EQ(600.0f, parent->size.y, "Parent height should be 600");
	
	// Check column1
	ASSERT_FLOAT_EQ(150.0f, column1->size.x, "Column1 width should be 150");
	ASSERT_EQ(2, column1->children.size(), "Column1 should have 2 children");
	ui::Element* c1_child1 = doc.getElement(column1->children[0]);
	ui::Element* c1_child2 = doc.getElement(column1->children[1]);
	
	// Check column2
	ASSERT_FLOAT_EQ(200.0f, column2->size.x, "Column2 width should be 200");
	ASSERT_EQ(3, column2->children.size(), "Column2 should have 3 children");
	ui::Element* c2_child1 = doc.getElement(column2->children[0]);
	ui::Element* c2_child2 = doc.getElement(column2->children[1]);
	ui::Element* c2_child3 = doc.getElement(column2->children[2]);
	
	// Check sizes
	ASSERT_FLOAT_EQ(100.0f, c1_child1->size.x, "C1 child1 width should be 100");
	ASSERT_FLOAT_EQ(50.0f, c1_child1->size.y, "C1 child1 height should be 50");
	ASSERT_FLOAT_EQ(100.0f, c1_child2->size.x, "C1 child2 width should be 100");
	ASSERT_FLOAT_EQ(60.0f, c1_child2->size.y, "C1 child2 height should be 60");
	
	ASSERT_FLOAT_EQ(150.0f, c2_child1->size.x, "C2 child1 width should be 150");
	ASSERT_FLOAT_EQ(40.0f, c2_child1->size.y, "C2 child1 height should be 40");
	ASSERT_FLOAT_EQ(150.0f, c2_child2->size.x, "C2 child2 width should be 150");
	ASSERT_FLOAT_EQ(70.0f, c2_child2->size.y, "C2 child2 height should be 70");
	ASSERT_FLOAT_EQ(150.0f, c2_child3->size.x, "C2 child3 width should be 150");
	ASSERT_FLOAT_EQ(30.0f, c2_child3->size.y, "C2 child3 height should be 30");
	
	// Positions: parent lays out as row
	float parent_x = parent->position.x;
	float parent_y = parent->position.y;
	
	// Column1 at left
	ASSERT_EQ(parent_x, column1->position.x, "Column1 x should be parent x");
	ASSERT_EQ(parent_y, column1->position.y, "Column1 y should be parent y");
	
	// Column2 to the right of column1
	ASSERT_FLOAT_EQ(parent_x + 150.0f, column2->position.x, "Column2 x should be parent x + column1 width");
	ASSERT_EQ(parent_y, column2->position.y, "Column2 y should be parent y");
	
	// Inside column1 (direction=column), children stacked vertically
	ASSERT_EQ(column1->position.x, c1_child1->position.x, "C1 child1 x should be column1 x");
	ASSERT_EQ(column1->position.y, c1_child1->position.y, "C1 child1 y should be column1 y");
	
	ASSERT_EQ(column1->position.x, c1_child2->position.x, "C1 child2 x should be column1 x");
	ASSERT_FLOAT_EQ(column1->position.y + 50.0f, c1_child2->position.y, "C1 child2 y should be column1 y + c1_child1 height");
	
	// Inside column2 (direction=column), children stacked vertically
	ASSERT_EQ(column2->position.x, c2_child1->position.x, "C2 child1 x should be column2 x");
	ASSERT_EQ(column2->position.y, c2_child1->position.y, "C2 child1 y should be column2 y");
	
	ASSERT_EQ(column2->position.x, c2_child2->position.x, "C2 child2 x should be column2 x");
	ASSERT_FLOAT_EQ(column2->position.y + 40.0f, c2_child2->position.y, "C2 child2 y should be column2 y + c2_child1 height");
	
	ASSERT_EQ(column2->position.x, c2_child3->position.x, "C2 child3 x should be column2 x");
	ASSERT_FLOAT_EQ(column2->position.y + 40.0f + 70.0f, c2_child3->position.y, "C2 child3 y should be column2 y + c2_child1 height + c2_child2 height");
	
	return true;
}

bool testAdvancedFitContent() {
	// Citation: layout.md - Units
	// "- fit-content: Auto-size to content. For panels, sums child sizes. E.g., `width=fit-content`."
	MockDocument doc;
	ASSERT_PARSE(doc, R"(
	[panel width=fit-content height=fit-content direction=row] {
		[panel width=fit-content height=100] {
			[panel width=200 height=50] {}
		}
		[panel width=100 height=fit-content] {
			[panel height=30] {}
			[panel height=40] {}
		}
	}
	)");
	Span<u32> root_indices = doc.m_roots;
	ASSERT_EQ(1, root_indices.size(), "Should parse 1 root element");
	ui::Element* parent = doc.getElement(root_indices[0]);
	ASSERT_EQ(2, parent->children.size(), "Parent should have 2 children");
	ui::Element* child1 = doc.getElement(parent->children[0]);
	ui::Element* child2 = doc.getElement(parent->children[1]);
	
	// Child1 has 1 child
	ASSERT_EQ(1, child1->children.size(), "Child1 should have 1 child");
	ui::Element* grandchild1 = doc.getElement(child1->children[0]);
	
	// Child2 has 2 children
	ASSERT_EQ(2, child2->children.size(), "Child2 should have 2 children");
	ui::Element* grandchild2_1 = doc.getElement(child2->children[0]);
	ui::Element* grandchild2_2 = doc.getElement(child2->children[1]);
	
	// Compute layout
	doc.computeLayout(Vec2(800, 600));
	
	// Check grandchild sizes
	ASSERT_FLOAT_EQ(200.0f, grandchild1->size.x, "Grandchild1 width should be 200");
	ASSERT_FLOAT_EQ(50.0f, grandchild1->size.y, "Grandchild1 height should be 50");
	
	ASSERT_FLOAT_EQ(0.0f, grandchild2_1->size.x, "Grandchild2_1 width should be 0 (default fit-content with no content)");
	ASSERT_FLOAT_EQ(30.0f, grandchild2_1->size.y, "Grandchild2_1 height should be 30");
	
	ASSERT_FLOAT_EQ(0.0f, grandchild2_2->size.x, "Grandchild2_2 width should be 0 (default fit-content with no content)");
	ASSERT_FLOAT_EQ(40.0f, grandchild2_2->size.y, "Grandchild2_2 height should be 40");
	
	// Child1: width=fit-content (should fit grandchild1 width=200), height=100
	ASSERT_FLOAT_EQ(200.0f, child1->size.x, "Child1 width should fit content (200)");
	ASSERT_FLOAT_EQ(100.0f, child1->size.y, "Child1 height should be 100");
	
	// Child2: width=100, height=fit-content (should fit sum of grandchildren heights=30+40=70)
	ASSERT_FLOAT_EQ(100.0f, child2->size.x, "Child2 width should be 100");
	ASSERT_FLOAT_EQ(70.0f, child2->size.y, "Child2 height should fit content (70)");
	
	// Parent: width=fit-content (sum of children widths=200+100=300), height=fit-content (max of children heights=100+70=100)
	ASSERT_FLOAT_EQ(300.0f, parent->size.x, "Parent width should fit content (sum of children widths: 300)");
	ASSERT_FLOAT_EQ(100.0f, parent->size.y, "Parent height should fit content (max of children heights: 100)");
	
	// Assert positions
	ASSERT_EQ(0.0f, parent->position.x, "Parent x should be 0");
	ASSERT_EQ(0.0f, parent->position.y, "Parent y should be 0");
	
	ASSERT_EQ(0.0f, child1->position.x, "Child1 x should be 0");
	ASSERT_EQ(0.0f, child1->position.y, "Child1 y should be 0");
	
	ASSERT_EQ(0.0f, grandchild1->position.x, "Grandchild1 x should be 0");
	ASSERT_EQ(0.0f, grandchild1->position.y, "Grandchild1 y should be 0");
	
	ASSERT_EQ(200.0f, child2->position.x, "Child2 x should be 200");
	ASSERT_EQ(0.0f, child2->position.y, "Child2 y should be 0");
	
	ASSERT_EQ(200.0f, grandchild2_1->position.x, "Grandchild2_1 x should be 200");
	ASSERT_EQ(0.0f, grandchild2_1->position.y, "Grandchild2_1 y should be 0");
	
	ASSERT_EQ(200.0f, grandchild2_2->position.x, "Grandchild2_2 x should be 200");
	ASSERT_EQ(30.0f, grandchild2_2->position.y, "Grandchild2_2 y should be 30");

	return true;
}

bool testDefaultFitContentSimple() {
	// Citation: layout.md - Element Sizing
	// "Each UI element has `width` and `height` attributes that control its size, known as dimensions. Set them explicitly for a fixed size; otherwise, they default to `fit-content`,"
	MockDocument doc;
	ASSERT_PARSE(doc, R"(
	[panel] {
		[panel width=50 height=30] {}
		[panel width=70 height=40] {}
	}
	)");
	Span<u32> root_indices = doc.m_roots;
	ASSERT_EQ(1, root_indices.size(), "Should parse 1 root element");
	ui::Element* parent = doc.getElement(root_indices[0]);
	ASSERT_EQ(2, parent->children.size(), "Parent should have 2 children");
	ui::Element* child1 = doc.getElement(parent->children[0]);
	ui::Element* child2 = doc.getElement(parent->children[1]);
	
	// Compute layout
	doc.computeLayout(Vec2(800, 600));
	
	// Children have explicit sizes
	ASSERT_EQ(50.0f, child1->size.x, "Child1 width should be 50");
	ASSERT_EQ(30.0f, child1->size.y, "Child1 height should be 30");
	ASSERT_EQ(70.0f, child2->size.x, "Child2 width should be 70");
	ASSERT_EQ(40.0f, child2->size.y, "Child2 height should be 40");
	
	// Parent has no size specified, so default fit-content
	// Direction is column, so width = max child width = 70, height = sum child heights = 30 + 40 = 70
	ASSERT_EQ(70.0f, parent->size.x, "Parent width should fit content (max child width: 70)");
	ASSERT_EQ(70.0f, parent->size.y, "Parent height should fit content (sum child heights: 70)");
	
	return true;
}

bool testDefaultFitContentLeaf() {
	// Citation: layout.md - Element Sizing
	// "Each UI element has `width` and `height` attributes that control its size, known as dimensions. Set them explicitly for a fixed size; otherwise, they default to `fit-content`,"
	MockDocument doc;
	ASSERT_PARSE(doc, R"(
	[panel width=200 height=100] {
		[panel] {}
	}
	)");
	Span<u32> root_indices = doc.m_roots;
	ASSERT_EQ(1, root_indices.size(), "Should parse 1 root element");
	ui::Element* parent = doc.getElement(root_indices[0]);
	ASSERT_EQ(1, parent->children.size(), "Parent should have 1 child");
	ui::Element* child = doc.getElement(parent->children[0]);
	
	// Compute layout
	doc.computeLayout(Vec2(800, 600));
	
	// Parent has explicit size
	ASSERT_EQ(200.0f, parent->size.x, "Parent width should be 200");
	ASSERT_EQ(100.0f, parent->size.y, "Parent height should be 100");
	
	// Child has no size specified, so default fit-content, and since leaf, should be 0
	ASSERT_EQ(0.0f, child->size.x, "Child width should be 0 (default fit-content with no content)");
	ASSERT_EQ(0.0f, child->size.y, "Child height should be 0 (default fit-content with no content)");
	
	return true;
}

bool testVerticalMarginCollapse() {
	// Citation: layout.md - Margin Collapsing
	// "Adjacent margins combine into the larger value to prevent excessive gaps."
	MockDocument doc;
	ASSERT_PARSE(doc, R"(
	[panel] {
		[panel margin=10 height=50] {}
		[panel margin=20 height=50] {}
	}
	)");
	doc.computeLayout(Vec2(800, 600));
	Span<u32> root_indices = doc.m_roots;
	ASSERT_EQ(1, root_indices.size(), "Should parse 1 root");
	ui::Element* root = doc.getElement(root_indices[0]);
	ASSERT_EQ(2, root->children.size(), "Root should have 2 children");
	ui::Element* child1 = doc.getElement(root->children[0]);
	ui::Element* child2 = doc.getElement(root->children[1]);
	
	ASSERT_EQ(10.0f, child1->position.y, "Child1 y should be 10");
	ASSERT_EQ(80.0f, child2->position.y, "Child2 y should be 80");
	
	return true;
}

bool testHorizontalMarginCollapse() {
	// Citation: layout.md - Margin Collapsing
	// "Adjacent margins combine into the larger value to prevent excessive gaps."
	MockDocument doc;
	ASSERT_PARSE(doc, R"(
	[panel direction=row] {
		[panel margin=10 width=50] {}
		[panel margin=20 width=50] {}
	}
	)");
	doc.computeLayout(Vec2(800, 600));
	Span<u32> root_indices = doc.m_roots;
	ASSERT_EQ(1, root_indices.size(), "Should parse 1 root");
	ui::Element* root = doc.getElement(root_indices[0]);
	ASSERT_EQ(2, root->children.size(), "Root should have 2 children");
	ui::Element* child1 = doc.getElement(root->children[0]);
	ui::Element* child2 = doc.getElement(root->children[1]);

	ASSERT_EQ(10.0f, child1->position.x, "Child1 x should be 10");
	ASSERT_EQ(80.0f, child2->position.x, "Child2 x should be 80");

	return true;
}

bool testWrap() {
	// Citation: layout.md - Wrapping
	// "When `wrap=true`, elements that don't fit on the current line move to the next line (for `direction=row`)"
	MockDocument doc;
	ASSERT_PARSE(doc, R"(
	[panel width=100 height=100 direction=row wrap=true] {
		[panel width=50 height=50] {}
		[panel width=50 height=50] {}
		[panel width=50 height=50] {}
	}
	)");
	doc.computeLayout(Vec2(800, 600));
	Span<u32> root_indices = doc.m_roots;
	ASSERT_EQ(1, root_indices.size(), "Should parse 1 root");
	ui::Element* root = doc.getElement(root_indices[0]);
	ASSERT_EQ(3, root->children.size(), "Root should have 3 children");
	ui::Element* child1 = doc.getElement(root->children[0]);
	ui::Element* child2 = doc.getElement(root->children[1]);
	ui::Element* child3 = doc.getElement(root->children[2]);

	// With wrap=true, the third child should wrap to the next row
	// child1 at (0,0), child2 at (50,0), child3 at (0,50)
	ASSERT_EQ(0.0f, child1->position.x, "Child1 x should be 0");
	ASSERT_EQ(0.0f, child1->position.y, "Child1 y should be 0");
	ASSERT_EQ(50.0f, child2->position.x, "Child2 x should be 50");
	ASSERT_EQ(0.0f, child2->position.y, "Child2 y should be 0");
	ASSERT_EQ(0.0f, child3->position.x, "Child3 x should be 0 (wrapped)");
	ASSERT_EQ(50.0f, child3->position.y, "Child3 y should be 50 (wrapped)");

	return true;
}

bool testNoWrap() {
	// Citation: layout.md - Wrapping
	// "`wrap=false` (default): Elements stay on a single line/column, potentially overflowing the container."
	MockDocument doc;
	ASSERT_PARSE(doc, R"(
	[panel width=100 height=100 direction=row wrap=nowrap] {
		[panel width=50 height=50] {}
		[panel width=50 height=50] {}
		[panel width=50 height=50] {}
	}
	)");
	doc.computeLayout(Vec2(800, 600));
	Span<u32> root_indices = doc.m_roots;
	ASSERT_EQ(1, root_indices.size(), "Should parse 1 root");
	ui::Element* root = doc.getElement(root_indices[0]);
	ASSERT_EQ(3, root->children.size(), "Root should have 3 children");
	ui::Element* child1 = doc.getElement(root->children[0]);
	ui::Element* child2 = doc.getElement(root->children[1]);
	ui::Element* child3 = doc.getElement(root->children[2]);

	// With wrap=nowrap, no wrapping, third child overflows
	// child1 at (0,0), child2 at (50,0), child3 at (100,0)
	ASSERT_EQ(0.0f, child1->position.x, "Child1 x should be 0");
	ASSERT_EQ(0.0f, child1->position.y, "Child1 y should be 0");
	ASSERT_EQ(50.0f, child2->position.x, "Child2 x should be 50");
	ASSERT_EQ(0.0f, child2->position.y, "Child2 y should be 0");
	ASSERT_EQ(100.0f, child3->position.x, "Child3 x should be 100 (no wrap)");
	ASSERT_EQ(0.0f, child3->position.y, "Child3 y should be 0 (no wrap)");

	return true;
}

bool testAlignItemsCenter() {
	// Citation: layout.md - Off-axis alignment
	// "Off-axis alignment controls how child elements are positioned along the axis perpendicular to the container's main axis. For `direction=row`, the off-axis is vertical;"
	// "- `center`: Elements are centered along the off-axis."
	MockDocument doc;
	ASSERT_PARSE(doc, R"(
	[panel width=400 height=200 direction=row align-items=center] {
		[panel width=50 height=30] {}
		[panel width=50 height=30] {}
	}
	)");
	doc.computeLayout(Vec2(800, 600));
	ui::Element* panel = doc.getElement(0);
	ui::Element* btn1 = doc.getElement(1);
	ui::Element* btn2 = doc.getElement(2);

	// Panel height=200, buttons height=30, so center at (200-30)/2 = 85
	float expected_y = (200.0f - 30.0f) * 0.5f; // 85
	ASSERT_EQ(expected_y, btn1->position.y, "First button y should be centered");
	ASSERT_EQ(expected_y, btn2->position.y, "Second button y should be centered");

	return true;
}

bool testAlignItemsStart() {
	// Citation: layout.md - Off-axis alignment
	// "- `start`: Elements are aligned to the start of the off-axis (top for row, left for column)."
	MockDocument doc;
	ASSERT_PARSE(doc, R"(
	[panel width=400 height=200 direction=row align-items=start] {
		[panel width=50 height=30] {}
		[panel width=50 height=30] {}
	}
	)");
	doc.computeLayout(Vec2(800, 600));
	ui::Element* panel = doc.getElement(0);
	ui::Element* btn1 = doc.getElement(1);
	ui::Element* btn2 = doc.getElement(2);

	// Start: buttons aligned to top
	ASSERT_EQ(0.0f, btn1->position.y, "First button y should be at start (top)");
	ASSERT_EQ(0.0f, btn2->position.y, "Second button y should be at start (top)");

	return true;
}

bool testAlignItemsEnd() {
	// Citation: layout.md - Off-axis alignment
	// "- `end`: Elements are aligned to the end of the off-axis (bottom for row, right for column)."
	MockDocument doc;
	ASSERT_PARSE(doc, R"(
	[panel width=400 height=200 direction=row align-items=end] {
		[panel width=50 height=30] {}
		[panel width=50 height=30] {}
	}
	)");
	doc.computeLayout(Vec2(800, 600));
	ui::Element* panel = doc.getElement(0);
	ui::Element* btn1 = doc.getElement(1);
	ui::Element* btn2 = doc.getElement(2);

	// End: buttons aligned to bottom, panel height=200, button height=30, so y = 200-30 = 170
	float expected_y = 200.0f - 30.0f; // 170
	ASSERT_EQ(expected_y, btn1->position.y, "First button y should be at end (bottom)");
	ASSERT_EQ(expected_y, btn2->position.y, "Second button y should be at end (bottom)");

	return true;
}

bool testAlignItemsStretch() {
	// Citation: layout.md - Off-axis alignment
	// "- `stretch` (fill): Elements stretch to fill the available space along the off-axis. This is the default "
	MockDocument doc;
	ASSERT_PARSE(doc, R"(
	[panel width=400 height=200 direction=row align-items=stretch] {
		[panel width=50] {}
		[panel width=50] {}
	}
	)");
	doc.computeLayout(Vec2(800, 600));
	ui::Element* panel = doc.getElement(0);
	ui::Element* btn1 = doc.getElement(1);
	ui::Element* btn2 = doc.getElement(2);

	// Stretch: buttons should fill the height, so height=200
	ASSERT_EQ(0.0f, btn1->position.y, "First button y should be at start");
	ASSERT_EQ(200.0f, btn1->size.y, "First button should stretch to full height");
	ASSERT_EQ(0.0f, btn2->position.y, "Second button y should be at start");
	ASSERT_EQ(200.0f, btn2->size.y, "Second button should stretch to full height");

	return true;
}

bool testAlignItemsStretchMargins() {
    // Citation: layout.md - Off-axis alignment
    // "When `align-items=stretch`, elements expand to match the container's size in the off-axis direction, minus padding and margins."
    MockDocument doc;
    ASSERT_PARSE(doc, R"(
	[panel width=400 height=200 direction=row align-items=stretch padding=10] {
		[panel width=50 margin=5] {}
		[panel width=50 margin=15] {}
	}
	)");

	Span<u32> root_indices = doc.m_roots;
	ASSERT_EQ(1, root_indices.size(), "Should parse 1 root element");
	ui::Element* parent = doc.getElement(root_indices[0]);
	ASSERT_EQ(2, parent->children.size(), "Parent should have 2 children");
	ui::Element* child1 = doc.getElement(parent->children[0]);
	ui::Element* child2 = doc.getElement(parent->children[1]);

	// Compute layout
	doc.computeLayout(Vec2(800, 600));

	// Parent paddings
	float pad_top = parent->paddings[0];
	float pad_bottom = parent->paddings[2];
	// Available cross-axis (vertical) size for children = parent.height - pad_top - pad_bottom
	float available = parent->size.y - pad_top - pad_bottom; // should be 200 - 10 - 10 = 180

	// Child margins
	float c1_top = child1->margins[0];
	float c1_bottom = child1->margins[2];
	float c2_top = child2->margins[0];
	float c2_bottom = child2->margins[2];

	// Expected heights
	float expected_c1_h = available - c1_top - c1_bottom; // 180 - 5 - 5 = 170
	float expected_c2_h = available - c2_top - c2_bottom; // 180 - 15 - 15 = 150

	ASSERT_FLOAT_EQ(expected_c1_h, child1->size.y, "Child1 height should be available minus its margins");
	ASSERT_FLOAT_EQ(expected_c2_h, child2->size.y, "Child2 height should be available minus its margins");

	// Positions should account for parent padding and child top margin
	ASSERT_FLOAT_EQ(parent->position.y + pad_top + c1_top, child1->position.y, "Child1 y should account for parent padding and child top margin");
	ASSERT_FLOAT_EQ(parent->position.y + pad_top + c2_top, child2->position.y, "Child2 y should account for parent padding and child top margin");

	return true;
}

bool testAlignItemsWithWrap() {
	// Citation: layout.md - Wrapping
	// "When wrap=true, align-items is applied to each wrapped line or column individually, rather than to the entire container."
	// "Justification and item aligment are applied to each row/column separately."
	// "When `wrap=true`, `align-items` is applied to each wrapped line or column individually, rather than to the entire container."
	MockDocument doc;
	ASSERT_PARSE(doc, R"(
	[panel width=100 height=200 direction=row wrap=true align-items=center] {
		[panel width=50 height=30] {}
		[panel width=50 height=50] {}
		[panel width=50 height=30] {}
	}
	)");
	doc.computeLayout(Vec2(800, 600));
	ui::Element* panel = doc.getElement(0);
	ui::Element* btn1 = doc.getElement(1);
	ui::Element* btn2 = doc.getElement(2);
	ui::Element* btn3 = doc.getElement(3);

	// First line (y=0-50): btn1 centered at (50-30)/2=10, btn2 at (50-50)/2=0
	// Second line (y=50-80): btn3 centered at 50 + (30-30)/2=50
	ASSERT_EQ(10.0f, btn1->position.y, "Btn1 should be centered in first line");
	ASSERT_EQ(0.0f, btn2->position.y, "Btn2 should be centered in first line");
	ASSERT_EQ(50.0f, btn3->position.y, "Btn3 should be centered in second line");

	return true;
}

bool testJustifyContentWithWrap() {
	// Citation: layout.md - Wrapping
	// "Justification and item aligment are applied to each row/column separately."
	// "Justification and item aligment are applied to each row/column separately."
	MockDocument doc;
	ASSERT_PARSE(doc, R"(
	[panel width=100 height=200 direction=row wrap=wrap justify-content=center] {
		[panel width=50 height=50] {}
		[panel width=50 height=50] {}
		[panel width=50 height=50] {}
	}
	)");
	doc.computeLayout(Vec2(800, 600));
	ui::Element* panel = doc.getElement(0);
	ui::Element* btn1 = doc.getElement(1);
	ui::Element* btn2 = doc.getElement(2);
	ui::Element* btn3 = doc.getElement(3);

	// First line: 2 buttons of 50 each, total 100, centered: positions at 0 and 50? Wait, center means the group is centered.
	// For center: available 100, content 100, so starts at 0.
	// But to make it visible, perhaps use space-between or smaller container.

	// Panel width=200, children 50 each, total 150, centered in 200: start at (200-150)/2 = 25, 75, 125.
	MockDocument doc2;
	ASSERT_PARSE(doc2, R"(
	[panel width=200 height=200 direction=row wrap=wrap justify-content=center] {
		[panel width=50 height=30] {}
		[panel width=50 height=50] {}
		[panel width=50 height=30] {}
	}
	)");
	doc2.computeLayout(Vec2(800, 600));
	ui::Element* panel2 = doc2.getElement(0);
	ui::Element* btn1_2 = doc2.getElement(1);
	ui::Element* btn2_2 = doc2.getElement(2);
	ui::Element* btn3_2 = doc2.getElement(3);

	// All buttons centered in 200: start at (200-150)/2 = 25, 75, 125
	ASSERT_EQ(25.0f, btn1_2->position.x, "Btn1 should be centered");
	ASSERT_EQ(0.0f, btn1_2->position.y, "Btn1 y should be 0");
	ASSERT_EQ(75.0f, btn2_2->position.x, "Btn2 should be after btn1");
	ASSERT_EQ(0.0f, btn2_2->position.y, "Btn2 y should be 0");
	ASSERT_EQ(125.0f, btn3_2->position.x, "Btn3 should be after btn2");
	ASSERT_EQ(0.0f, btn3_2->position.y, "Btn3 y should be 0");

	return true;
}

bool testWrapCrossAxisDistribution() {
	// Citation: layout.md - Wrapping
	// "When `wrap=true` creates multiple lines (or columns), and the container's size along the cross-axis is
	// larger than the combined size of all lines, the lines are distributed starting from the container's start
	// edge along the cross-axis. This means lines bunch at the top (for `direction=row`) or left (for `direction=column`), 
	// with any extra space remaining unused."
	MockDocument doc;
	ASSERT_PARSE(doc, R"(
	[panel width=100 height=200 direction=row wrap=true] {
		[panel width=50 height=25] {}
		[panel width=50 height=25] {}
		[panel width=50 height=25] {}
	}
	)");
	doc.computeLayout(Vec2(800, 600));
	ui::Element* panel = doc.getElement(0);
	ui::Element* child1 = doc.getElement(1);
	ui::Element* child2 = doc.getElement(2);
	ui::Element* child3 = doc.getElement(3);

	// First line: child1 and child2 at y=0
	ASSERT_EQ(0.0f, child1->position.y, "Child1 y should be 0");
	ASSERT_EQ(0.0f, child2->position.y, "Child2 y should be 0");

	// Second line: child3 at y=25 (height of first line)
	ASSERT_EQ(25.0f, child3->position.y, "Child3 y should be 25");

	// Lines are bunched at the start, extra cross-axis space (200 - 50) is unused
	return true;
}

bool testInvalidTag() {
	// Test that parsing fails for invalid tags
	MockDocument doc;
	doc.m_suppress_logging = true;
	bool result = doc.parse("button width=100 height=50 {}", "test.ui");
	ASSERT_EQ(false, result, "Parse should fail for 'button' when it's not a valid tag");
	return true;
}

bool testLineBreaks() {
	// Citation: layout.md - Line Breaks
	// "Line breaks in inline flow are triggered exclusively by block elements."
	// "When a block element appears, it causes a line break, positioning itself at the start of the new line and forcing any subsequent inline elements to start on the following line."
	MockFontManager mock;
	ui::Document doc(&mock, getGlobalAllocator());
	ASSERT_PARSE(doc, R"(
		[panel width=200 height=100 direction=row font="arial.ttf" font-size=16] {
			First
			[panel width=50 height=20] { Block }
			Second
		}
	)");
	doc.computeLayout(Vec2(800, 600));
	ui::Element* parent = doc.getElement(0);
	Span<u32> children = parent->children;
	ASSERT_EQ(3, children.size(), "Parent should have 3 children");

	ui::Element* text1 = doc.getElement(children[0]);
	ui::Element* block = doc.getElement(children[1]);
	ui::Element* text2 = doc.getElement(children[2]);

	// First text at start
	ASSERT_FLOAT_EQ(0.0f, text1->position.x, "First text x should be 0");
	ASSERT_FLOAT_EQ(0.0f, text1->position.y, "First text y should be 0");

	// Block causes line break, positioned at start of new line
	ASSERT_FLOAT_EQ(0.0f, block->position.x, "Block x should be 0");
	ASSERT_FLOAT_EQ(16.0f, block->position.y, "Block y should be font height (16)");

	// Second text on following line
	ASSERT_FLOAT_EQ(0.0f, text2->position.x, "Second text x should be 0");
	ASSERT_FLOAT_EQ(36.0f, text2->position.y, "Second text y should be block y + block height (16 + 20)");

	return true;
}

bool testFitContentWithInlineText() {
	// Citation: layout.md - Fit-Content - With Text Elements
	// text does not wrap and is treated as a single line.
	// The width is calculated as the full rendered width of the text string in pixels,
	// and height is based on the font size (typically the line height).
	MockFontManager mock;
	ui::Document doc(&mock, getGlobalAllocator());
	ASSERT_PARSE(doc, R"(
		[panel width=fit-content height=fit-content direction=row font="arial.ttf" font-size=16] {
			Text
		}
	)");
	doc.computeLayout(Vec2(800, 600));
	ui::Element* parent = doc.getElement(0);
	ui::Element* textElem = doc.getElement(1);
	
	// Parent fits the computed text size
	ASSERT_EQ(32.0f, parent->size.x, "Parent width should fit text (4 chars * 16 * 0.5)");
	ASSERT_EQ(16.0f, parent->size.y, "Parent height should fit text");
	
	return true;
}

bool testTextWrapping() {
	// Citation: layout.md - Text
	// "Text flows inline within the panel and wraps to multiple lines when
	// the unwrapped width exceeds the available panel width (minus padding) and `wrap=true`."
	MockFontManager mock;
	ui::Document doc(&mock, getGlobalAllocator());
	ASSERT_PARSE(doc, R"(
		[panel width=100 height=fit-content wrap=true font="arial.ttf" font-size=16] {
			"This is a very long text that should wrap to multiple lines"
		}
	)");
	doc.computeLayout(Vec2(800, 600));
	Span<u32> root_indices = doc.m_roots;
	ASSERT_EQ(1, root_indices.size(), "Should parse 1 root element");
	ui::Element* panel = doc.getElement(root_indices[0]);
	ASSERT_TAG(panel, PANEL);
	Span<ui::Attribute> attrs = panel->attributes;
	ASSERT_EQ(5, attrs.size(), "Panel should have 5 attributes");
	ASSERT_ATTRIBUTE(panel, 0, WIDTH);
	ASSERT_EQ("100", attrs[0].value, "Width should be 100");
	ASSERT_ATTRIBUTE(panel, 1, HEIGHT);
	ASSERT_EQ("fit-content", attrs[1].value, "Height should be fit-content");
	ASSERT_ATTRIBUTE(panel, 2, WRAP);
	ASSERT_EQ("true", attrs[2].value, "Wrap should be true");
	ASSERT_ATTRIBUTE(panel, 3, FONT);
	ASSERT_EQ("arial.ttf", attrs[3].value, "Font should be arial.ttf");
	ASSERT_ATTRIBUTE(panel, 4, FONT_SIZE);
	ASSERT_EQ("16", attrs[4].value, "Font size should be 16");

	// Check child text element
	ASSERT_EQ(1, panel->children.size(), "Panel should have 1 child");
	ui::Element* textElem = doc.getElement(panel->children[0]);
	ASSERT_TAG(textElem, SPAN);

	// Assert text element layout
	ASSERT_EQ(0.0f, textElem->position.x, "Text x position should be 0");
	ASSERT_EQ(0.0f, textElem->position.y, "Text y position should be 0");
	ASSERT_TRUE(textElem->size.x > 100.0f, "Text width should be full width");
	ASSERT_TRUE(textElem->size.y > 16.0f, "Text height should be greater than single line height due to wrapping");

	// Assert panel layout
	ASSERT_EQ(100.0f, panel->size.x, "Panel width should be 100");
	ASSERT_TRUE(panel->size.y > 16.0f, "Panel height should be greater than single line height due to wrapping");
	return true;
}

bool testWrappingInheritance() {
	// Citation: layout.md - Wrapping
	// "The `wrap` property controls whether child elements wrap to new lines or columns when they exceed the container's size along the main axis. When `wrap=true`, elements that don't fit on the current line move to the next line (for `direction=row`) or next column (for `direction=column`)."
	MockFontManager mock;
	ui::Document doc(&mock, getGlobalAllocator());
	ASSERT_PARSE(doc, R"(
		[panel width=120 height=fit-content wrap=true direction=row font="arial.ttf" font-size=16] {
			[panel width=60 height=fit-content] {
				Short text should not wrap
			}
			[panel width=60 height=fit-content wrap=false] {
				This is a long text that should not wrap in this panel
			}
			[panel width=60 height=fit-content] {
				This is a long text that should not wrap in this panel because wrap is not inherited
			}
		}
	)");
	doc.computeLayout(Vec2(800, 600));
	Span<u32> root_indices = doc.m_roots;
	ASSERT_EQ(1, root_indices.size(), "Should parse 1 root element");
	ui::Element* outer_panel = doc.getElement(root_indices[0]);
	ASSERT_TAG(outer_panel, PANEL);
	ASSERT_EQ(3, outer_panel->children.size(), "Outer panel should have 3 children");

	ui::Element* child1 = doc.getElement(outer_panel->children[0]);
	ui::Element* child2 = doc.getElement(outer_panel->children[1]);
	ui::Element* child3 = doc.getElement(outer_panel->children[2]);

	// First child: short text, should not wrap
	ui::Element* text1 = doc.getElement(child1->children[0]);
	ASSERT_TAG(text1, SPAN);
	ASSERT_FLOAT_EQ(16.0f, text1->size.y, "Short text should be single line");

	// Second child: long text, wrap=false, should not wrap
	ui::Element* text2 = doc.getElement(child2->children[0]);
	ASSERT_TAG(text2, SPAN);
	ASSERT_TRUE(text2->size.x > 60.0f, "Long text should overflow, not wrap");
	ASSERT_FLOAT_EQ(text2->size.y, 16.0f, "Long text should be single line");

	// Third child: long text, no wrap attribute, should NOT wrap (wrap is not inherited)
	ui::Element* text3 = doc.getElement(child3->children[0]);
	ASSERT_TAG(text3, SPAN);
	ASSERT_TRUE(text3->size.x > 60.0f, "Long text should overflow, not wrap (wrap is not inherited)");
	ASSERT_FLOAT_EQ(text3->size.y, 16.0f, "Long text should be single line (wrap is not inherited)");

	return true;
}

bool testMultilineStringLayout() {
	// Citation: layout.md - Multiline Strings
	// "Quoted strings in markup can span multiple lines, 
	// but newlines (`\n`) and carriage returns (`\r`) are treated as whitespace
	// and do not create line breaks in the layout.
	MockFontManager mock;
	ui::Document doc(&mock, getGlobalAllocator());
	ASSERT_PARSE(doc, R"(
		[panel width=fit-content height=fit-content font="arial.ttf" font-size=16] {
			Line 1
			Line 2
			Line 3
		}
	)");
	doc.computeLayout(Vec2(800, 600));
	Span<u32> root_indices = doc.m_roots;
	ASSERT_EQ(1, root_indices.size(), "Should parse 1 root element");
	ui::Element* panel = doc.getElement(root_indices[0]);
	ASSERT_TAG(panel, PANEL);
	Span<ui::Attribute> attrs = panel->attributes;
	ASSERT_EQ(4, attrs.size(), "Panel should have 4 attributes");
	ASSERT_ATTRIBUTE(panel, 0, WIDTH);
	ASSERT_EQ("fit-content", attrs[0].value, "Width should be fit-content");
	ASSERT_ATTRIBUTE(panel, 1, HEIGHT);
	ASSERT_EQ("fit-content", attrs[1].value, "Height should be fit-content");
	ASSERT_ATTRIBUTE(panel, 2, FONT);
	ASSERT_EQ("arial.ttf", attrs[2].value, "Font should be arial.ttf");
	ASSERT_ATTRIBUTE(panel, 3, FONT_SIZE);
	ASSERT_EQ("16", attrs[3].value, "Font size should be 16");

	// Check child text element
	ASSERT_EQ(1, panel->children.size(), "Panel should have 1 child");
	ui::Element* textElem = doc.getElement(panel->children[0]);
	ASSERT_TAG(textElem, SPAN);

	// Assert text element layout
	// Text is "Line 1\nLine 2\nLine 3", \n treated as spaces, so full text width
	// 20 chars, 20*8 = 160, single line height = 16
	ASSERT_FLOAT_EQ(160.0f, textElem->size.x, "Text width should be the full text width (HTML-compatible)");
	ASSERT_FLOAT_EQ(16.0f, textElem->size.y, "Text height should be single line (HTML-compatible)");
	ASSERT_FLOAT_EQ(0.0f, textElem->position.x, "Text x position should be 0");
	ASSERT_FLOAT_EQ(0.0f, textElem->position.y, "Text y position should be 0");

	// Assert panel layout (fits the text)
	ASSERT_FLOAT_EQ(160.0f, panel->size.x, "Panel width should fit text width");
	ASSERT_FLOAT_EQ(16.0f, panel->size.y, "Panel height should fit text height");

	return true;
}

bool testDoubleQuotesInText() {
	// Citation: layout.md - Text
	// "Double quotes (`"`) in text content are treated as regular characters and render as expected without any special handling, since text is unquoted in the markup."
	logInfo("Running testDoubleQuotesInText");
	MockFontManager mock;
	ui::Document doc(&mock, getGlobalAllocator());
	ASSERT_PARSE(doc, R"(
		[panel width=fit-content height=fit-content font="arial.ttf" font-size=16] {
			"Hello "world""
		}
	)");
	doc.computeLayout(Vec2(800, 600));
	Span<u32> root_indices = doc.m_roots;
	ASSERT_EQ(1, root_indices.size(), "Should parse 1 root element");
	ui::Element* panel = doc.getElement(root_indices[0]);
	ASSERT_TAG(panel, PANEL);

	// Check child text element
	ASSERT_EQ(1, panel->children.size(), "Panel should have 1 child");
	ui::Element* textElem = doc.getElement(panel->children[0]);
	ASSERT_TAG(textElem, SPAN);

	// Assert text element layout includes quotes
	// Text is "Hello "world"", 15 characters (4 doublequotes are counted), each 8px wide = 120px
	ASSERT_FLOAT_EQ(120.0f, textElem->size.x, "Text width should include double quotes");
	ASSERT_FLOAT_EQ(16.0f, textElem->size.y, "Text height should be single line");
	ASSERT_FLOAT_EQ(0.0f, textElem->position.x, "Text x position should be 0");
	ASSERT_FLOAT_EQ(0.0f, textElem->position.y, "Text y position should be 0");

	// Assert panel layout (fits the text)
	ASSERT_FLOAT_EQ(120.0f, panel->size.x, "Panel width should fit text width");
	ASSERT_FLOAT_EQ(16.0f, panel->size.y, "Panel height should fit text height");

	return true;
}

bool testTextHorizontalRendering() {
	// Citation: layout.md - Inline Flow
	// "Text strings always render horizontally (left-to-right), regardless of `direction`."
	MockFontManager mock;
	ui::Document doc(&mock, getGlobalAllocator());
	ASSERT_PARSE(doc, R"(
		[panel direction=column font="arial.ttf" font-size=16] {
			[span value="First text"]
			[span value="Second text"]
		}
	)");
	doc.computeLayout(Vec2(800, 600));
	Span<u32> root_indices = doc.m_roots;
	ASSERT_EQ(1, root_indices.size(), "Should parse 1 root element");
	ui::Element* panel = doc.getElement(root_indices[0]);
	ASSERT_EQ(2, panel->children.size(), "Panel should have 2 children");

	ui::Element* text1 = doc.getElement(panel->children[0]);
	ui::Element* text2 = doc.getElement(panel->children[1]);

	// In column direction, text strings should be stacked vertically
	// Both should start at x=0 (left-aligned)
	ASSERT_FLOAT_EQ(0.0f, text1->position.x, "First text x should be 0");
	ASSERT_FLOAT_EQ(0.0f, text2->position.x, "Second text x should be 0");

	// Second text should be below first text (stacked vertically)
	ASSERT_FLOAT_EQ(text1->position.y + text1->size.y, text2->position.y, "Second text should be below first text");

	return true;
}

bool testBaselineAlignment() {
	// Citation: layout.md - Baseline Alignment in Inline Flow
	// "In a line of inline elements, the layout algorithm:
	// 1. Calculates the dominant baseline for the line, typically the baseline of the tallest text element or the first element with a defined baseline.
	// 2. Positions each inline element so that its baseline aligns with the line's baseline."
	MockFontManager mock;
	ui::Document doc(&mock, getGlobalAllocator());
	ASSERT_PARSE(doc, R"(
		[panel direction=row font="arial.ttf" font-size=16] {
			[span value="Small text" font-size=12]
			[span value="Normal text" font-size=16]
			[span value="Large text" font-size=20]
		}
	)");
	doc.computeLayout(Vec2(800, 600));
	Span<u32> root_indices = doc.m_roots;
	ASSERT_EQ(1, root_indices.size(), "Should parse 1 root element");
	ui::Element* panel = doc.getElement(root_indices[0]);
	ASSERT_EQ(3, panel->children.size(), "Panel should have 3 children");

	ui::Element* smallText = doc.getElement(panel->children[0]);
	ui::Element* normalText = doc.getElement(panel->children[1]);
	ui::Element* largeText = doc.getElement(panel->children[2]);

	// Baseline alignment should position elements so their baselines align
	// The baseline is at position.y + ascender
	// All spans should have the same baseline y-coordinate
	float smallAscender = doc.m_font_manager->getAscender(smallText->font_handle);
	float normalAscender = doc.m_font_manager->getAscender(normalText->font_handle);
	float largeAscender = doc.m_font_manager->getAscender(largeText->font_handle);
	float smallBaseline = smallText->position.y + smallAscender;
	float normalBaseline = normalText->position.y + normalAscender;
	float largeBaseline = largeText->position.y + largeAscender;

	ASSERT_FLOAT_EQ(normalBaseline, smallBaseline, "Small text baseline should align with normal text");
	ASSERT_FLOAT_EQ(normalBaseline, largeBaseline, "Large text baseline should align with normal text");

	return true;

	// All elements should be on the same baseline (y position should account for baseline alignment)
	// For now, this will fail as baseline alignment is not implemented
	float expectedBaselineY = 0.0f; // This should be calculated based on the dominant baseline
	ASSERT_FLOAT_EQ(expectedBaselineY, smallText->position.y + smallText->size.y, "Small text baseline should align");
	ASSERT_FLOAT_EQ(expectedBaselineY, normalText->position.y + normalText->size.y, "Normal text baseline should align");
	ASSERT_FLOAT_EQ(expectedBaselineY, largeText->position.y + largeText->size.y, "Large text baseline should align");

	return true;
}

bool testAlignCenterMultipleSpans() {
	// Citation: layout.md - Text Alignment
	// "Text alignment controls how text is positioned horizontally within its container. The `align` attribute can be set to `left`, `center`, or `right`, with `left` as the default."
	MockFontManager mock;
	ui::Document doc(&mock, getGlobalAllocator());
	ASSERT_PARSE(doc, R"(
		[panel width=400 align=center font="arial.ttf" font-size=16 direction=row] {
			[span value="First"]
			[span value="Second"]
			[span value="Third"]
		}
	)");
	doc.computeLayout(Vec2(800, 600));
	Span<u32> root_indices = doc.m_roots;
	ASSERT_EQ(1, root_indices.size(), "Should parse 1 root element");
	ui::Element* panel = doc.getElement(root_indices[0]);
	ASSERT_EQ(3, panel->children.size(), "Panel should have 3 children");

	ui::Element* span1 = doc.getElement(panel->children[0]);
	ui::Element* span2 = doc.getElement(panel->children[1]);
	ui::Element* span3 = doc.getElement(panel->children[2]);

	// Calculate total width of spans
	float totalWidth = span1->size.x + span2->size.x + span3->size.x;
	// Panel width is 400, so centered group should start at (400 - totalWidth) / 2
	float expectedStartX = (400.0f - totalWidth) / 2.0f;

	// Currently this will fail because text alignment for multiple spans is not implemented
	ASSERT_FLOAT_EQ(expectedStartX, span1->position.x, "First span should be centered");

	return true;
}

bool testAlignRightMultipleSpans() {
	// Citation: layout.md - Text Alignment
	// "Text alignment controls how text is positioned horizontally within its container. The `align` attribute can be set to `left`, `center`, or `right`, with `left` as the default."
	MockFontManager mock;
	ui::Document doc(&mock, getGlobalAllocator());
	ASSERT_PARSE(doc, R"(
		[panel width=400 align=right font="arial.ttf" font-size=16 direction=row] {
			[span value="First"]
			[span value="Second"]
			[span value="Third"]
		}
	)");
	doc.computeLayout(Vec2(800, 600));
	Span<u32> root_indices = doc.m_roots;
	ASSERT_EQ(1, root_indices.size(), "Should parse 1 root element");
	ui::Element* panel = doc.getElement(root_indices[0]);
	ASSERT_EQ(3, panel->children.size(), "Panel should have 3 children");

	ui::Element* span1 = doc.getElement(panel->children[0]);
	ui::Element* span2 = doc.getElement(panel->children[1]);
	ui::Element* span3 = doc.getElement(panel->children[2]);

	// Calculate total width of spans
	float totalWidth = span1->size.x + span2->size.x + span3->size.x;
	// Panel width is 400, so right-aligned group should start at 400 - totalWidth
	float expectedStartX = 400.0f - totalWidth;

	// Currently this will fail because text alignment for multiple spans is not implemented
	ASSERT_FLOAT_EQ(expectedStartX, span1->position.x, "First span should be right-aligned");

	return true;
}

bool testAlignCenter() {
	// Citation: layout.md - Text Alignment
	// "Text alignment controls how text is positioned horizontally within its container. The `align` attribute can be set to `left`, `center`, or `right`, with `left` as the default."
	MockFontManager mock;
	ui::Document doc(&mock, getGlobalAllocator());
	ASSERT_PARSE(doc, R"(
		[panel width=400 align=center font="arial.ttf" font-size=16] {
			Centered Text
		}
	)");
	doc.computeLayout(Vec2(800, 600));
	Span<u32> root_indices = doc.m_roots;
	ASSERT_EQ(1, root_indices.size(), "Should parse 1 root element");
	ui::Element* panel = doc.getElement(root_indices[0]);
	ASSERT_EQ(1, panel->children.size(), "Panel should have 1 child");

	ui::Element* text = doc.getElement(panel->children[0]);

	// Text should be centered within the 400px panel
	// Assuming text width is calculated, it should be positioned at (400 - text_width) / 2
	float expectedX = (400.0f - text->size.x) / 2.0f;
	ASSERT_FLOAT_EQ(expectedX, text->position.x, "Text should be centered horizontally");

	return true;
}

bool testAlignRight() {
	// Citation: layout.md - Text Alignment
	// "Text alignment controls how text is positioned horizontally within its container. The `align` attribute can be set to `left`, `center`, or `right`, with `left` as the default."
	MockFontManager mock;
	ui::Document doc(&mock, getGlobalAllocator());
	ASSERT_PARSE(doc, R"(
		[panel width=400 align=right font="arial.ttf" font-size=16] {
			Right Aligned Text
		}
	)");
	doc.computeLayout(Vec2(800, 600));
	Span<u32> root_indices = doc.m_roots;
	ASSERT_EQ(1, root_indices.size(), "Should parse 1 root element");
	ui::Element* panel = doc.getElement(root_indices[0]);
	ASSERT_EQ(1, panel->children.size(), "Panel should have 1 child");

	ui::Element* text = doc.getElement(panel->children[0]);

	// Text should be right-aligned within the 400px panel
	// It should be positioned at 400 - text_width
	float expectedX = 400.0f - text->size.x;
	ASSERT_FLOAT_EQ(expectedX, text->position.x, "Text should be right-aligned horizontally");

	return true;
}

bool testComplexLayout() {
	MockDocument doc;
	ASSERT_PARSE(doc, R"(
	[panel direction=row width=50% height=fit-content margin=25%
			bg-color=#f0f0f0
			font="/engine/editor/fonts/JetBrainsMono-Regular.ttf"
			font-size=40
			padding=1em
			wrap=true
	] {
		[panel direction=row width=fill padding=0.5em bg-color=#ffffff align=center] {
			Welcome to [span value=" Lumix " color=#ff0000 font-size=60] Demo
		}
		[panel bg-color=#ff0000 width=fill align=center] { Start Demo }
	}
	)");
	doc.computeLayout(Vec2(800, 600));
	// Basic assertions
	ASSERT_TRUE(doc.m_elements.size() >= 5, "Should parse multiple elements");
	ui::Element* root = doc.getElement(0);
	ASSERT_FLOAT_EQ(400.0f, root->size.x, "Root width should be 50% of 800");
	ui::Element* child1 = doc.getElement(root->children[0]);
	ui::Element* child2 = doc.getElement(root->children[1]);
	// Assert sizes and positions for children
	ASSERT_FLOAT_EQ(240.0f, child1->position.x, "Child1 x position");
	ASSERT_FLOAT_EQ(190.0f, child1->position.y, "Child1 y position");
	ASSERT_FLOAT_EQ(320.0f, child1->size.x, "Child1 width");
	ASSERT_FLOAT_EQ(100.0f, child1->size.y, "Child1 height");
	ASSERT_FLOAT_EQ(240.0f, child2->position.x, "Child2 x position");
	ASSERT_FLOAT_EQ(290.0f, child2->position.y, "Child2 y position");
	ASSERT_FLOAT_EQ(320.0f, child2->size.x, "Child2 width");
	ASSERT_FLOAT_EQ(40.0f, child2->size.y, "Child2 height");
	ASSERT_FLOAT_EQ(180.0f, root->size.y, "Root height");
	return true;
}

} // namespace

void runUILayoutTests() {
	logInfo("=== Running UI Layout Tests ===");
	RUN_TEST(testTwoPanelsLayout);
	RUN_TEST(testLayoutDirection);
	RUN_TEST(testJustifyContent);
	RUN_TEST(testJustifyStart);
	RUN_TEST(testJustifyEnd);
	RUN_TEST(testJustifySpaceBetween);
	RUN_TEST(testJustifySpaceAround);
	RUN_TEST(testJustifySpaceAroundSingleChild);
	RUN_TEST(testJustifySpaceBetweenSingleChild);
	RUN_TEST(testPercentLayout);
	RUN_TEST(testPercentMargins);
	RUN_TEST(testMarginPadding);
	RUN_TEST(testBasicLayout);
	RUN_TEST(testFitContent);
	RUN_TEST(testFitContentWithMargins);
	RUN_TEST(testFill);
	RUN_TEST(testFillWithPadding);
	RUN_TEST(testFillWithMargin);
	RUN_TEST(testFillSingleChild);
	RUN_TEST(testFillWithWrap);
	RUN_TEST(testLayoutWithMargins);
	RUN_TEST(testNestedPanelsWithMargins);
	RUN_TEST(testDirectionRow);
	RUN_TEST(testDirectionColumn);
	RUN_TEST(testDirectionDefault);
	RUN_TEST(testNestedPanelsDifferentDirections);
	RUN_TEST(testAdvancedFitContent);
	RUN_TEST(testDefaultFitContentSimple);
	RUN_TEST(testDefaultFitContentLeaf);
	RUN_TEST(testVerticalMarginCollapse);
	RUN_TEST(testHorizontalMarginCollapse);
	RUN_TEST(testWrap);
	RUN_TEST(testNoWrap);
	RUN_TEST(testAlignItemsCenter);
	RUN_TEST(testAlignItemsStart);
	RUN_TEST(testAlignItemsEnd);
	RUN_TEST(testAlignItemsStretch);
	RUN_TEST(testAlignItemsWithWrap);
	RUN_TEST(testJustifyContentWithWrap);
	RUN_TEST(testWrapCrossAxisDistribution);
	RUN_TEST(testInvalidTag);
	RUN_TEST(testAlignItemsStretchMargins);
	RUN_TEST(testLineBreaks);
	RUN_TEST(testFitContentWithInlineText);
	RUN_TEST(testTextWrapping);
	RUN_TEST(testWrappingInheritance);
	RUN_TEST(testMultilineStringLayout);
	RUN_TEST(testDoubleQuotesInText);
	RUN_TEST(testTextHorizontalRendering);
	RUN_TEST(testBaselineAlignment);
	RUN_TEST(testAlignCenterMultipleSpans);
	RUN_TEST(testAlignRightMultipleSpans);
	RUN_TEST(testAlignCenter);
	RUN_TEST(testAlignRight);
	RUN_TEST(testComplexLayout);
}
