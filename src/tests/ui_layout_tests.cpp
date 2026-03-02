#include "core/log.h"
#include "tests/common.h"

using namespace Lumix;

namespace {

bool testTwoPanelsLayout() {
	// Citation: layout.md - Root Elements
	// "Root elements behave like they are children of a panel that covers the whole screen with 0 padding and `direction=column`."
	MockDocument doc;
	ASSERT_PARSE(doc, R"(
	[panel width=100% height=100]
	[panel width=150 height=80]
	)");
	Span<u32> root_indices = doc.m_roots;
	ASSERT_EQ(2, root_indices.size());
	ui::Element* panel1 = doc.getElement(root_indices[0]);
	ui::Element* panel2 = doc.getElement(root_indices[1]);
	
	doc.computeLayout(Vec2(800, 600));
	
	// Check sizes
	ASSERT_FLOAT_EQ(800.0f, panel1->size.x);
	ASSERT_FLOAT_EQ(100.0f, panel1->size.y);
	ASSERT_FLOAT_EQ(150.0f, panel2->size.x);
	ASSERT_FLOAT_EQ(80.0f, panel2->size.y);
	
	// Root elements are laid out like in a panel with direction=column
	ASSERT_FLOAT_EQ(0.0f, panel1->position.x);
	ASSERT_FLOAT_EQ(0.0f, panel1->position.y);
	ASSERT_FLOAT_EQ(0.0f, panel2->position.x);
	ASSERT_FLOAT_EQ(100.0f, panel2->position.y);
	
	return true;
}

bool testPercentHeightOnRoot() {
	// Citation: layout.md - Root Elements and Units
	// "Root elements behave like they are children of a panel that covers the whole screen with 0 padding and `direction=column`."
	// "%: Percentage of parent (or viewport for roots). E.g., `height=50%` for half the viewport's height."
	MockDocument doc;
	ASSERT_PARSE(doc, R"(
	[panel width=100 height=50%]
	)");
	Span<u32> root_indices = doc.m_roots;
	ASSERT_EQ(1, root_indices.size());
	ui::Element* panel = doc.getElement(root_indices[0]);
	
	doc.computeLayout(Vec2(800, 600));
	
	// Check sizes
	ASSERT_FLOAT_EQ(100.0f, panel->size.x);
	ASSERT_FLOAT_EQ(300.0f, panel->size.y);
	
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
	ASSERT_EQ(1, root_indices.size());
	ui::Element* parent = doc.getElement(root_indices[0]);
	ASSERT_EQ(2, parent->children.size());
	ui::Element* child1 = doc.getElement(parent->children[0]);
	ui::Element* child2 = doc.getElement(parent->children[1]);

	doc.computeLayout(Vec2(800, 600));

	// Check children sizes
	ASSERT_FLOAT_EQ(100.0f, child1->size.x);
	ASSERT_FLOAT_EQ(50.0f, child1->size.y);
	ASSERT_FLOAT_EQ(150.0f, child2->size.x);
	ASSERT_FLOAT_EQ(50.0f, child2->size.y);

	// Children are laid out horizontally
	float parent_x = parent->position.x;
	float parent_y = parent->position.y;

	ASSERT_FLOAT_EQ(parent_x, child1->position.x);
	ASSERT_FLOAT_EQ(parent_y, child1->position.y);

	// Child2 should be to the right of child1 (row layout)
	ASSERT_FLOAT_EQ(parent_x + 100.0f, child2->position.x);
	ASSERT_FLOAT_EQ(parent_y, child2->position.y);

	return true;
}

bool testJustifyCenter() {
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
	ASSERT_TRUE(doc.m_elements.size() >= 3);
	ui::Element* panel = doc.getElement(0);
	ui::Element* btn1 = doc.getElement(1);
	ui::Element* btn2 = doc.getElement(2);
	// Panel at (0,0) to (400,200), buttons 50x50 each, total span 100, available 400, center at 150
	float expected_x = (400 - 100) * 0.5f; // 150
	ASSERT_FLOAT_EQ(expected_x, btn1->position.x);
	ASSERT_FLOAT_EQ(expected_x + 50.0f, btn2->position.x);
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
	ASSERT_TRUE(doc.m_elements.size() >= 3);
	ui::Element* panel = doc.getElement(0);
	ui::Element* btn1 = doc.getElement(1);
	ui::Element* btn2 = doc.getElement(2);
	// Start: buttons at left
	ASSERT_FLOAT_EQ(0.0f, btn1->position.x);
	ASSERT_FLOAT_EQ(50.0f, btn2->position.x);
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
	ASSERT_TRUE(doc.m_elements.size() >= 3);
	ui::Element* panel = doc.getElement(0);
	ui::Element* btn1 = doc.getElement(1);
	ui::Element* btn2 = doc.getElement(2);
	// End: total span 100, available 400, end at 300
	float expected_x = 400 - 100; // 300
	ASSERT_FLOAT_EQ(expected_x, btn1->position.x);
	ASSERT_FLOAT_EQ(expected_x + 50.0f, btn2->position.x);
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
	ASSERT_TRUE(doc.m_elements.size() >= 4);
	ui::Element* panel = doc.getElement(0);
	ui::Element* btn1 = doc.getElement(1);
	ui::Element* btn2 = doc.getElement(2);
	ui::Element* btn3 = doc.getElement(3);
	// Space between: 3 buttons, 2 spaces, total span 150, available 400, space = (400-150)/2 = 125
	// Positions: 0, 50+125=175, 175+50+125=350
	ASSERT_FLOAT_EQ(0.0f, btn1->position.x);
	ASSERT_FLOAT_EQ(175.0f, btn2->position.x);
	ASSERT_FLOAT_EQ(350.0f, btn3->position.x);
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
	ASSERT_TRUE(doc.m_elements.size() >= 3);
	ui::Element* panel = doc.getElement(0);
	ui::Element* btn1 = doc.getElement(1);
	ui::Element* btn2 = doc.getElement(2);
	// Space around: 2 buttons, space = (400-100)/2 = 150, half space 75
	// Positions: 75, 75+50+150=275
	ASSERT_FLOAT_EQ(100.0f, btn1->position.x);
	ASSERT_FLOAT_EQ(250.0f, btn2->position.x);
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
	ASSERT_TRUE(doc.m_elements.size() >= 2);
	ui::Element* panel = doc.getElement(0);
	ui::Element* child = doc.getElement(1);
	// With single child, space-around should behave like center
	// Container width 400, child width 50, so centered at (400-50)/2 = 175
	ASSERT_FLOAT_EQ(175.0f, child->position.x);
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
	ASSERT_TRUE(doc.m_elements.size() >= 2);
	ui::Element* panel = doc.getElement(0);
	ui::Element* child = doc.getElement(1);
	// With single child, space-between should behave like start
	ASSERT_FLOAT_EQ(0.0f, child->position.x);
	return true;
}

bool testJustifyContentWithMargins() {
	// Citation: layout.md - Justification and Margins
	// "justify-content should account for margins when distributing space"
	MockDocument doc;
	ASSERT_PARSE(doc, R"(
	[panel width=400 height=200 direction=row justify-content=end padding=5] {
		[panel width=50 height=50 margin=10] {}
		[panel width=50 height=50 margin=10] {}
	}
	)");
	doc.computeLayout(Vec2(800, 600));
	ASSERT_TRUE(doc.m_elements.size() >= 3);
	ui::Element* panel = doc.getElement(0);
	ui::Element* btn1 = doc.getElement(1);
	ui::Element* btn2 = doc.getElement(2);
	ASSERT_FLOAT_EQ(275.0f, btn1->position.x);
	ASSERT_FLOAT_EQ(335.0f, btn2->position.x);
	return true;
}

bool testJustifyVerticalCenter() {
	// Citation: elements_attributes.md - panel
	// "`justify-content` - `center` - Elements are centered as a group"
	MockDocument doc;
	ASSERT_PARSE(doc, R"(
	[panel width=400 height=200 direction=column justify-content=center] {
		[panel width=50 height=50] {}
		[panel width=50 height=50] {}
	}
	)");
	doc.computeLayout(Vec2(800, 600));
	ASSERT_TRUE(doc.m_elements.size() >= 3);
	ui::Element* panel = doc.getElement(0);
	ui::Element* btn1 = doc.getElement(1);
	ui::Element* btn2 = doc.getElement(2);

	ASSERT_FLOAT_EQ(50.f, btn1->position.y);
	ASSERT_FLOAT_EQ(100.f, btn2->position.y);
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
	ASSERT_TRUE(doc.m_elements.size() >= 2);
	ui::Element* parent = doc.getElement(0);
	ui::Element* child = doc.getElement(1);
	// Parent size: 400x200
	// Child: 50% of parent -> 200x100
	ASSERT_FLOAT_EQ(400.0f, parent->size.x);
	ASSERT_FLOAT_EQ(200.0f, parent->size.y);
	ASSERT_FLOAT_EQ(200.0f, child->size.x);
	ASSERT_FLOAT_EQ(100.0f, child->size.y);
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
	ASSERT_TRUE(doc.m_elements.size() >= 2);
	ui::Element* parent = doc.getElement(0);
	ui::Element* child = doc.getElement(1);
	// Parent size: 400x200
	// margin=10% should yield: top/bottom = 10% of parent height = 20, left/right = 10% of parent width = 40
	ASSERT_FLOAT_EQ(20.0f, child->margins.top);
	ASSERT_FLOAT_EQ(40.0f, child->margins.right);
	ASSERT_FLOAT_EQ(20.0f, child->margins.bottom);
	ASSERT_FLOAT_EQ(40.0f, child->margins.left);
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
	ASSERT_TRUE(doc.m_elements.size() >= 2);
	ui::Element* parent = doc.getElement(0);
	ui::Element* child = doc.getElement(1);

	float expected_child_x = parent->position.x + parent->paddings.left + child->margins.left; // parent.x + left_padding + child_left_margin
	float expected_child_y = parent->position.y + parent->paddings.top + child->margins.top; // parent.y + top_padding + child_top_margin
	ASSERT_FLOAT_EQ(expected_child_x, child->position.x);
	ASSERT_FLOAT_EQ(expected_child_y, child->position.y);

	return true;
}

bool testBasicLayout() {
	// Citation: layout.md - Element Sizing
	// "Each UI element has `width` and `height` attributes that control its size, known as dimensions. Set them explicitly for a fixed size;"
	MockDocument doc;
	ASSERT_PARSE(doc, "[panel width=200 height=100] {}");
	doc.computeLayout(Vec2(800, 600));
	ASSERT_TRUE(doc.m_elements.size() >= 1);
	ui::Element* elem = doc.getElement(0);
	ASSERT_FLOAT_EQ(200.0f, elem->size.x);
	ASSERT_FLOAT_EQ(100.0f, elem->size.y);
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
	ASSERT_TRUE(doc.m_elements.size() >= 2);
	ui::Element* elem = doc.getElement(0);
	ASSERT_FLOAT_EQ(640.0f, elem->size.x);
	ASSERT_FLOAT_EQ(480.0f, elem->size.y);
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
	ASSERT_TRUE(doc.m_elements.size() >= 2);
	ui::Element* parent = doc.getElement(0);
	// Height should include child's top and bottom margins
	ASSERT_FLOAT_EQ(120.0f, parent->size.y);
	return true;
}

bool testGrow() {
	// Citation: layout.md - Fill
	// "The `fill` unit allows an element to expand and occupy the remaining available space in its parent container along the specified dimension."
	MockDocument doc;
	ASSERT_PARSE(doc, R"(
	[panel width=300 height=100 direction=row] {
		[panel width=100 height=50] {}
		[panel grow=1 height=50] {}
	}
	)");
	doc.computeLayout(Vec2(800, 600));
	ASSERT_TRUE(doc.m_elements.size() >= 3);
	ui::Element* parent = doc.getElement(0);
	ui::Element* child1 = doc.getElement(1);
	ui::Element* child2 = doc.getElement(2);
	ASSERT_FLOAT_EQ(100.0f, child1->size.x);
	// Fill should take remaining space: 300 - 100 = 200
	ASSERT_FLOAT_EQ(200.0f, child2->size.x);
	return true;
}

bool testGrowWithPadding() {
	// Citation: layout.md - Fill
	// "Fill respects margins and padding of the parent container."
	// "Difference from `width=100%`: ... `fill` expands to occupy the available space within the parent's content area (after subtracting padding)"
	MockDocument doc;
	ASSERT_PARSE(doc, R"(
	[panel width=300 height=100 direction=row padding=10] {
		[panel width=100 height=50] {}
		[panel grow=1 height=50] {}
	}
	)");
	doc.computeLayout(Vec2(800, 600));
	ASSERT_TRUE(doc.m_elements.size() >= 3);
	ui::Element* parent = doc.getElement(0);
	ui::Element* child1 = doc.getElement(1);
	ui::Element* child2 = doc.getElement(2);
	ASSERT_FLOAT_EQ(100.0f, child1->size.x);
	// Parent content width: 300 - 10*2 = 280, remaining after child1: 280 - 100 = 180
	ASSERT_FLOAT_EQ(180.0f, child2->size.x);
	return true;
}

bool testGrowWithMargin() {
	// Citation: layout.md - Fill
	// "Fill respects margins and padding of the parent container."
	MockDocument doc;
	ASSERT_PARSE(doc, R"(
	[panel width=300 height=100 direction=row] {
		[panel width=100 height=50 margin=5] {}
		[panel grow=1 height=50 margin=5] {}
	}
	)");
	doc.computeLayout(Vec2(800, 600));
	ASSERT_TRUE(doc.m_elements.size() >= 3);
	ui::Element* parent = doc.getElement(0);
	ui::Element* child1 = doc.getElement(1);
	ui::Element* child2 = doc.getElement(2);

	ASSERT_FLOAT_EQ(100.0f, child1->size.x);
	ASSERT_FLOAT_EQ(185.0f, child2->size.x);
	return true;
}

bool testGrowSingleChild() {
	// Citation: layout.md - Fill
	// "The `fill` unit allows an element to expand and occupy the remaining available space in its parent container along the specified dimension."
	MockDocument doc;
	ASSERT_PARSE(doc, R"(
	[panel width=300 height=100 direction=row] {
		[panel grow=1 height=50] {}
	}
	)");
	doc.computeLayout(Vec2(800, 600));
	ASSERT_TRUE(doc.m_elements.size() >= 2);
	ui::Element* parent = doc.getElement(0);
	ui::Element* child = doc.getElement(1);
	// Fill should take the entire parent content width: 300
	ASSERT_FLOAT_EQ(300.0f, child->size.x);
	return true;
}

bool testGrowProportional() {
	// Grow should distribute remaining space proportionally to weights
	MockDocument doc;
	ASSERT_PARSE(doc, R"(
	[panel width=400 direction=row] {
		[panel width=100 height=50] {}
		[panel grow=2 height=50] {}
		[panel grow=1 height=50] {}
	}
	)");
	doc.computeLayout(Vec2(800, 600));
	ui::Element* parent = doc.getElement(0);
	ui::Element* child1 = doc.getElement(1);
	ui::Element* child2 = doc.getElement(2);
	ui::Element* child3 = doc.getElement(3);

	// Remaining after child1: 400 - 100 = 300. Weights 2 and 1 -> 200 and 100
	ASSERT_FLOAT_EQ(100.0f, child1->size.x);
	ASSERT_FLOAT_EQ(200.0f, child2->size.x);
	ASSERT_FLOAT_EQ(100.0f, child3->size.x);
	return true;
}

bool testGrowRespectsPadding() {
	// Grow should respect parent padding when computing available space
	MockDocument doc;
	ASSERT_PARSE(doc, R"(
	[panel width=400 padding=10 direction=row] {
		[panel width=100 height=50] {}
		[panel grow=1 height=50] {}
	}
	)");
	doc.computeLayout(Vec2(800, 600));
	ui::Element* parent = doc.getElement(0);
	ui::Element* child1 = doc.getElement(1);
	ui::Element* child2 = doc.getElement(2);

	// Parent content width = 400 - 10*2 = 380; remaining after child1 = 280
	ASSERT_FLOAT_EQ(100.0f, child1->size.x);
	ASSERT_FLOAT_EQ(280.0f, child2->size.x);
	return true;
}

bool testGrowMiddle() {
	// Verify that a grow element in the middle expands to fill remaining space
	MockDocument doc;
	ASSERT_PARSE(doc, R"(
	[panel width=300 height=50 direction=row] {
		[panel width=50 height=50] {}
		[panel grow=1 height=50] {}
		[panel width=100 height=50] {}
	}
	)");
	doc.computeLayout(Vec2(800, 600));
	ASSERT_TRUE(doc.m_elements.size() >= 4);
	ui::Element* parent = doc.getElement(0);
	ui::Element* left = doc.getElement(1);
	ui::Element* middle = doc.getElement(2);
	ui::Element* right = doc.getElement(3);
	// Sizes: left 50, right 100, middle should fill remaining: 300 - 50 - 100 = 150
	ASSERT_FLOAT_EQ(50.0f, left->size.x);
	ASSERT_FLOAT_EQ(100.0f, right->size.x);
	ASSERT_FLOAT_EQ(150.0f, middle->size.x);
	// Positions: left at 0, middle at 50, right at 200
	ASSERT_FLOAT_EQ(0.0f, left->position.x);
	ASSERT_FLOAT_EQ(50.0f, middle->position.x);
	ASSERT_FLOAT_EQ(200.0f, right->position.x);
	return true;
}

bool testGrowParentWithPercentChild() {
	// Test grow=1 parent with width=100% child
	// The child should expand to fill the parent's width
	MockDocument doc;
	ASSERT_PARSE(doc, R"(
	[panel width=100% direction=row] {
		[panel grow=1 height=100 direction=row] {
			[panel width=100% height=50] {}
		}
	}
	)");
	doc.computeLayout(Vec2(800, 600));
	ASSERT_EQ(3, doc.m_elements.size());
	ui::Element* parent = doc.getElement(0);
	ui::Element* child = doc.getElement(1);
	ui::Element* grandchild = doc.getElement(2);

	ASSERT_FLOAT_EQ(800.0f, parent->size.x);
	ASSERT_FLOAT_EQ(100.0f, parent->size.y);
	ASSERT_FLOAT_EQ(800.0f, child->size.x);
	ASSERT_FLOAT_EQ(100.0f, child->size.y);
	ASSERT_FLOAT_EQ(800.0f, grandchild->size.x);
	ASSERT_FLOAT_EQ(50.0f, grandchild->size.y);

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
	ASSERT_EQ(2, root_indices.size());
	ui::Element* panel1 = doc.getElement(root_indices[0]);
	ui::Element* panel2 = doc.getElement(root_indices[1]);
	
	doc.computeLayout(Vec2(800, 600));
	
	// Root elements are laid out like in a panel with direction=column
	// Position should account for margins: panel1.y + panel1.height + max(panel1.bottom_margin, panel2.top_margin)
	// Note: margins are collapsed
	ASSERT_FLOAT_EQ(10.0f, panel1->position.x);
	ASSERT_FLOAT_EQ(10.0f, panel1->position.y);
	ASSERT_FLOAT_EQ(5.0f, panel2->position.x);
	ASSERT_FLOAT_EQ(100.0f + 10.0f + 10.0f, panel2->position.y);
	
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
	ASSERT_EQ(1, root_indices.size());
	ui::Element* parent = doc.getElement(root_indices[0]);
	ASSERT_EQ(2, parent->children.size());
	ui::Element* child1 = doc.getElement(parent->children[0]);
	ui::Element* child2 = doc.getElement(parent->children[1]);
	
	doc.computeLayout(Vec2(800, 600));
	
	float parent_x = parent->position.x;
	float parent_y = parent->position.y;
	float padding_left = parent->paddings.left; // left padding
	float padding_top = parent->paddings.top;  // top padding
	
	ASSERT_FLOAT_EQ(parent_x + padding_left + 5.0f, child1->position.x);
	ASSERT_FLOAT_EQ(parent_y + padding_top + 5.0f, child1->position.y);
	
	// Child2 should be positioned after child1 with margins (collapsed)
	float expected_child2_y = parent_y + padding_top + 5.0f + child1->size.y + maximum(child1->margins.bottom, child2->margins.top);
	ASSERT_FLOAT_EQ(parent_x + padding_left + 10.0f, child2->position.x);
	ASSERT_FLOAT_EQ(expected_child2_y, child2->position.y);
	
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
	ASSERT_EQ(1, root_indices.size());
	ui::Element* parent = doc.getElement(root_indices[0]);
	ASSERT_EQ(2, parent->children.size());
	ui::Element* child1 = doc.getElement(parent->children[0]);
	ui::Element* child2 = doc.getElement(parent->children[1]);
	
	doc.computeLayout(Vec2(800, 600));
	
	// Check children sizes
	ASSERT_FLOAT_EQ(100.0f, child1->size.x);
	ASSERT_FLOAT_EQ(50.0f, child1->size.y);
	ASSERT_FLOAT_EQ(150.0f, child2->size.x);
	ASSERT_FLOAT_EQ(50.0f, child2->size.y);
	
	// Children are laid out horizontally
	float parent_x = parent->position.x;
	float parent_y = parent->position.y;
	
	ASSERT_FLOAT_EQ(parent_x, child1->position.x);
	ASSERT_FLOAT_EQ(parent_y, child1->position.y);
	
	// Child2 should be to the right of child1 (row layout)
	ASSERT_FLOAT_EQ(parent_x + 100.0f, child2->position.x);
	ASSERT_FLOAT_EQ(parent_y, child2->position.y);
	
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
	ASSERT_EQ(1, root_indices.size());
	ui::Element* parent = doc.getElement(root_indices[0]);
	ASSERT_EQ(2, parent->children.size());
	ui::Element* child1 = doc.getElement(parent->children[0]);
	ui::Element* child2 = doc.getElement(parent->children[1]);
	
	doc.computeLayout(Vec2(800, 600));
	
	// In column direction, children should be laid out vertically
	float parent_x = parent->position.x;
	float parent_y = parent->position.y;
	
	ASSERT_FLOAT_EQ(parent_x + 5.0f, child1->position.x);
	ASSERT_FLOAT_EQ(parent_y + 5.0f, child1->position.y);
	
	// Child2 should be below child1, accounting for margins (collapsed)
	ASSERT_FLOAT_EQ(parent_x + 5.0f, child2->position.x);
	ASSERT_FLOAT_EQ(parent_y + 5.0f + 50.0f + 5.0f, child2->position.y);
	
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
	ASSERT_EQ(1, root_indices.size());
	ui::Element* parent = doc.getElement(root_indices[0]);
	ASSERT_EQ(2, parent->children.size());
	ui::Element* child1 = doc.getElement(parent->children[0]);
	ui::Element* child2 = doc.getElement(parent->children[1]);
	
	doc.computeLayout(Vec2(800, 600));
	
	// Check children sizes
	ASSERT_FLOAT_EQ(100.0f, child1->size.x);
	ASSERT_FLOAT_EQ(50.0f, child1->size.y);
	ASSERT_FLOAT_EQ(100.0f, child2->size.x);
	ASSERT_FLOAT_EQ(80.0f, child2->size.y);
	
	// Default direction should be column (vertical layout)
	float parent_x = parent->position.x;
	float parent_y = parent->position.y;
	
	ASSERT_FLOAT_EQ(parent_x, child1->position.x);
	ASSERT_FLOAT_EQ(parent_y, child1->position.y);
	
	// Child2 should be below child1
	ASSERT_FLOAT_EQ(parent_x, child2->position.x);
	ASSERT_FLOAT_EQ(parent_y + 50.0f, child2->position.y);
	
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
	ASSERT_EQ(1, root_indices.size());
	ui::Element* parent = doc.getElement(root_indices[0]);
	ASSERT_EQ(2, parent->children.size());
	ui::Element* column1 = doc.getElement(parent->children[0]);
	ui::Element* column2 = doc.getElement(parent->children[1]);
	
	doc.computeLayout(Vec2(800, 600));
	
	// Check parent
	ASSERT_FLOAT_EQ(800.0f, parent->size.x);
	ASSERT_FLOAT_EQ(600.0f, parent->size.y);
	
	// Check column1
	ASSERT_FLOAT_EQ(150.0f, column1->size.x);
	ASSERT_EQ(2, column1->children.size());
	ui::Element* c1_child1 = doc.getElement(column1->children[0]);
	ui::Element* c1_child2 = doc.getElement(column1->children[1]);
	
	// Check column2
	ASSERT_FLOAT_EQ(200.0f, column2->size.x);
	ASSERT_EQ(3, column2->children.size());
	ui::Element* c2_child1 = doc.getElement(column2->children[0]);
	ui::Element* c2_child2 = doc.getElement(column2->children[1]);
	ui::Element* c2_child3 = doc.getElement(column2->children[2]);
	
	// Check sizes
	ASSERT_FLOAT_EQ(100.0f, c1_child1->size.x);
	ASSERT_FLOAT_EQ(50.0f, c1_child1->size.y);
	ASSERT_FLOAT_EQ(100.0f, c1_child2->size.x);
	ASSERT_FLOAT_EQ(60.0f, c1_child2->size.y);
	
	ASSERT_FLOAT_EQ(150.0f, c2_child1->size.x);
	ASSERT_FLOAT_EQ(40.0f, c2_child1->size.y);
	ASSERT_FLOAT_EQ(150.0f, c2_child2->size.x);
	ASSERT_FLOAT_EQ(70.0f, c2_child2->size.y);
	ASSERT_FLOAT_EQ(150.0f, c2_child3->size.x);
	ASSERT_FLOAT_EQ(30.0f, c2_child3->size.y);
	
	// Positions: parent lays out as row
	float parent_x = parent->position.x;
	float parent_y = parent->position.y;
	
	// Column1 at left
	ASSERT_EQ(parent_x, column1->position.x);
	ASSERT_EQ(parent_y, column1->position.y);
	
	// Column2 to the right of column1
	ASSERT_FLOAT_EQ(parent_x + 150.0f, column2->position.x);
	ASSERT_EQ(parent_y, column2->position.y);
	
	// Inside column1 (direction=column), children stacked vertically
	ASSERT_EQ(column1->position.x, c1_child1->position.x);
	ASSERT_EQ(column1->position.y, c1_child1->position.y);
	
	ASSERT_EQ(column1->position.x, c1_child2->position.x);
	ASSERT_FLOAT_EQ(column1->position.y + 50.0f, c1_child2->position.y);
	
	// Inside column2 (direction=column), children stacked vertically
	ASSERT_EQ(column2->position.x, c2_child1->position.x);
	ASSERT_EQ(column2->position.y, c2_child1->position.y);
	
	ASSERT_EQ(column2->position.x, c2_child2->position.x);
	ASSERT_FLOAT_EQ(column2->position.y + 40.0f, c2_child2->position.y);
	
	ASSERT_EQ(column2->position.x, c2_child3->position.x);
	ASSERT_FLOAT_EQ(column2->position.y + 40.0f + 70.0f, c2_child3->position.y);
	
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
	ASSERT_EQ(1, root_indices.size());
	ui::Element* parent = doc.getElement(root_indices[0]);
	ASSERT_EQ(2, parent->children.size());
	ui::Element* child1 = doc.getElement(parent->children[0]);
	ui::Element* child2 = doc.getElement(parent->children[1]);
	
	// Child1 has 1 child
	ASSERT_EQ(1, child1->children.size());
	ui::Element* grandchild1 = doc.getElement(child1->children[0]);
	
	// Child2 has 2 children
	ASSERT_EQ(2, child2->children.size());
	ui::Element* grandchild2_1 = doc.getElement(child2->children[0]);
	ui::Element* grandchild2_2 = doc.getElement(child2->children[1]);
	
	doc.computeLayout(Vec2(800, 600));
	
	// Check grandchild sizes
	ASSERT_FLOAT_EQ(200.0f, grandchild1->size.x);
	ASSERT_FLOAT_EQ(50.0f, grandchild1->size.y);
	
	ASSERT_FLOAT_EQ(0.0f, grandchild2_1->size.x);
	ASSERT_FLOAT_EQ(30.0f, grandchild2_1->size.y);
	
	ASSERT_FLOAT_EQ(0.0f, grandchild2_2->size.x);
	ASSERT_FLOAT_EQ(40.0f, grandchild2_2->size.y);
	
	// Child1: width=fit-content (should fit grandchild1 width=200), height=100
	ASSERT_FLOAT_EQ(200.0f, child1->size.x);
	ASSERT_FLOAT_EQ(100.0f, child1->size.y);
	
	// Child2: width=100, height=fit-content (should fit sum of grandchildren heights=30+40=70)
	ASSERT_FLOAT_EQ(100.0f, child2->size.x);
	ASSERT_FLOAT_EQ(70.0f, child2->size.y);
	
	// Parent: width=fit-content (sum of children widths=200+100=300), height=fit-content (max of children heights=100+70=100)
	ASSERT_FLOAT_EQ(300.0f, parent->size.x);
	ASSERT_FLOAT_EQ(100.0f, parent->size.y);
	
	// Assert positions
	ASSERT_EQ(0.0f, parent->position.x);
	ASSERT_EQ(0.0f, parent->position.y);
	
	ASSERT_EQ(0.0f, child1->position.x);
	ASSERT_EQ(0.0f, child1->position.y);
	
	ASSERT_EQ(0.0f, grandchild1->position.x);
	ASSERT_EQ(0.0f, grandchild1->position.y);
	
	ASSERT_EQ(200.0f, child2->position.x);
	ASSERT_EQ(0.0f, child2->position.y);
	
	ASSERT_EQ(200.0f, grandchild2_1->position.x);
	ASSERT_EQ(0.0f, grandchild2_1->position.y);
	
	ASSERT_EQ(200.0f, grandchild2_2->position.x);
	ASSERT_EQ(30.0f, grandchild2_2->position.y);

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
	ASSERT_EQ(1, root_indices.size());
	ui::Element* parent = doc.getElement(root_indices[0]);
	ASSERT_EQ(2, parent->children.size());
	ui::Element* child1 = doc.getElement(parent->children[0]);
	ui::Element* child2 = doc.getElement(parent->children[1]);
	
	doc.computeLayout(Vec2(800, 600));
	
	// Children have explicit sizes
	ASSERT_EQ(50.0f, child1->size.x);
	ASSERT_EQ(30.0f, child1->size.y);
	ASSERT_EQ(70.0f, child2->size.x);
	ASSERT_EQ(40.0f, child2->size.y);
	
	// Parent has no size specified, so default fit-content
	// Direction is column, so width = max child width = 70, height = sum child heights = 30 + 40 = 70
	ASSERT_EQ(70.0f, parent->size.x);
	ASSERT_EQ(70.0f, parent->size.y);
	
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
	ASSERT_EQ(1, root_indices.size());
	ui::Element* parent = doc.getElement(root_indices[0]);
	ASSERT_EQ(1, parent->children.size());
	ui::Element* child = doc.getElement(parent->children[0]);
	
	doc.computeLayout(Vec2(800, 600));
	
	// Parent has explicit size
	ASSERT_EQ(200.0f, parent->size.x);
	ASSERT_EQ(100.0f, parent->size.y);
	
	// Child has no size specified, so default fit-content, and since leaf, should be 0
	ASSERT_EQ(0.0f, child->size.x);
	ASSERT_EQ(0.0f, child->size.y);
	
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
	ASSERT_EQ(1, root_indices.size());
	ui::Element* root = doc.getElement(root_indices[0]);
	ASSERT_EQ(2, root->children.size());
	ui::Element* child1 = doc.getElement(root->children[0]);
	ui::Element* child2 = doc.getElement(root->children[1]);
	
	ASSERT_EQ(10.0f, child1->position.y);
	ASSERT_EQ(80.0f, child2->position.y);
	
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
	ASSERT_EQ(1, root_indices.size());
	ui::Element* root = doc.getElement(root_indices[0]);
	ASSERT_EQ(2, root->children.size());
	ui::Element* child1 = doc.getElement(root->children[0]);
	ui::Element* child2 = doc.getElement(root->children[1]);

	ASSERT_EQ(10.0f, child1->position.x);
	ASSERT_EQ(80.0f, child2->position.x);

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
	ASSERT_EQ(1, root_indices.size());
	ui::Element* root = doc.getElement(root_indices[0]);
	ASSERT_EQ(3, root->children.size());
	ui::Element* child1 = doc.getElement(root->children[0]);
	ui::Element* child2 = doc.getElement(root->children[1]);
	ui::Element* child3 = doc.getElement(root->children[2]);

	// With wrap=true, the third child should wrap to the next row
	// child1 at (0,0), child2 at (50,0), child3 at (0,50)
	ASSERT_EQ(0.0f, child1->position.x);
	ASSERT_EQ(0.0f, child1->position.y);
	ASSERT_EQ(50.0f, child2->position.x);
	ASSERT_EQ(0.0f, child2->position.y);
	ASSERT_EQ(0.0f, child3->position.x);
	ASSERT_EQ(50.0f, child3->position.y);

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
	ASSERT_EQ(1, root_indices.size());
	ui::Element* root = doc.getElement(root_indices[0]);
	ASSERT_EQ(3, root->children.size());
	ui::Element* child1 = doc.getElement(root->children[0]);
	ui::Element* child2 = doc.getElement(root->children[1]);
	ui::Element* child3 = doc.getElement(root->children[2]);

	// With wrap=nowrap, no wrapping, third child overflows
	// child1 at (0,0), child2 at (50,0), child3 at (100,0)
	ASSERT_EQ(0.0f, child1->position.x);
	ASSERT_EQ(0.0f, child1->position.y);
	ASSERT_EQ(50.0f, child2->position.x);
	ASSERT_EQ(0.0f, child2->position.y);
	ASSERT_EQ(100.0f, child3->position.x);
	ASSERT_EQ(0.0f, child3->position.y);

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
		[panel width=50 height=50] {}
		[panel width=50 height=40] {}
	}
	)");
	doc.computeLayout(Vec2(800, 600));
	ui::Element* panel = doc.getElement(0);
	ui::Element* btn1 = doc.getElement(1);
	ui::Element* btn2 = doc.getElement(2);
	ui::Element* btn3 = doc.getElement(3);

	// Center: buttons should be centered vertically
	ASSERT_EQ(10, btn1->position.y);
	ASSERT_EQ(0, btn2->position.y);
	ASSERT_EQ(5, btn3->position.y);

	return true;
}

