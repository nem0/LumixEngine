#include "core/log.h"
#include "tests/common.h"

using namespace Lumix;

namespace {

bool testTwoPanelsLayout() {
	// Citation: layout.md - Element Sizing
	// "Root elements behave like they are children of a box that covers the whole screen with 0 padding and `direction=column`."
	MockDocument doc;
	ASSERT_PARSE(doc, R"(
	[box width=100% height=100]
	[box width=150 height=80]
	)");
	Span<u32> root_indices = doc.m_roots;
	ASSERT_EQ(2, root_indices.size());
	ui::Element* panel1 = doc.getElement(root_indices[0]);
	ui::Element* panel2 = doc.getElement(root_indices[1]);
	
	doc.computeLayout(Vec2(800, 600));
	
	ASSERT_FLOAT_EQ(800.0f, panel1->size.x);
	ASSERT_FLOAT_EQ(100.0f, panel1->size.y);
	ASSERT_FLOAT_EQ(150.0f, panel2->size.x);
	ASSERT_FLOAT_EQ(80.0f, panel2->size.y);
	
	ASSERT_FLOAT_EQ(0.0f, panel1->position.x);
	ASSERT_FLOAT_EQ(0.0f, panel1->position.y);
	ASSERT_FLOAT_EQ(0.0f, panel2->position.x);
	ASSERT_FLOAT_EQ(100.0f, panel2->position.y);
	
	return true;
}

bool testPercentHeightOnRoot() {
	// Citation: layout.md - Element Sizing
	// "Root elements behave like they are children of a box that covers the whole screen with 0 padding and `direction=column`."
	// "%: Percentage of parent (or viewport for roots). E.g., `width=50%` for half the parent's width.
	MockDocument doc;
	ASSERT_PARSE(doc, R"(
	[box width=100 height=50%]
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

bool testJustifyCenter() {
	// Citation: elements_attributes.md - Justification
	// "- `center`: Elements are centered as a group."

	MockDocument doc;
	ASSERT_PARSE(doc, R"(
	[box width=400 height=200 direction=row justify-content=center] {
		[box width=50 height=50] {}
		[box width=50 height=50] {}
	}
	)");
	doc.computeLayout(Vec2(800, 600));
	ASSERT_TRUE(doc.m_elements.size() >= 3);
	ui::Element* panel = doc.getElement(0);
	ui::Element* btn1 = doc.getElement(1);
	ui::Element* btn2 = doc.getElement(2);
	
	float expected_x = (400 - 100) * 0.5f;
	ASSERT_FLOAT_EQ(expected_x, btn1->position.x);
	ASSERT_FLOAT_EQ(expected_x + 50.0f, btn2->position.x);
	return true;
}

bool testJustifyStart() {
	// Citation: elements_attributes.md - Justification
	// "- `start`: Elements are placed sequentially starting from the container's start edge plus padding"
	MockDocument doc;
	ASSERT_PARSE(doc, R"(
	[box width=400 height=200 direction=row justify-content=start] {
		[box width=50 height=50] {}
		[box width=50 height=50] {}
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
	// Citation: elements_attributes.md - Justification
	// "- `end`: Elements are placed starting from the container's end edge minus padding."
	MockDocument doc;
	ASSERT_PARSE(doc, R"(
	[box width=400 height=200 direction=row justify-content=end] {
		[box width=50 height=50] {}
		[box width=50 height=50] {}
	}
	)");
	doc.computeLayout(Vec2(800, 600));
	ASSERT_TRUE(doc.m_elements.size() >= 3);
	ui::Element* panel = doc.getElement(0);
	ui::Element* btn1 = doc.getElement(1);
	ui::Element* btn2 = doc.getElement(2);
	float expected_x = 400 - 100;
	ASSERT_FLOAT_EQ(expected_x, btn1->position.x);
	ASSERT_FLOAT_EQ(expected_x + 50.0f, btn2->position.x);
	return true;
}

bool testJustifySpaceBetween() {
	// Citation: elements_attributes.md - Justification
	// "- `space-between`: Elements are evenly distributed with the first at start and last at end.
	// Remaining space (container_size - total_sizes - margins - padding) is divided equally among n-1 gaps."
	MockDocument doc;
	ASSERT_PARSE(doc, R"(
	[box width=400 height=200 direction=row justify-content=space-between] {
		[box width=50 height=50] {}
		[box width=50 height=50] {}
		[box width=50 height=50] {}
	}
	)");
	doc.computeLayout(Vec2(800, 600));
	ASSERT_TRUE(doc.m_elements.size() >= 4);
	ui::Element* panel = doc.getElement(0);
	ui::Element* btn1 = doc.getElement(1);
	ui::Element* btn2 = doc.getElement(2);
	ui::Element* btn3 = doc.getElement(3);

	ASSERT_FLOAT_EQ(0.0f, btn1->position.x);
	ASSERT_FLOAT_EQ(175.0f, btn2->position.x);
	ASSERT_FLOAT_EQ(350.0f, btn3->position.x);
	return true;
}

bool testJustifySpaceAround() {
	// Citation: elements_attributes.md - box
	// "- `space-around`: Equal space is added around each element.
	// Total space is divided equally around n elements, with each getting space / (2n) on both sides."
	MockDocument doc;
	ASSERT_PARSE(doc, R"(
	[box width=400 height=200 direction=row justify-content=space-around] {
		[box width=50 height=50] {}
		[box width=50 height=50] {}
	}
	)");
	doc.computeLayout(Vec2(800, 600));
	ASSERT_TRUE(doc.m_elements.size() >= 3);
	ui::Element* panel = doc.getElement(0);
	ui::Element* btn1 = doc.getElement(1);
	ui::Element* btn2 = doc.getElement(2);

	ASSERT_FLOAT_EQ(100.0f, btn1->position.x);
	ASSERT_FLOAT_EQ(250.0f, btn2->position.x);
	return true;
}

bool testJustifySpaceAroundSingleChild() {
	// Citation: layout.md - Justification
	// "With a single child, `space-around` behaves like `center`."
	MockDocument doc;
	ASSERT_PARSE(doc, R"(
	[box width=400 height=200 direction=row justify-content=space-around] {
		[box width=50 height=50] {}
	}
	)");
	doc.computeLayout(Vec2(800, 600));
	ASSERT_TRUE(doc.m_elements.size() >= 2);
	ui::Element* panel = doc.getElement(0);
	ui::Element* child = doc.getElement(1);
	ASSERT_FLOAT_EQ(175.0f, child->position.x);
	return true;
}

bool testJustifySpaceBetweenSingleChild() {
	// Citation: layout.md - Justification
	// "With a single child, `space-between` behaves like `start`."
	MockDocument doc;
	ASSERT_PARSE(doc, R"(
	[box width=400 height=200 direction=row justify-content=space-between] {
		[box width=50 height=50] {}
	}
	)");
	doc.computeLayout(Vec2(800, 600));
	ASSERT_TRUE(doc.m_elements.size() >= 2);
	ui::Element* panel = doc.getElement(0);
	ui::Element* child = doc.getElement(1);
	ASSERT_FLOAT_EQ(0.0f, child->position.x);
	return true;
}

bool testJustifyContentWithMargins() {
	// Citation: layout.md - Justification
	// "In all justification modes, margins between elements are preserved and included in the positioning calculations"
	MockDocument doc;
	ASSERT_PARSE(doc, R"(
	[box width=400 height=200 direction=row justify-content=end padding=5] {
		[box width=50 height=50 margin=10] {}
		[box width=50 height=50 margin=10] {}
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
	// Citation: elements_attributes.md - Justification
	// "- `center`: Elements are centered as a group."
	MockDocument doc;
	ASSERT_PARSE(doc, R"(
	[box width=400 height=200 direction=column justify-content=center] {
		[box width=50 height=50] {}
		[box width=50 height=50] {}
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
	[box width=400 height=200] {
		[box width=50% height=50%] {}
	}
	)");
	doc.computeLayout(Vec2(800, 600));
	ASSERT_TRUE(doc.m_elements.size() >= 2);
	ui::Element* parent = doc.getElement(0);
	ui::Element* child = doc.getElement(1);

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
	[box width=400 height=200] {
		[box margin=10%] {}
	}
	)");
	doc.computeLayout(Vec2(800, 600));
	ASSERT_TRUE(doc.m_elements.size() >= 2);
	ui::Element* parent = doc.getElement(0);
	ui::Element* child = doc.getElement(1);

	ASSERT_FLOAT_EQ(20.0f, child->margins.top);
	ASSERT_FLOAT_EQ(40.0f, child->margins.right);
	ASSERT_FLOAT_EQ(20.0f, child->margins.bottom);
	ASSERT_FLOAT_EQ(40.0f, child->margins.left);
	return true;
}

bool testMarginPadding() {
	// Citation: layout.md - Margins and Padding
	// "Margins provide external spacing between elements and their containers, affecting position but not size."
	// "Padding adds internal space within the element's border, expanding its total size."
	MockDocument doc;
	ASSERT_PARSE(doc, R"(
	[box width=400 height=200 margin=10 padding=5] {
		[box width=50 height=50] {}
	}
	)");
	doc.computeLayout(Vec2(800, 600));
	ASSERT_TRUE(doc.m_elements.size() >= 2);
	ui::Element* parent = doc.getElement(0);
	ui::Element* child = doc.getElement(1);

	float expected_child_x = parent->position.x + parent->paddings.left + child->margins.left;
	float expected_child_y = parent->position.y + parent->paddings.top + child->margins.top;
	ASSERT_FLOAT_EQ(expected_child_x, child->position.x);
	ASSERT_FLOAT_EQ(expected_child_y, child->position.y);

	return true;
}

bool testHorizontalSideSpecificMarginPadding() {
	MockDocument doc;
	ASSERT_PARSE(doc, R"(
	[box width=300 height=100 direction=row padding-left=11 padding-right=13] {
		[box width=50 height=20 margin-left=7 margin-right=17] {}
		[box width=30 height=20 margin-left=5 margin-right=3] {}
	}
	)");
	doc.computeLayout(Vec2(800, 600));
	ASSERT_TRUE(doc.m_elements.size() >= 3);
	ui::Element* parent = doc.getElement(0);
	ui::Element* first = doc.getElement(1);
	ui::Element* second = doc.getElement(2);

	ASSERT_FLOAT_EQ(11.0f, parent->paddings.left);
	ASSERT_FLOAT_EQ(13.0f, parent->paddings.right);

	ASSERT_FLOAT_EQ(7.0f, first->margins.left);
	ASSERT_FLOAT_EQ(17.0f, first->margins.right);
	ASSERT_FLOAT_EQ(5.0f, second->margins.left);
	ASSERT_FLOAT_EQ(3.0f, second->margins.right);

	ASSERT_FLOAT_EQ(18.0f, first->position.x);
	ASSERT_FLOAT_EQ(85.0f, second->position.x);

	return true;
}

bool testVerticalSideSpecificMarginPadding() {
	MockDocument doc;
	ASSERT_PARSE(doc, R"(
	[box width=200 height=300 direction=column padding-top=11 padding-bottom=13] {
		[box width=50 height=20 margin-top=7 margin-bottom=17] {}
		[box width=30 height=30 margin-top=5 margin-bottom=3] {}
	}
	)");
	doc.computeLayout(Vec2(800, 600));
	ASSERT_TRUE(doc.m_elements.size() >= 3);
	ui::Element* parent = doc.getElement(0);
	ui::Element* first = doc.getElement(1);
	ui::Element* second = doc.getElement(2);

	ASSERT_FLOAT_EQ(11.0f, parent->paddings.top);
	ASSERT_FLOAT_EQ(13.0f, parent->paddings.bottom);

	ASSERT_FLOAT_EQ(7.0f, first->margins.top);
	ASSERT_FLOAT_EQ(17.0f, first->margins.bottom);
	ASSERT_FLOAT_EQ(5.0f, second->margins.top);
	ASSERT_FLOAT_EQ(3.0f, second->margins.bottom);

	ASSERT_FLOAT_EQ(18.0f, first->position.y);
	ASSERT_FLOAT_EQ(55.0f, second->position.y);

	return true;
}

bool testSideSpecificShorthandPrecedence() {
	MockDocument doc;
	ASSERT_PARSE(doc, R"(
	[box width=100 height=100 direction=column padding=10 padding-top=3 padding-bottom=4] {
		[box width=10 height=10 margin=8 margin-top=1 margin-bottom=2] {}
	}
	[box width=100 height=100 direction=column padding-top=3 padding=10] {
		[box width=10 height=10 margin-top=1 margin=8] {}
	}
	)");
	doc.computeLayout(Vec2(800, 600));
	ASSERT_EQ(2, doc.m_roots.size());

	ui::Element* parent1 = doc.getElement(doc.m_roots[0]);
	ui::Element* parent2 = doc.getElement(doc.m_roots[1]);
	ASSERT_EQ(1, parent1->children.size());
	ASSERT_EQ(1, parent2->children.size());
	ui::Element* child1 = doc.getElement(parent1->children[0]);
	ui::Element* child2 = doc.getElement(parent2->children[0]);

	ASSERT_FLOAT_EQ(10.0f, parent1->paddings.left);
	ASSERT_FLOAT_EQ(10.0f, parent1->paddings.right);
	ASSERT_FLOAT_EQ(3.0f, parent1->paddings.top);
	ASSERT_FLOAT_EQ(4.0f, parent1->paddings.bottom);

	ASSERT_FLOAT_EQ(8.0f, child1->margins.left);
	ASSERT_FLOAT_EQ(8.0f, child1->margins.right);
	ASSERT_FLOAT_EQ(1.0f, child1->margins.top);
	ASSERT_FLOAT_EQ(2.0f, child1->margins.bottom);

	ASSERT_FLOAT_EQ(10.0f, parent2->paddings.left);
	ASSERT_FLOAT_EQ(10.0f, parent2->paddings.right);
	ASSERT_FLOAT_EQ(10.0f, parent2->paddings.top);
	ASSERT_FLOAT_EQ(10.0f, parent2->paddings.bottom);

	ASSERT_FLOAT_EQ(8.0f, child2->margins.left);
	ASSERT_FLOAT_EQ(8.0f, child2->margins.right);
	ASSERT_FLOAT_EQ(8.0f, child2->margins.top);
	ASSERT_FLOAT_EQ(8.0f, child2->margins.bottom);

	ASSERT_FLOAT_EQ(parent1->position.y + 4.0f, child1->position.y);
	ASSERT_FLOAT_EQ(parent2->position.y + 18.0f, child2->position.y);

	return true;
}

bool testBasicLayout() {
	// Citation: layout.md - Element Sizing
	// "Each UI element has `width` and `height` attributes that control its size, known as dimensions. Set them explicitly for a fixed size;"
	MockDocument doc;
	ASSERT_PARSE(doc, "[box width=200 height=100] {}");
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
	[box width=fit-content height=fit-content] {
		[box width=640 height=480] {}
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
	[box height=fit-content] {
		[box height=100 margin=10] {}
	}
	)");
	doc.computeLayout(Vec2(800, 600));
	ASSERT_TRUE(doc.m_elements.size() >= 2);
	ui::Element* parent = doc.getElement(0);

	ASSERT_FLOAT_EQ(120.0f, parent->size.y);
	return true;
}

bool testGrow() {
	// Citation: layout.md - Grow
	// "The `grow` attribute controls how an element expands 
	// to fill available space in its parent container along the main axis"
	MockDocument doc;
	ASSERT_PARSE(doc, R"(
	[box width=300 height=100 direction=row] {
		[box width=100 height=50] {}
		[box grow=1 height=50] {}
	}
	)");
	doc.computeLayout(Vec2(800, 600));
	ASSERT_TRUE(doc.m_elements.size() >= 3);
	ui::Element* parent = doc.getElement(0);
	ui::Element* child1 = doc.getElement(1);
	ui::Element* child2 = doc.getElement(2);
	ASSERT_FLOAT_EQ(100.0f, child1->size.x);

	ASSERT_FLOAT_EQ(200.0f, child2->size.x);
	return true;
}

bool testGrowWithPadding() {
	// Citation: layout.md - Grow
	// "Grow respects the parent's padding: available space is the content area 
	// after subtracting padding, and each growing child's margin is also subtracted before distributing."
	MockDocument doc;
	ASSERT_PARSE(doc, R"(
	[box width=300 height=100 direction=row padding=10] {
		[box width=100 height=50] {}
		[box grow=1 height=50] {}
	}
	)");
	doc.computeLayout(Vec2(800, 600));
	ASSERT_TRUE(doc.m_elements.size() >= 3);
	ui::Element* parent = doc.getElement(0);
	ui::Element* child1 = doc.getElement(1);
	ui::Element* child2 = doc.getElement(2);
	ASSERT_FLOAT_EQ(100.0f, child1->size.x);

	ASSERT_FLOAT_EQ(180.0f, child2->size.x);
	return true;
}

bool testGrowWithMargin() {
	// Citation: layout.md - Grow
	// "Grow respects the parent's padding: available space is the content area 
	// after subtracting padding, and each growing child's margin is also subtracted before distributing."
	MockDocument doc;
	ASSERT_PARSE(doc, R"(
	[box width=300 height=100 direction=row] {
		[box width=100 height=50 margin=5] {}
		[box grow=1 height=50 margin=5] {}
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
	// Citation: layout.md - Grow
	// "The `grow` attribute controls how an element expands 
	// to fill available space in its parent container along the main axis"
	MockDocument doc;
	ASSERT_PARSE(doc, R"(
	[box width=300 height=100 direction=row] {
		[box grow=1 height=50] {}
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
	// Citation: layout.md - Grow
	// "Elements with a non-zero `grow` value share the remaining space in proportion 
	// to their weights after all fixed-size children have been measured."
	MockDocument doc;
	ASSERT_PARSE(doc, R"(
	[box width=400 direction=row] {
		[box width=100 height=50] {}
		[box grow=2 height=50] {}
		[box grow=1 height=50] {}
	}
	)");
	doc.computeLayout(Vec2(800, 600));
	ui::Element* parent = doc.getElement(0);
	ui::Element* child1 = doc.getElement(1);
	ui::Element* child2 = doc.getElement(2);
	ui::Element* child3 = doc.getElement(3);

	ASSERT_FLOAT_EQ(100.0f, child1->size.x);
	ASSERT_FLOAT_EQ(200.0f, child2->size.x);
	ASSERT_FLOAT_EQ(100.0f, child3->size.x);
	return true;
}

bool testGrowVertical() {
	// Citation: layout.md - Grow
	// "The `grow` attribute controls how an element expands
	// to fill available space in its parent container along the main axis"
	MockDocument doc;
	ASSERT_PARSE(doc, R"(
	[box width=200 height=300 direction=column] {
		[box width=100 height=50] {}
		[box grow=1 width=100] {}
	}
	)");
	doc.computeLayout(Vec2(800, 600));
	ASSERT_TRUE(doc.m_elements.size() >= 3);
	ui::Element* parent = doc.getElement(0);
	ui::Element* child1 = doc.getElement(1);
	ui::Element* child2 = doc.getElement(2);

	ASSERT_FLOAT_EQ(50.0f, child1->size.y);
	ASSERT_FLOAT_EQ(250.0f, child2->size.y);
	ASSERT_FLOAT_EQ(0.0f, child1->position.y);
	ASSERT_FLOAT_EQ(50.0f, child2->position.y);

	return true;
}

bool testGrowVerticalMiddle() {
	// Citation: layout.md - Grow
	// "The `grow` attribute controls how an element expands
	// to fill available space in its parent container along the main axis"
	MockDocument doc;
	ASSERT_PARSE(doc, R"(
	[box width=200 height=300 direction=column] {
		[box width=100 height=50] {}
		[box grow=1 width=100] {}
		[box width=100 height=100] {}
	}
	)");
	doc.computeLayout(Vec2(800, 600));
	ASSERT_TRUE(doc.m_elements.size() >= 4);
	ui::Element* top = doc.getElement(1);
	ui::Element* middle = doc.getElement(2);
	ui::Element* bottom = doc.getElement(3);

	ASSERT_FLOAT_EQ(50.0f, top->size.y);
	ASSERT_FLOAT_EQ(150.0f, middle->size.y);
	ASSERT_FLOAT_EQ(100.0f, bottom->size.y);

	ASSERT_FLOAT_EQ(0.0f, top->position.y);
	ASSERT_FLOAT_EQ(50.0f, middle->position.y);
	ASSERT_FLOAT_EQ(200.0f, bottom->position.y);

	return true;
}

bool testGrowMiddle() {
	// Citation: layout.md - Grow
	// "Left-fill-right (the classic toolbar pattern) works correctly because 
	// growing is computed in a two-pass manner ΓÇö fixed children are sized first,
	// then remaining space is distributed among all `grow` children:"
	MockDocument doc;
	ASSERT_PARSE(doc, R"(
	[box width=300 height=50 direction=row] {
		[box width=50 height=50] {}
		[box grow=1 height=50] {}
		[box width=100 height=50] {}
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
	// Citation: layout.md - Grow
	// "Percentage-based dimensions (e.g., `width=100%`) are resolved in **step 1.4**, 
	// after the parent's width has been finalized by grow distribution."
	MockDocument doc;
	ASSERT_PARSE(doc, R"(
	[box width=100% direction=row] {
		[box width=50 grow=1 height=100 direction=row] {
			[box width=100% height=50] {}
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
	[box width=200 height=100 margin=10] {}
	[box width=150 height=80 margin=5] {}
	)");
	Span<u32> root_indices = doc.m_roots;
	ASSERT_EQ(2, root_indices.size());
	ui::Element* panel1 = doc.getElement(root_indices[0]);
	ui::Element* panel2 = doc.getElement(root_indices[1]);
	
	doc.computeLayout(Vec2(800, 600));
	
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
	[box width=400 height=300 padding=10] {
		[box width=200 height=100 margin=5] {}
		[box width=150 height=80 margin=10] {}
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
	float padding_left = parent->paddings.left;
	float padding_top = parent->paddings.top;
	
	ASSERT_FLOAT_EQ(parent_x + padding_left + 5.0f, child1->position.x);
	ASSERT_FLOAT_EQ(parent_y + padding_top + 5.0f, child1->position.y);
	
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
	[box direction=row padding=0] {
		[box width=100 height=50 margin=0] {}
		[box width=150 height=50 margin=0] {}
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
	
	ASSERT_FLOAT_EQ(parent_x, child1->position.x);
	ASSERT_FLOAT_EQ(parent_y, child1->position.y);
	
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
	[box direction=column] {
		[box width=100 height=50 margin=5] {}
		[box width=100 height=80 margin=5] {}
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
	
	ASSERT_FLOAT_EQ(parent_x + 5.0f, child1->position.x);
	ASSERT_FLOAT_EQ(parent_y + 5.0f, child1->position.y);
	
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
	[box] {
		[box width=100 height=50] {}
		[box width=100 height=80] {}
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
	
	ASSERT_FLOAT_EQ(parent_x, child1->position.x);
	ASSERT_FLOAT_EQ(parent_y, child1->position.y);
	
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
	[box direction=row width=800 height=600 bg-color=#000000] {
		[box direction=column width=150] {
			[box width=100 height=50 bg-color=#ffffff] {}
			[box width=100 height=60 bg-color=#ff00ff] {}
		}
		[box direction=column width=200] {
			[box width=150 height=40 bg-color=#0000ff] {}
			[box width=150 height=70 bg-color=#ff0000] {}
			[box width=150 height=30 bg-color=#00ff00] {}
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

	ASSERT_FLOAT_EQ(150.0f, column1->size.x);
	ASSERT_EQ(2, column1->children.size());
	ui::Element* c1_child1 = doc.getElement(column1->children[0]);
	ui::Element* c1_child2 = doc.getElement(column1->children[1]);
	
	ASSERT_FLOAT_EQ(200.0f, column2->size.x);
	ASSERT_EQ(3, column2->children.size());
	ui::Element* c2_child1 = doc.getElement(column2->children[0]);
	ui::Element* c2_child2 = doc.getElement(column2->children[1]);
	ui::Element* c2_child3 = doc.getElement(column2->children[2]);
	
	float parent_x = parent->position.x;
	float parent_y = parent->position.y;
	
	ASSERT_EQ(parent_x, column1->position.x);
	ASSERT_EQ(parent_y, column1->position.y);
	
	ASSERT_FLOAT_EQ(parent_x + 150.0f, column2->position.x);
	ASSERT_EQ(parent_y, column2->position.y);
	
	ASSERT_EQ(column1->position.x, c1_child1->position.x);
	ASSERT_EQ(column1->position.y, c1_child1->position.y);
	
	ASSERT_EQ(column1->position.x, c1_child2->position.x);
	ASSERT_FLOAT_EQ(column1->position.y + 50.0f, c1_child2->position.y);
	
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
	[box width=fit-content height=fit-content direction=row] {
		[box width=fit-content height=100] {
			[box width=200 height=50] {}
		}
		[box width=100 height=fit-content] {
			[box height=30] {}
			[box height=40] {}
		}
	}
	)");
	doc.computeLayout(Vec2(800, 600));

	Span<u32> root_indices = doc.m_roots;
	ASSERT_EQ(1, root_indices.size());
	ui::Element* parent = doc.getElement(root_indices[0]);
	ASSERT_EQ(2, parent->children.size());
	ui::Element* child1 = doc.getElement(parent->children[0]);
	ui::Element* child2 = doc.getElement(parent->children[1]);
	
	ASSERT_EQ(1, child1->children.size());
	ui::Element* grandchild1 = doc.getElement(child1->children[0]);
	
	ASSERT_EQ(2, child2->children.size());
	ui::Element* grandchild2_1 = doc.getElement(child2->children[0]);
	ui::Element* grandchild2_2 = doc.getElement(child2->children[1]);
		
	ASSERT_FLOAT_EQ(200.0f, child1->size.x);
	ASSERT_FLOAT_EQ(100.0f, child1->size.y);
	
	ASSERT_FLOAT_EQ(100.0f, child2->size.x);
	ASSERT_FLOAT_EQ(70.0f, child2->size.y);
	
	ASSERT_FLOAT_EQ(300.0f, parent->size.x);
	ASSERT_FLOAT_EQ(100.0f, parent->size.y);
	
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
	// Citation: elements_attributes.md - Sizing and layout properties
	// "| `width` | Sets the element's width. | `fit-content` |"
	MockDocument doc;
	ASSERT_PARSE(doc, R"(
	[box] {
		[box width=50 height=30] {}
		[box width=70 height=40] {}
	}
	)");
	Span<u32> root_indices = doc.m_roots;
	ASSERT_EQ(1, root_indices.size());
	ui::Element* parent = doc.getElement(root_indices[0]);
	ASSERT_EQ(2, parent->children.size());
	ui::Element* child1 = doc.getElement(parent->children[0]);
	ui::Element* child2 = doc.getElement(parent->children[1]);
	
	doc.computeLayout(Vec2(800, 600));
	
	ASSERT_EQ(70.0f, parent->size.x);
	ASSERT_EQ(70.0f, parent->size.y);
	
	return true;
}

bool testDefaultFitContentLeaf() {
	// Citation: layout.md - Element Sizing
	// "Each UI element has `width` and `height` attributes that control its size, known as dimensions. Set them explicitly for a fixed size; otherwise, they default to `fit-content`,"
	MockDocument doc;
	ASSERT_PARSE(doc, R"(
	[box width=200 height=100] {
		[box] {}
	}
	)");
	Span<u32> root_indices = doc.m_roots;
	ASSERT_EQ(1, root_indices.size());
	ui::Element* parent = doc.getElement(root_indices[0]);
	ASSERT_EQ(1, parent->children.size());
	ui::Element* child = doc.getElement(parent->children[0]);
	
	doc.computeLayout(Vec2(800, 600));
	
	ASSERT_EQ(200.0f, parent->size.x);
	ASSERT_EQ(100.0f, parent->size.y);
	
	ASSERT_EQ(0.0f, child->size.x);
	ASSERT_EQ(0.0f, child->size.y);
	
	return true;
}

bool testVerticalMarginCollapse() {
	// Citation: layout.md - Margin Collapsing
	// "Adjacent margins combine into the larger value to prevent excessive gaps."
	MockDocument doc;
	ASSERT_PARSE(doc, R"(
	[box] {
		[box margin=10 height=50] {}
		[box margin=20 height=50] {}
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
	[box direction=row] {
		[box margin=10 width=50] {}
		[box margin=20 width=50] {}
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
	[box width=100 height=100 direction=row wrap=true] {
		[box width=50 height=50] {}
		[box width=50 height=50] {}
		[box width=50 height=50] {}
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
	[box width=100 height=100 direction=row wrap=nowrap] {
		[box width=50 height=50] {}
		[box width=50 height=50] {}
		[box width=50 height=50] {}
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
	[box width=400 height=200 direction=row align-items=center] {
		[box width=50 height=30] {}
		[box width=50 height=50] {}
		[box width=50 height=40] {}
	}
	)");
	doc.computeLayout(Vec2(800, 600));
	ui::Element* panel = doc.getElement(0);
	ui::Element* btn1 = doc.getElement(1);
	ui::Element* btn2 = doc.getElement(2);
	ui::Element* btn3 = doc.getElement(3);

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
	[box width=400 height=200 direction=row align-items=start] {
		[box width=50 height=30] {}
		[box width=50 height=40] {}
	}
	)");
	doc.computeLayout(Vec2(800, 600));
	ui::Element* panel = doc.getElement(0);
	ui::Element* btn1 = doc.getElement(1);
	ui::Element* btn2 = doc.getElement(2);

	ASSERT_EQ(0.0f, btn1->position.y);
	ASSERT_EQ(0.0f, btn2->position.y);

	return true;
}

bool testAlignItemsEnd() {
	// Citation: layout.md - Off-axis alignment
	// "- `end`: Elements are aligned to the end of the off-axis (bottom for row, right for column)."
	MockDocument doc;
	ASSERT_PARSE(doc, R"(
	[box width=400 height=200 direction=row align-items=end] {
		[box width=50 height=30] {}
		[box width=50 height=50] {}
		[box width=50 height=40] {}
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
	[box width=400 height=200 direction=row align-items=stretch] {
		[box width=50] {}
		[box width=50] {}
		[box width=50 height=30] {}
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
	[box width=400 height=200 direction=column align-items=stretch] {
		[box height=50 margin=10] {}
		[box height=50 margin=5] {}
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
	// Test align-items=stretch with a child box containing right-aligned text
	MockFontManager mock;
	ui::Document doc(&mock, getGlobalAllocator());
	ASSERT_PARSE(doc, R"(
	[box width=400 height=200 align-items=stretch direction=column] {
		[box align=right font="arial.ttf" font-size=16] {
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

	ASSERT_FLOAT_EQ(400.0f, parent->size.x);
	ASSERT_FLOAT_EQ(200.0f, parent->size.y);

	ASSERT_FLOAT_EQ(400.0f, child_panel->size.x);
	ASSERT_FLOAT_EQ(16.0f, child_panel->size.y);

	ASSERT_EQ(1, child_panel->children.size());
	ui::Element* text_elem = doc.getElement(child_panel->children[0]);
	ASSERT_TAG(text_elem, SPAN);

	ASSERT_EQ(1, text_elem->lines.size());
	float text_width = text_elem->size.x;
	ASSERT_FLOAT_EQ(child_panel->size.x - text_width, text_elem->lines[0].pos.x);
	ASSERT_FLOAT_EQ(12.8f, text_elem->lines[0].pos.y);

	return true;
}

bool testAlignItemsWithWrap() {
	// Citation: layout.md - Wrapping
	// "When `wrap=true`, `align-items` is applied to each wrapped line or column individually, rather than to the entire container."
	// "Justification and item aligment are applied to each row/column separately."
	MockDocument doc;
	ASSERT_PARSE(doc, R"(
	[box width=100 height=200 direction=row wrap=true align-items=center] {
		[box width=50 height=30] {}
		[box width=50 height=50] {}
		[box width=50 height=30] {}
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
	[box width=400 height=200 direction=column align-items=center] {
		[box width=50 height=30] {}
		[box width=80 height=50] {}
		[box width=60 height=40] {}
	}
	)");
	doc.computeLayout(Vec2(800, 600));
	ui::Element* panel = doc.getElement(0);
	ui::Element* btn1 = doc.getElement(1);
	ui::Element* btn2 = doc.getElement(2);
	ui::Element* btn3 = doc.getElement(3);

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
	[box width=400 height=200 direction=column align-items=start] {
		[box width=50 height=30] {}
		[box width=80 height=40] {}
	}
	)");
	doc.computeLayout(Vec2(800, 600));
	ui::Element* panel = doc.getElement(0);
	ui::Element* btn1 = doc.getElement(1);
	ui::Element* btn2 = doc.getElement(2);

	ASSERT_FLOAT_EQ(0.0f, btn1->position.x);
	ASSERT_FLOAT_EQ(0.0f, btn2->position.x);

	return true;
}

bool testAlignItemsEndColumn() {
	// Citation: layout.md - Off-axis alignment
	// "- `end`: Elements are aligned to the end of the off-axis (bottom for row, right for column)."
	MockDocument doc;
	ASSERT_PARSE(doc, R"(
	[box width=400 height=200 direction=column align-items=end] {
		[box width=50 height=30] {}
		[box width=80 height=50] {}
		[box width=60 height=40] {}
	}
	)");
	doc.computeLayout(Vec2(800, 600));
	ui::Element* panel = doc.getElement(0);
	ui::Element* btn1 = doc.getElement(1);
	ui::Element* btn2 = doc.getElement(2);
	ui::Element* btn3 = doc.getElement(3);

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
	[box width=400 height=200 direction=column align-items=stretch] {
		[box height=30] {}
		[box height=50] {}
		[box width=60 height=40] {}
	}
	)");
	doc.computeLayout(Vec2(800, 600));
	ui::Element* panel = doc.getElement(0);
	ui::Element* btn1 = doc.getElement(1);
	ui::Element* btn2 = doc.getElement(2);
	ui::Element* btn3 = doc.getElement(3);

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
	MockDocument doc;
	ASSERT_PARSE(doc, R"(
	[box width=100 height=200 direction=row wrap=wrap justify-content=center] {
		[box width=50 height=50] {}
		[box width=50 height=50] {}
		[box width=50 height=50] {}
	}
	)");
	doc.computeLayout(Vec2(800, 600));
	ui::Element* panel = doc.getElement(0);
	ui::Element* btn1 = doc.getElement(1);
	ui::Element* btn2 = doc.getElement(2);
	ui::Element* btn3 = doc.getElement(3);

	MockDocument doc2;
	ASSERT_PARSE(doc2, R"(
	[box width=200 height=200 direction=row wrap=wrap justify-content=center] {
		[box width=50 height=30] {}
		[box width=50 height=50] {}
		[box width=50 height=30] {}
	}
	)");
	doc2.computeLayout(Vec2(800, 600));
	ui::Element* panel2 = doc2.getElement(0);
	ui::Element* btn1_2 = doc2.getElement(1);
	ui::Element* btn2_2 = doc2.getElement(2);
	ui::Element* btn3_2 = doc2.getElement(3);

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
	[box width=100 height=200 direction=row wrap=true] {
		[box width=50 height=25] {}
		[box width=50 height=25] {}
		[box width=50 height=25] {}
	}
	)");
	doc.computeLayout(Vec2(800, 600));
	ui::Element* panel = doc.getElement(0);
	ui::Element* child1 = doc.getElement(1);
	ui::Element* child2 = doc.getElement(2);
	ui::Element* child3 = doc.getElement(3);

	ASSERT_EQ(0.0f, child1->position.y);
	ASSERT_EQ(0.0f, child2->position.y);

	ASSERT_EQ(25.0f, child3->position.y);

	return true;
}

bool testLineBreaks() {
	// Citation: layout.md - Line Breaks
	// "Line breaks occur only at block-level elements. Inline elements flow continuously until a block element forces a new line."
	MockFontManager mock;
	ui::Document doc(&mock, getGlobalAllocator());
	ASSERT_PARSE(doc, R"(
		[box width=200 height=100 direction=row font="arial.ttf" font-size=16] {
			First
			[box width=50 height=20] { Block }
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

	ASSERT_EQ(1, text1->lines.size());
	ASSERT_FLOAT_EQ(0.0f, text1->lines[0].pos.x);
	ASSERT_FLOAT_EQ(12.8f, text1->lines[0].pos.y);

	ASSERT_FLOAT_EQ(0.0f, block->position.x);
	ASSERT_FLOAT_EQ(16.0f, block->position.y);

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
		[box width=fit-content height=fit-content direction=row font="arial.ttf" font-size=16] {
			Text
		}
	)");
	doc.computeLayout(Vec2(800, 600));
	ui::Element* parent = doc.getElement(0);
	ui::Element* textElem = doc.getElement(1);
	
	ASSERT_EQ(32.0f, parent->size.x);
	ASSERT_EQ(16.0f, parent->size.y);
	
	return true;
}

bool testTextNoWrapping() {
	// Citation: layout.md - Text
	// "Text flows inline and can wrap to multiple lines when `wrap=true`
	// and the content exceeds the box's available width (after subtracting padding)."

	MockFontManager mock;
	ui::Document doc(&mock, getGlobalAllocator());
	ASSERT_PARSE(doc, R"(
		[box width=100 height=fit-content wrap=false font="arial.ttf" font-size=16] {
			This is a very long text that should not wrap even if it exceeds the box width
		}
	)");
	doc.computeLayout(Vec2(800, 600));
	Span<u32> root_indices = doc.m_roots;
	ASSERT_EQ(1, root_indices.size());
	ui::Element* panel = doc.getElement(root_indices[0]);

	ASSERT_EQ(1, panel->children.size());
	ui::Element* textElem = doc.getElement(panel->children[0]);

	ASSERT_EQ(1, textElem->lines.size());
	ASSERT_TRUE(textElem->size.x > panel->size.x);
	ASSERT_EQ(16.0f, textElem->size.y);
	ASSERT_EQ(0.0f, textElem->lines[0].pos.x);
	ASSERT_EQ(12.8f, textElem->lines[0].pos.y);
	return true;
}

bool testSpanCenteringWithTrailingWhitespace() {\
	// Citation: layout.md -  Whitespace Handling
	// "- Consecutive whitespace characters are collapsed into a single space.
	//- Leading and trailing whitespace is trimmed."

	MockFontManager mock;
	ui::Document doc(&mock, getGlobalAllocator());
	ASSERT_PARSE(doc, R"(
	[box width=200 align=center font="arial.ttf" font-size=16] {
		[span value="Hello   "]
	}
	)");
	doc.computeLayout(Vec2(800, 600));
	Span<u32> root_indices = doc.m_roots;
	ASSERT_EQ(1, root_indices.size());
	ui::Element* panel = doc.getElement(root_indices[0]);
	ASSERT_EQ(1, panel->children.size());
	ui::Element* span = doc.getElement(panel->children[0]);
	ASSERT_EQ(1, span->lines.size());
	ASSERT_FLOAT_EQ(80.0f, span->lines[0].pos.x);
	return true;
}

bool testTextWrapping() {
	// Citation: layout.md - Text
	// "Text flows inline and can wrap to multiple lines when `wrap=true`
	// and the content exceeds the box's available width (after subtracting padding)."
	MockFontManager mock;
	ui::Document doc(&mock, getGlobalAllocator());
	ASSERT_PARSE(doc, R"(
		[box width=120 height=fit-content wrap=true padding=10 font="arial.ttf" font-size=16] {
			This is a very long text that should wrap at the content width minus padding
		}
	)");
	doc.computeLayout(Vec2(800, 600));
	Span<u32> root_indices = doc.m_roots;
	ASSERT_EQ(1, root_indices.size());
	ui::Element* panel = doc.getElement(root_indices[0]);
	ASSERT_EQ(1, panel->children.size());
	ui::Element* textElem = doc.getElement(panel->children[0]);

	ASSERT_TRUE(textElem->lines.size() > 1);
	ASSERT_EQ(10.0f, textElem->lines[0].pos.x);
	ASSERT_EQ(22.8f, textElem->lines[0].pos.y);

	float left = panel->position.x + panel->paddings.left;
	float right = panel->position.x + panel->size.x - panel->paddings.right;
	for (const ui::SpanLine& line : textElem->lines) {
		ASSERT_TRUE(line.pos.x >= left);
		ASSERT_TRUE(line.pos.x + line.width <= right);
	}

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
		[box width=120 height=fit-content wrap=false direction=row font="arial.ttf" font-size=16] {
			[box width=60 height=fit-content] {
				Should wrap, wrap is not inherited and default is true
			}
			[box width=60 height=fit-content wrap=false] {
				This is a long text that should not wrap in this box
			}
		}
	)");
	doc.computeLayout(Vec2(800, 600));
	Span<u32> root_indices = doc.m_roots;
	ASSERT_EQ(1, root_indices.size());
	ui::Element* outer_panel = doc.getElement(root_indices[0]);
	ASSERT_EQ(2, outer_panel->children.size());
	ui::Element* child1 = doc.getElement(outer_panel->children[0]);
	ui::Element* child2 = doc.getElement(outer_panel->children[1]);
	ASSERT_EQ(1, child1->children.size());
	ASSERT_EQ(1, child2->children.size());
	ui::Element* text1 = doc.getElement(child1->children[0]);
	ui::Element* text2 = doc.getElement(child2->children[0]);
	ASSERT_TRUE(text1->lines.size() > 1);
	ASSERT_EQ(1, text2->lines.size());
	
	return true;
}

bool testMultilineStringLayout() {
	// Citation: layout.md - Multiline Strings
	// "Unquoted text spanning multiple lines in markup is treated as a single line"
	MockFontManager mock;
	ui::Document doc(&mock, getGlobalAllocator());
	ASSERT_PARSE(doc, R"(
		[box width=fit-content height=fit-content font="arial.ttf" font-size=16] {
			Line 1
			Line 2
			Line 3
		}
	)");
	doc.computeLayout(Vec2(800, 600));
	Span<u32> root_indices = doc.m_roots;
	ASSERT_EQ(1, root_indices.size());
	ui::Element* panel = doc.getElement(root_indices[0]);

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

bool testDoubleQuotesInText() {
	// Citation: layout.md - Text
	// "Double quotes (`"`) in text content are treated as regular characters and render as expected without any special handling, since text is unquoted in the markup."
	MockFontManager mock;
	ui::Document doc(&mock, getGlobalAllocator());
	ASSERT_PARSE(doc, R"(
		[box width=fit-content height=fit-content font="arial.ttf" font-size=16] {
			"Hello "world""
		}
	)");
	doc.computeLayout(Vec2(800, 600));
	Span<u32> root_indices = doc.m_roots;
	ASSERT_EQ(1, root_indices.size());
	ui::Element* panel = doc.getElement(root_indices[0]);

	ASSERT_EQ(1, panel->children.size());
	ui::Element* textElem = doc.getElement(panel->children[0]);

	ASSERT_FLOAT_EQ(120.0f, textElem->size.x);
	ASSERT_FLOAT_EQ(16.0f, textElem->size.y);

	ASSERT_EQ(1, textElem->lines.size());
	ASSERT_FLOAT_EQ(0.0f, textElem->lines[0].pos.x);
	ASSERT_FLOAT_EQ(12.8f, textElem->lines[0].pos.y);

	ASSERT_FLOAT_EQ(120.0f, panel->size.x);
	ASSERT_FLOAT_EQ(16.0f, panel->size.y);

	return true;
}

bool testTextHorizontalRendering() {
	// Citation: layout.md - Inline Flow
	// "Text elements (`span`s and unquoted text) are arranged horizontally in inline flow, regardless of the container's `direction`."
	MockFontManager mock;
	ui::Document doc(&mock, getGlobalAllocator());
	ASSERT_PARSE(doc, R"(
		[box direction=column font="arial.ttf" font-size=16] {
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

	ASSERT_FLOAT_EQ(text1->position.y, text2->position.y);

	return true;
}

bool testBaselineAlignment() {
	// Citation: layout.md - Baseline Alignment in Inline Flow
	// "For visual consistency, inline elements align to a baseline:
	// - Text baselines are determined by font metrics.
	// - The line's baseline is set by the tallest text element."
	MockFontManager mock;
	ui::Document doc(&mock, getGlobalAllocator());
	ASSERT_PARSE(doc, R"(
		[box direction=row font="arial.ttf" font-size=16] {
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

	ASSERT_EQ(1, smallText->lines.size());
	ASSERT_EQ(1, normalText->lines.size());
	ASSERT_EQ(1, largeText->lines.size());
	float smallBaseline = smallText->lines[0].pos.y;
	float normalBaseline = normalText->lines[0].pos.y;
	float largeBaseline = largeText->lines[0].pos.y;

	ASSERT_FLOAT_EQ(normalBaseline, smallBaseline);
	ASSERT_FLOAT_EQ(normalBaseline, largeBaseline);

	return true;

	float expectedBaselineY = 0.0f;
	ASSERT_FLOAT_EQ(expectedBaselineY, smallText->position.y + smallText->size.y);
	ASSERT_FLOAT_EQ(expectedBaselineY, normalText->position.y + normalText->size.y);
	ASSERT_FLOAT_EQ(expectedBaselineY, largeText->position.y + largeText->size.y);

	ASSERT_FLOAT_EQ(0.0f, smallText->position.x);
	ASSERT_FLOAT_EQ(smallText->size.x, normalText->position.x);
	ASSERT_FLOAT_EQ(smallText->size.x + normalText->size.x, largeText->position.x);

	return true;
}

bool testBaselineAlignmentWithWrapping() {
	// Citation: layout.md - Baseline Alignment in Inline Flow
	// "For visual consistency, inline elements align to a baseline:
	// - Text baselines are determined by font metrics.
	// - The line's baseline is set by the tallest text element."
	MockFontManager mock;
	ui::Document doc(&mock, getGlobalAllocator());
	ASSERT_PARSE(doc, R"(
		[box direction=row wrap=true width=200 font="arial.ttf" font-size=16] {
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

	ASSERT_TRUE(normalText->lines.size() > 1);
	ASSERT_EQ(3, normalText->lines.size());
	ASSERT_EQ(1, largeText->lines.size());

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
		[box width=400 align=center font="arial.ttf" font-size=16 direction=row] {
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

	float totalWidth = span1->size.x + span2->size.x + span3->size.x;
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

	ASSERT_FLOAT_EQ(expectedStartX, span1->lines[0].pos.x);
	ASSERT_FLOAT_EQ(expectedStartX + span1->lines[0].width, span2->lines[0].pos.x);
	ASSERT_FLOAT_EQ(expectedStartX + span1->lines[0].width + span2->lines[0].width, span3->lines[0].pos.x);
	
	return true;
}

bool testAlignRightMultipleSpans() {
	// Citation: layout.md - Text Alignment
	// "Text alignment controls how text is positioned horizontally within its container. The `align` attribute can be set to `left`, `center`, or `right`, with `left` as the default."
	MockFontManager mock;
	ui::Document doc(&mock, getGlobalAllocator());
	ASSERT_PARSE(doc, R"(
		[box width=400 align=right font="arial.ttf" font-size=16 direction=row] {
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

	float totalWidth = span1->size.x + span2->size.x + span3->size.x;
	float expectedStartX = 400.0f - totalWidth;

	ASSERT_EQ(1, span1->lines.size());
	ASSERT_EQ(1, span2->lines.size());
	ASSERT_EQ(1, span3->lines.size());
	ASSERT_FLOAT_EQ(expectedStartX, span1->lines[0].pos.x);
	ASSERT_FLOAT_EQ(expectedStartX + span1->lines[0].width, span2->lines[0].pos.x);
	ASSERT_FLOAT_EQ(expectedStartX + span1->lines[0].width + span2->lines[0].width, span3->lines[0].pos.x);

	return true;
}

bool testAlignCenter() {
	// Citation: layout.md - Text Alignment
	// "Text alignment controls how text is positioned horizontally within its container. The `align` attribute can be set to `left`, `center`, or `right`, with `left` as the default."
	MockFontManager mock;
	ui::Document doc(&mock, getGlobalAllocator());
	ASSERT_PARSE(doc, R"(
		[box width=400 align=center font="arial.ttf" font-size=16] {
			Centered Text
		}
	)");
	doc.computeLayout(Vec2(800, 600));
	Span<u32> root_indices = doc.m_roots;
	ASSERT_EQ(1, root_indices.size());
	ui::Element* panel = doc.getElement(root_indices[0]);
	ASSERT_EQ(1, panel->children.size());

	ui::Element* text = doc.getElement(panel->children[0]);

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
		[box width=400 align=right font="arial.ttf" font-size=16] {
			Right Aligned Text
		}
	)");
	doc.computeLayout(Vec2(800, 600));
	Span<u32> root_indices = doc.m_roots;
	ASSERT_EQ(1, root_indices.size());
	ui::Element* panel = doc.getElement(root_indices[0]);
	ASSERT_EQ(1, panel->children.size());

	ui::Element* text = doc.getElement(panel->children[0]);

	float expectedX = 400.0f - text->size.x;
	ASSERT_EQ(1, text->lines.size());
	ASSERT_FLOAT_EQ(expectedX, text->lines[0].pos.x);

	return true;
}

bool testPanelWithInlineSpan() {
	// Citation: layout.md - Whitespace Handling
	// "Whitespace in text content is normalized similarly to HTML:
	// - Newlines (`\n`), carriage returns (`\r`), tabs, and spaces are treated as whitespace.
	// - Consecutive whitespace characters are collapsed into a single space.
	// - Leading and trailing whitespace is trimmed."
	MockDocument doc;
	ASSERT_PARSE(doc, R"(
		[box direction=row align=center font-size=40 wrap=true width=100% font="arial.ttf"] {
			Welcome to [span value=" Lumix " font-size=60] Demo
		}
	)");
	doc.computeLayout(Vec2(800, 600));
	Span<u32> root_indices = doc.m_roots;
	ASSERT_EQ(1, root_indices.size());
	ui::Element* panel = doc.getElement(root_indices[0]);

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
	[box direction=column font="arial.ttf" width=20% height=100%] {
		Header
		[box grow=1] { Content }
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
	RUN_TEST(testGrowVertical);
	RUN_TEST(testGrowVerticalMiddle);
	RUN_TEST(testGrowSingleChild);
	RUN_TEST(testGrowWithMargin);
	RUN_TEST(testGrowWithPadding);
	RUN_TEST(testHorizontalMarginCollapse);
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
	RUN_TEST(testLayoutWithMargins);
	RUN_TEST(testLineBreaks);
	RUN_TEST(testMarginPadding);
	RUN_TEST(testHorizontalSideSpecificMarginPadding);
	RUN_TEST(testVerticalSideSpecificMarginPadding);
	RUN_TEST(testSideSpecificShorthandPrecedence);
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