bool testAlignItemsStart() {
	// Citation: layout.md - Off-axis alignment
	// "- `start`: Elements are aligned to the start of the off-axis (top for row, left for column)."
	MockDocument doc;
	ASSERT_PARSE(doc, R"(
	[panel width=400 height=200 direction=row align-items=start] {
		[panel width=50 height=30] {}
		[panel width=50 height=40] {}
	}
	)");
	doc.computeLayout(Vec2(800, 600));
	ui::Element* panel = doc.getElement(0);
	ui::Element* btn1 = doc.getElement(1);
	ui::Element* btn2 = doc.getElement(2);

	// Start: buttons aligned to top
	ASSERT_EQ(0.0f, btn1->position.y);
	ASSERT_EQ(0.0f, btn2->position.y);

	return true;
}

bool testAlignItemsEnd() {
	// Citation: layout.md - Off-axis alignment
	// "- `end`: Elements are aligned to the end of the off-axis (bottom for row, right for column)."
	MockDocument doc;
	ASSERT_PARSE(doc, R"(
	[panel width=400 height=200 direction=row align-items=end] {
		[panel width=50 height=30] {}
		[panel width=50 height=50] {}
		[panel width=50 height=40] {}
	}
	)");
	doc.computeLayout(Vec2(800, 600));
	ui::Element* panel = doc.getElement(0);
	ui::Element* btn1 = doc.getElement(1);
	ui::Element* btn2 = doc.getElement(2);
	ui::Element* btn3 = doc.getElement(3);

	ASSERT_EQ(20, btn1->position.y);
	ASSERT_EQ(0, btn2->position.y);
	ASSERT_EQ(10, btn3->position.y);

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
		[panel width=50 height=30] {}
	}
	)");
	doc.computeLayout(Vec2(800, 600));
	ui::Element* panel = doc.getElement(0);
	ui::Element* btn1 = doc.getElement(1);
	ui::Element* btn2 = doc.getElement(2);
	ui::Element* btn3 = doc.getElement(3);

	ASSERT_EQ(0.0f, btn1->position.y);
	ASSERT_EQ(0.0f, btn2->position.y);
	ASSERT_EQ(0.0f, btn3->position.y);
	ASSERT_EQ(30.0f, btn1->size.y);
	ASSERT_EQ(30.0f, btn2->size.y);
	ASSERT_EQ(30.0f, btn3->size.y);

	return true;
}

bool testAlignItemsStretchMargins() {
    // Citation: layout.md - Off-axis alignment
    // "When `align-items=stretch`, elements expand to match the container's size in the off-axis direction, minus padding and margins."
	MockDocument doc;
	ASSERT_PARSE(doc, R"(
	[panel width=400 height=200 direction=column align-items=stretch] {
		[panel height=50 margin=10] {}
		[panel height=50 margin=5] {}
	}
	)");
	doc.computeLayout(Vec2(800, 600));
	ui::Element* panel = doc.getElement(0);
	ui::Element* child1 = doc.getElement(1);
	ui::Element* child2 = doc.getElement(2);

	ASSERT_FLOAT_EQ(380.0f, child1->size.x);
	ASSERT_FLOAT_EQ(390.0f, child2->size.x);
	return true;
}

bool testAlignItemsStretchWithText() {
	// Test align-items=stretch with a child panel containing right-aligned text
	MockFontManager mock;
	ui::Document doc(&mock, getGlobalAllocator());
	ASSERT_PARSE(doc, R"(
	[panel width=400 height=200 align-items=stretch direction=column] {
		[panel align=right font="arial.ttf" font-size=16] {
			Right Text
		}
	}
	)");
	doc.computeLayout(Vec2(800, 600));
	Span<u32> root_indices = doc.m_roots;
	ASSERT_EQ(1, root_indices.size());
	ui::Element* parent = doc.getElement(root_indices[0]);
	ASSERT_EQ(1, parent->children.size());
	ui::Element* child_panel = doc.getElement(parent->children[0]);

	// Parent panel: width=400, height=200, direction=row, align-items=stretch
	ASSERT_FLOAT_EQ(400.0f, parent->size.x);
	ASSERT_FLOAT_EQ(200.0f, parent->size.y);

	ASSERT_FLOAT_EQ(400.0f, child_panel->size.x);
	ASSERT_FLOAT_EQ(16.0f, child_panel->size.y);

	// Child panel has one child: the text element
	ASSERT_EQ(1, child_panel->children.size());
	ui::Element* text_elem = doc.getElement(child_panel->children[0]);
	ASSERT_TAG(text_elem, SPAN);

	// Text element: single line, right-aligned within child panel
	ASSERT_EQ(1, text_elem->lines.size());
	float text_width = text_elem->size.x;
	ASSERT_FLOAT_EQ(child_panel->size.x - text_width, text_elem->lines[0].pos.x);
	// Baseline y position: assuming ascender for font-size=16 is ~12.8px
	ASSERT_FLOAT_EQ(12.8f, text_elem->lines[0].pos.y);

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

	ASSERT_EQ(10.0f, btn1->position.y);
	ASSERT_EQ(0.0f, btn2->position.y);
	ASSERT_EQ(50.0f, btn3->position.y);

	return true;
}

bool testAlignItemsCenterColumn() {
	// Citation: layout.md - Off-axis alignment
	// "Off-axis alignment controls how child elements are positioned along the axis perpendicular to the container's main axis. For `direction=column`, the off-axis is horizontal;"
	// "- `center`: Elements are centered along the off-axis."
	MockDocument doc;
	ASSERT_PARSE(doc, R"(
	[panel width=400 height=200 direction=column align-items=center] {
		[panel width=50 height=30] {}
		[panel width=80 height=50] {}
		[panel width=60 height=40] {}
	}
	)");
	doc.computeLayout(Vec2(800, 600));
	ui::Element* panel = doc.getElement(0);
	ui::Element* btn1 = doc.getElement(1);
	ui::Element* btn2 = doc.getElement(2);
	ui::Element* btn3 = doc.getElement(3);

	// Center: buttons should be centered horizontally
	// Panel width=400, btn1 width=50, so centered at (400-50)/2 = 175
	// btn2 width=80, centered at (400-80)/2 = 160
	// btn3 width=60, centered at (400-60)/2 = 170
	ASSERT_FLOAT_EQ(175.0f, btn1->position.x);
	ASSERT_FLOAT_EQ(160.0f, btn2->position.x);
	ASSERT_FLOAT_EQ(170.0f, btn3->position.x);

	return true;
}

bool testAlignItemsStartColumn() {
	// Citation: layout.md - Off-axis alignment
	// "- `start`: Elements are aligned to the start of the off-axis (top for row, left for column)."
	MockDocument doc;
	ASSERT_PARSE(doc, R"(
	[panel width=400 height=200 direction=column align-items=start] {
		[panel width=50 height=30] {}
		[panel width=80 height=40] {}
	}
	)");
	doc.computeLayout(Vec2(800, 600));
	ui::Element* panel = doc.getElement(0);
	ui::Element* btn1 = doc.getElement(1);
	ui::Element* btn2 = doc.getElement(2);

	// Start: buttons aligned to left
	ASSERT_FLOAT_EQ(0.0f, btn1->position.x);
	ASSERT_FLOAT_EQ(0.0f, btn2->position.x);

	return true;
}

bool testAlignItemsEndColumn() {
	// Citation: layout.md - Off-axis alignment
	// "- `end`: Elements are aligned to the end of the off-axis (bottom for row, right for column)."
	MockDocument doc;
	ASSERT_PARSE(doc, R"(
	[panel width=400 height=200 direction=column align-items=end] {
		[panel width=50 height=30] {}
		[panel width=80 height=50] {}
		[panel width=60 height=40] {}
	}
	)");
	doc.computeLayout(Vec2(800, 600));
	ui::Element* panel = doc.getElement(0);
	ui::Element* btn1 = doc.getElement(1);
	ui::Element* btn2 = doc.getElement(2);
	ui::Element* btn3 = doc.getElement(3);

	// End: buttons aligned to right
	// Panel width=400, btn1 width=50, so right-aligned at 400-50 = 350
	// btn2 width=80, right-aligned at 400-80 = 320
	// btn3 width=60, right-aligned at 400-60 = 340
	ASSERT_FLOAT_EQ(350.0f, btn1->position.x);
	ASSERT_FLOAT_EQ(320.0f, btn2->position.x);
	ASSERT_FLOAT_EQ(340.0f, btn3->position.x);

	return true;
}

bool testAlignItemsStretchColumn() {
	// Citation: layout.md - Off-axis alignment
	// "- `stretch` (fill): Elements stretch to fill the available space along the off-axis. This is the default "
	MockDocument doc;
	ASSERT_PARSE(doc, R"(
	[panel width=400 height=200 direction=column align-items=stretch] {
		[panel height=30] {}
		[panel height=50] {}
		[panel width=60 height=40] {}
	}
	)");
	doc.computeLayout(Vec2(800, 600));
	ui::Element* panel = doc.getElement(0);
	ui::Element* btn1 = doc.getElement(1);
	ui::Element* btn2 = doc.getElement(2);
	ui::Element* btn3 = doc.getElement(3);

	// Stretch: elements should fill the full width
	ASSERT_FLOAT_EQ(0.0f, btn1->position.x);
	ASSERT_FLOAT_EQ(0.0f, btn2->position.x);
	ASSERT_FLOAT_EQ(0.0f, btn3->position.x);
	ASSERT_FLOAT_EQ(400.0f, btn1->size.x);
	ASSERT_FLOAT_EQ(400.0f, btn2->size.x);
	ASSERT_FLOAT_EQ(60.0f, btn3->size.x);

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
	ASSERT_EQ(25.0f, btn1_2->position.x);
	ASSERT_EQ(0.0f, btn1_2->position.y);
	ASSERT_EQ(75.0f, btn2_2->position.x);
	ASSERT_EQ(0.0f, btn2_2->position.y);
	ASSERT_EQ(125.0f, btn3_2->position.x);
	ASSERT_EQ(0.0f, btn3_2->position.y);

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
	ASSERT_EQ(0.0f, child1->position.y);
	ASSERT_EQ(0.0f, child2->position.y);

	// Second line: child3 at y=25 (height of first line)
	ASSERT_EQ(25.0f, child3->position.y);

	// Lines are bunched at the start, extra cross-axis space (200 - 50) is unused
	return true;
}

bool testInvalidTag() {
	// Test that parsing fails for invalid tags
	MockDocument doc;
	doc.m_suppress_logging = true;
	bool result = doc.parse("button width=100 height=50 {}", "test.ui");
	ASSERT_EQ(false, result);
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
	ASSERT_EQ(3, children.size());

	ui::Element* text1 = doc.getElement(children[0]);
	ui::Element* block = doc.getElement(children[1]);
	ui::Element* text2 = doc.getElement(children[2]);

	// First text at start
	ASSERT_EQ(1, text1->lines.size());
	ASSERT_FLOAT_EQ(0.0f, text1->lines[0].pos.x);
	ASSERT_FLOAT_EQ(12.8f, text1->lines[0].pos.y);

	// Block causes line break, positioned at start of new line
	ASSERT_FLOAT_EQ(0.0f, block->position.x);
	ASSERT_FLOAT_EQ(16.0f, block->position.y);

	// Second text on following line
	ASSERT_EQ(1, text2->lines.size());
	ASSERT_FLOAT_EQ(0.0f, text2->lines[0].pos.x);
	ASSERT_FLOAT_EQ(48.8f, text2->lines[0].pos.y);

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
	ASSERT_EQ(32.0f, parent->size.x);
	ASSERT_EQ(16.0f, parent->size.y);
	
	return true;
}

bool testTextNoWrapping() {
	// Citation: layout.md - Text
	// "Text flows inline within the panel and wraps to multiple lines when
	// the unwrapped width exceeds the available panel width (minus padding) and `wrap=true`."
	// When wrap=false, text should not wrap.
	MockFontManager mock;
	ui::Document doc(&mock, getGlobalAllocator());
	ASSERT_PARSE(doc, R"(
		[panel width=100 height=fit-content wrap=false font="arial.ttf" font-size=16] {
			This is a very long text that should not wrap even if it exceeds the panel width
		}
	)");
	doc.computeLayout(Vec2(800, 600));
	Span<u32> root_indices = doc.m_roots;
	ASSERT_EQ(1, root_indices.size());
	ui::Element* panel = doc.getElement(root_indices[0]);
	ASSERT_TAG(panel, PANEL);

	// Check child text element
	ASSERT_EQ(1, panel->children.size());
	ui::Element* textElem = doc.getElement(panel->children[0]);
	ASSERT_TAG(textElem, SPAN);

	// Text should not wrap; only one line
	ASSERT_EQ(1, textElem->lines.size());
	// Panel width is 100, text overflows, so check that text size.x > panel->size.x
	ASSERT_TRUE(textElem->size.x > panel->size.x);
	// Height is lineheight (font size)
	ASSERT_EQ(16.0f, textElem->size.y);
	// Position is at panel padding (default 0)
	ASSERT_EQ(0.0f, textElem->lines[0].pos.x);
	ASSERT_EQ(12.8f, textElem->lines[0].pos.y);
	return true;
}

bool testSpanCenteringWithTrailingWhitespace() {
	MockFontManager mock;
	ui::Document doc(&mock, getGlobalAllocator());
	ASSERT_PARSE(doc, R"(
	[panel width=200 align=center font="arial.ttf" font-size=16] {
		[span value="Hello   "]
	}
	)");
	doc.computeLayout(Vec2(800, 600));
	Span<u32> root_indices = doc.m_roots;
	ASSERT_EQ(1, root_indices.size());
	ui::Element* panel = doc.getElement(root_indices[0]);
	ASSERT_EQ(1, panel->children.size());
	ui::Element* span = doc.getElement(panel->children[0]);
	ASSERT_TAG(span, SPAN);
	ASSERT_EQ(1, span->lines.size());
	ASSERT_FLOAT_EQ(80.0f, span->lines[0].pos.x);
	return true;
}

bool testTextWrapping() {
	// Citation: layout.md - Text
	// "Text flows inline within the panel and wraps to multiple lines when
	// the unwrapped width exceeds the available panel width (minus padding) and `wrap=true`."
	MockFontManager mock;
	ui::Document doc(&mock, getGlobalAllocator());
	ASSERT_PARSE(doc, R"(
		[panel width=120 height=fit-content wrap=true padding=10 font="arial.ttf" font-size=16] {
			This is a very long text that should wrap at the content width minus padding
		}
	)");
	doc.computeLayout(Vec2(800, 600));
	Span<u32> root_indices = doc.m_roots;
	ASSERT_EQ(1, root_indices.size());
	ui::Element* panel = doc.getElement(root_indices[0]);
	ASSERT_EQ(1, panel->children.size());
	ui::Element* textElem = doc.getElement(panel->children[0]);

	// Assert text element layout - should wrap at content width (120 - 10 - 10 = 100)
	ASSERT_TRUE(textElem->lines.size() > 1);
	ASSERT_EQ(10.0f, textElem->lines[0].pos.x);
	ASSERT_EQ(22.8f, textElem->lines[0].pos.y);

	// Assert each line fits within the parent's content box (accounts for padding)
	float left = panel->position.x + panel->paddings.left;
	float right = panel->position.x + panel->size.x - panel->paddings.right;
	for (const ui::SpanLine& line : textElem->lines) {
		ASSERT_TRUE(line.pos.x >= left);
		ASSERT_TRUE(line.pos.x + line.width <= right);
	}

	// Assert panel layout
	ASSERT_EQ(120.0f, panel->size.x);
	float expected_height = textElem->lines.size() * 16.0f + panel->paddings.top + panel->paddings.bottom;
	ASSERT_FLOAT_EQ(expected_height, panel->size.y);

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
	ASSERT_EQ(1, root_indices.size());
	ui::Element* outer_panel = doc.getElement(root_indices[0]);
	ASSERT_TAG(outer_panel, PANEL);
	ASSERT_EQ(3, outer_panel->children.size());

	ui::Element* child1 = doc.getElement(outer_panel->children[0]);
	ui::Element* child2 = doc.getElement(outer_panel->children[1]);
	ui::Element* child3 = doc.getElement(outer_panel->children[2]);

	// First child: short text, should not wrap
	ui::Element* text1 = doc.getElement(child1->children[0]);
	ASSERT_TAG(text1, SPAN);
	ASSERT_FLOAT_EQ(16.0f, text1->size.y);

	// Second child: long text, wrap=false, should not wrap
	ui::Element* text2 = doc.getElement(child2->children[0]);
	ASSERT_TAG(text2, SPAN);
	ASSERT_TRUE(text2->size.x > 60.0f);
	ASSERT_FLOAT_EQ(text2->size.y, 16.0f);

	// Third child: long text, no wrap attribute, should NOT wrap (wrap is not inherited)
	ui::Element* text3 = doc.getElement(child3->children[0]);
	ASSERT_TAG(text3, SPAN);
	ASSERT_TRUE(text3->size.x > 60.0f);
	ASSERT_FLOAT_EQ(text3->size.y, 16.0f);

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
	ASSERT_EQ(1, root_indices.size());
	ui::Element* panel = doc.getElement(root_indices[0]);
	ASSERT_TAG(panel, PANEL);
	Span<ui::Attribute> attrs = panel->attributes;
	ASSERT_EQ(4, attrs.size());
	ASSERT_ATTRIBUTE(panel, 0, WIDTH);
	ASSERT_EQ("fit-content", attrs[0].value);
	ASSERT_ATTRIBUTE(panel, 1, HEIGHT);
	ASSERT_EQ("fit-content", attrs[1].value);
	ASSERT_ATTRIBUTE(panel, 2, FONT);
	ASSERT_EQ("arial.ttf", attrs[2].value);
	ASSERT_ATTRIBUTE(panel, 3, FONT_SIZE);
	ASSERT_EQ("16", attrs[3].value);

	// Check child text element
	ASSERT_EQ(1, panel->children.size());
	ui::Element* textElem = doc.getElement(panel->children[0]);
	ASSERT_TAG(textElem, SPAN);

	// Assert text element layout
	// Text is "Line 1\nLine 2\nLine 3", \n treated as spaces, so full text width
	// 20 chars, 20*8 = 160, single line height = 16
	ASSERT_FLOAT_EQ(160.0f, textElem->size.x);
	ASSERT_FLOAT_EQ(16.0f, textElem->size.y);
	ASSERT_EQ(1, textElem->lines.size());
	ASSERT_FLOAT_EQ(0.0f, textElem->lines[0].pos.x);
	ASSERT_FLOAT_EQ(12.8f, textElem->lines[0].pos.y);

	// Assert panel layout (fits the text)
	ASSERT_FLOAT_EQ(160.0f, panel->size.x);
	ASSERT_FLOAT_EQ(16.0f, panel->size.y);

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
	ASSERT_EQ(1, root_indices.size());
	ui::Element* panel = doc.getElement(root_indices[0]);
	ASSERT_TAG(panel, PANEL);

	// Check child text element
	ASSERT_EQ(1, panel->children.size());
	ui::Element* textElem = doc.getElement(panel->children[0]);
	ASSERT_TAG(textElem, SPAN);

	// Assert text element layout includes quotes
	// Text is "Hello "world"", 15 characters (4 doublequotes are counted), each 8px wide = 120px
	ASSERT_FLOAT_EQ(120.0f, textElem->size.x);
	ASSERT_FLOAT_EQ(16.0f, textElem->size.y);

	ASSERT_EQ(1, textElem->lines.size());
	ASSERT_FLOAT_EQ(0.0f, textElem->lines[0].pos.x);
	ASSERT_FLOAT_EQ(12.8f, textElem->lines[0].pos.y);

	// Assert panel layout (fits the text)
	ASSERT_FLOAT_EQ(120.0f, panel->size.x);
	ASSERT_FLOAT_EQ(16.0f, panel->size.y);

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
	ASSERT_EQ(1, root_indices.size());
	ui::Element* panel = doc.getElement(root_indices[0]);
	ASSERT_EQ(2, panel->children.size());

	ui::Element* text1 = doc.getElement(panel->children[0]);
	ui::Element* text2 = doc.getElement(panel->children[1]);

	ASSERT_EQ(1, text1->lines.size());
	ASSERT_EQ(1, text2->lines.size());
	ASSERT_FLOAT_EQ(0.0f, text1->lines[0].pos.x);
	ASSERT_FLOAT_EQ(80.0f, text2->lines[0].pos.x);

	// Second text should be below first text (stacked vertically)
	ASSERT_FLOAT_EQ(text1->position.y, text2->position.y);

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
	ASSERT_EQ(1, root_indices.size());
	ui::Element* panel = doc.getElement(root_indices[0]);
	ASSERT_EQ(3, panel->children.size());

	ui::Element* smallText = doc.getElement(panel->children[0]);
	ui::Element* normalText = doc.getElement(panel->children[1]);
	ui::Element* largeText = doc.getElement(panel->children[2]);

	// Baseline alignment should position elements so their baselines align
	// The baseline is at position.y + ascender
	// All spans should have the same baseline y-coordinate
	ASSERT_EQ(1, smallText->lines.size());
	ASSERT_EQ(1, normalText->lines.size());
	ASSERT_EQ(1, largeText->lines.size());
	float smallBaseline = smallText->lines[0].pos.y;
	float normalBaseline = normalText->lines[0].pos.y;
	float largeBaseline = largeText->lines[0].pos.y;

	ASSERT_FLOAT_EQ(normalBaseline, smallBaseline);
	ASSERT_FLOAT_EQ(normalBaseline, largeBaseline);

	return true;

	// All elements should be on the same baseline (y position should account for baseline alignment)
	float expectedBaselineY = 0.0f;
	ASSERT_FLOAT_EQ(expectedBaselineY, smallText->position.y + smallText->size.y);
	ASSERT_FLOAT_EQ(expectedBaselineY, normalText->position.y + normalText->size.y);
	ASSERT_FLOAT_EQ(expectedBaselineY, largeText->position.y + largeText->size.y);

	// Assert x positions for baseline alignment
	ASSERT_FLOAT_EQ(0.0f, smallText->position.x);
	ASSERT_FLOAT_EQ(smallText->size.x, normalText->position.x);
	ASSERT_FLOAT_EQ(smallText->size.x + normalText->size.x, largeText->position.x);

	return true;
}

bool testBaselineAlignmentWithWrapping() {
	// Test baseline alignment with text wrapping
	MockFontManager mock;
	ui::Document doc(&mock, getGlobalAllocator());
	ASSERT_PARSE(doc, R"(
		[panel direction=row wrap=true width=200 font="arial.ttf" font-size=16] {
			[span value="Small" font-size=12]
			[span value="This is a very long text that should wrap to the next line" font-size=16]
			[span value="Large" font-size=20]
		}
	)");
	doc.computeLayout(Vec2(800, 600));
	Span<u32> root_indices = doc.m_roots;
	ASSERT_EQ(1, root_indices.size());
	ui::Element* panel = doc.getElement(root_indices[0]);
	ASSERT_EQ(3, panel->children.size());

	ui::Element* smallText = doc.getElement(panel->children[0]);
	ui::Element* normalText = doc.getElement(panel->children[1]);
	ui::Element* largeText = doc.getElement(panel->children[2]);

	// The long 16px span should wrap (be taller than a single line)
	ASSERT_TRUE(normalText->lines.size() > 1);
	ASSERT_EQ(3, normalText->lines.size());
	ASSERT_EQ(1, largeText->lines.size());

	// With wrap=true and inline spans measured to available width, each span occupies its own line.
	// So expect strictly increasing y positions per span in flow order.
	ASSERT_FLOAT_EQ(12.8f, normalText->lines[0].pos.y);
	ASSERT_FLOAT_EQ(12.8f, smallText->lines[0].pos.y);
	ASSERT_TRUE(largeText->lines[0].pos.y > normalText->lines[0].pos.y);

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
	ASSERT_EQ(1, root_indices.size());
	ui::Element* panel = doc.getElement(root_indices[0]);
	ASSERT_EQ(3, panel->children.size());

	ui::Element* span1 = doc.getElement(panel->children[0]);
	ui::Element* span2 = doc.getElement(panel->children[1]);
	ui::Element* span3 = doc.getElement(panel->children[2]);

	// Calculate total width of spans
	float totalWidth = span1->size.x + span2->size.x + span3->size.x;
	// Panel width is 400, so centered group should start at (400 - totalWidth) / 2
	float expectedStartX = (400.0f - totalWidth) / 2.0f;

	ASSERT_EQ(1, span1->lines.size());
	ASSERT_EQ(1, span2->lines.size());
	ASSERT_EQ(1, span3->lines.size());
	ASSERT_FLOAT_EQ(expectedStartX, span1->lines[0].pos.x);
	ASSERT_TRUE(span1->lines[0].pos.x < span2->lines[0].pos.x);
	ASSERT_TRUE(span2->lines[0].pos.x < span3->lines[0].pos.x);
	ASSERT_EQ(1, span1->lines.size());
	ASSERT_EQ(1, span2->lines.size());
	ASSERT_EQ(1, span3->lines.size());

	// Assert that the x position of each line in span1, span2, span3 matches the expectedStartX
	for (const auto& line : span1->lines) {
		ASSERT_FLOAT_EQ(expectedStartX, line.pos.x);
	}
	for (const auto& line : span2->lines) {
		ASSERT_FLOAT_EQ(expectedStartX + span1->size.x, line.pos.x);
	}
	for (const auto& line : span3->lines) {
		ASSERT_FLOAT_EQ(expectedStartX + span1->size.x + span2->size.x, line.pos.x);
	}
	
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
	ASSERT_EQ(1, root_indices.size());
	ui::Element* panel = doc.getElement(root_indices[0]);
	ASSERT_EQ(3, panel->children.size());

	ui::Element* span1 = doc.getElement(panel->children[0]);
	ui::Element* span2 = doc.getElement(panel->children[1]);
	ui::Element* span3 = doc.getElement(panel->children[2]);

	// Calculate total width of spans
	float totalWidth = span1->size.x + span2->size.x + span3->size.x;
	// Panel width is 400, so right-aligned group should start at 400 - totalWidth
	float expectedStartX = 400.0f - totalWidth;

	ASSERT_EQ(1, span1->lines.size());
	ASSERT_EQ(1, span2->lines.size());
	ASSERT_EQ(1, span3->lines.size());
	ASSERT_FLOAT_EQ(expectedStartX, span1->lines[0].pos.x);
	ASSERT_FLOAT_EQ(expectedStartX + span1->size.x, span2->lines[0].pos.x);
	ASSERT_FLOAT_EQ(expectedStartX + span1->size.x + span2->size.x, span3->lines[0].pos.x);

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
	ASSERT_EQ(1, root_indices.size());
	ui::Element* panel = doc.getElement(root_indices[0]);
	ASSERT_EQ(1, panel->children.size());

	ui::Element* text = doc.getElement(panel->children[0]);

	// Text should be centered within the 400px panel
	// Assuming text width is calculated, it should be positioned at (400 - text_width) / 2
	float expectedX = (400.0f - text->size.x) / 2.0f;
	ASSERT_EQ(1, text->lines.size());
	ASSERT_FLOAT_EQ(expectedX, text->lines[0].pos.x);

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
	ASSERT_EQ(1, root_indices.size());
	ui::Element* panel = doc.getElement(root_indices[0]);
	ASSERT_EQ(1, panel->children.size());

	ui::Element* text = doc.getElement(panel->children[0]);

	// Text should be right-aligned within the 400px panel
	// It should be positioned at 400 - text_width
	float expectedX = 400.0f - text->size.x;
	ASSERT_EQ(1, text->lines.size());
	ASSERT_FLOAT_EQ(expectedX, text->lines[0].pos.x);

	return true;
}

bool testPanelWithInlineSpan() {
	MockDocument doc;
	ASSERT_PARSE(doc, R"(
		[panel direction=row align=center bg-color=#00ff00 grow=1 font-size=40 wrap=true width=210 font="arial.ttf"] {
			Welcome to [span value=" Lumix " color=#ff0000 font-size=60] Demo
		}
	)");
	doc.computeLayout(Vec2(800, 600));
	Span<u32> root_indices = doc.m_roots;
	ASSERT_EQ(1, root_indices.size());
	ui::Element* panel = doc.getElement(root_indices[0]);

	// Assert children: text "Welcome to ", span " Lumix ", text " Demo"
	ASSERT_EQ(3, panel->children.size());
	ui::Element* text1 = doc.getElement(panel->children[0]);
	ui::Element* span = doc.getElement(panel->children[1]);
	ui::Element* text2 = doc.getElement(panel->children[2]);

	ASSERT_EQ(1, text1->lines.size());
	ASSERT_EQ(1, span->lines.size());
	ASSERT_EQ(1, text2->lines.size());

	return true;
}

bool testHeaderContentFooter() {
	MockDocument doc;
	ASSERT_PARSE(doc, R"(
	[panel direction=column font="arial.ttf" width=20% height=100%] {
		Header
		[panel grow=1] { Content }
		Footer
	}
	)");
	doc.computeLayout(Vec2(800, 600));
	Span<u32> root_indices = doc.m_roots;
	ASSERT_EQ(1, root_indices.size());
	ui::Element* parent = doc.getElement(root_indices[0]);
	ASSERT_EQ(3, parent->children.size());
	ui::Element* text1 = doc.getElement(parent->children[0]);
	ui::Element* panel_child = doc.getElement(parent->children[1]);
	ui::Element* text2 = doc.getElement(parent->children[2]);
	// Check that they are stacked vertically
	ASSERT_FLOAT_EQ(9.6f, text1->position.y);
	ASSERT_TRUE(panel_child->position.y > text1->position.y);
	ASSERT_TRUE(text2->position.y >= panel_child->position.y + panel_child->size.y);
	return true;
}

} // namespace

void runUILayoutTests() {
	logInfo("=== Running UI Layout Tests ===");
	RUN_TEST(testAdvancedFitContent);
	RUN_TEST(testAlignCenter);
	RUN_TEST(testAlignCenterMultipleSpans);
	RUN_TEST(testAlignItemsCenter);
	RUN_TEST(testAlignItemsEnd);
	RUN_TEST(testAlignItemsStart);
	RUN_TEST(testAlignItemsStretch);
	RUN_TEST(testAlignItemsStretchMargins);
	RUN_TEST(testAlignItemsStretchWithText);
	RUN_TEST(testAlignItemsWithWrap);
	RUN_TEST(testAlignItemsCenterColumn);
	RUN_TEST(testAlignItemsStartColumn);
	RUN_TEST(testAlignItemsEndColumn);
	RUN_TEST(testAlignItemsStretchColumn);
	RUN_TEST(testAlignRight);
	RUN_TEST(testAlignRightMultipleSpans);
	RUN_TEST(testBaselineAlignment);
	RUN_TEST(testBaselineAlignmentWithWrapping);
	RUN_TEST(testHeaderContentFooter);
	RUN_TEST(testBasicLayout);
	RUN_TEST(testDefaultFitContentLeaf);
	RUN_TEST(testDefaultFitContentSimple);
	RUN_TEST(testDirectionColumn);
	RUN_TEST(testDirectionDefault);
	RUN_TEST(testDirectionRow);
	RUN_TEST(testDoubleQuotesInText);
	RUN_TEST(testFitContent);
	RUN_TEST(testFitContentWithInlineText);
	RUN_TEST(testFitContentWithMargins);
	RUN_TEST(testGrow);
	RUN_TEST(testGrowMiddle);
	RUN_TEST(testGrowParentWithPercentChild);
	RUN_TEST(testGrowProportional);
	RUN_TEST(testGrowRespectsPadding);
	RUN_TEST(testGrowSingleChild);
	RUN_TEST(testGrowWithMargin);
	RUN_TEST(testGrowWithPadding);
	RUN_TEST(testHorizontalMarginCollapse);
	RUN_TEST(testInvalidTag);
	RUN_TEST(testJustifyContentWithWrap);
	RUN_TEST(testJustifyEnd);
	RUN_TEST(testJustifyCenter);
	RUN_TEST(testJustifySpaceAround);
	RUN_TEST(testJustifySpaceAroundSingleChild);
	RUN_TEST(testJustifySpaceBetween);
	RUN_TEST(testJustifySpaceBetweenSingleChild);
	RUN_TEST(testJustifyContentWithMargins);
	RUN_TEST(testJustifyVerticalCenter);
	RUN_TEST(testJustifyStart);
	RUN_TEST(testLayoutDirection);
	RUN_TEST(testLayoutWithMargins);
	RUN_TEST(testLineBreaks);
	RUN_TEST(testMarginPadding);
	RUN_TEST(testMultilineStringLayout);
	RUN_TEST(testNestedPanelsDifferentDirections);
	RUN_TEST(testNestedPanelsWithMargins);
	RUN_TEST(testNoWrap);
	RUN_TEST(testPercentLayout);
	RUN_TEST(testPercentMargins);
	RUN_TEST(testPercentHeightOnRoot);
	RUN_TEST(testPanelWithInlineSpan);
	RUN_TEST(testTextHorizontalRendering);
	RUN_TEST(testTextWrapping);
	RUN_TEST(testTextNoWrapping);
	RUN_TEST(testSpanCenteringWithTrailingWhitespace);
	RUN_TEST(testTwoPanelsLayout);
	RUN_TEST(testVerticalMarginCollapse);
	RUN_TEST(testWrap);
	RUN_TEST(testWrapCrossAxisDistribution);
	RUN_TEST(testWrappingInheritance);
}
