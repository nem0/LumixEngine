# UI Layout

## Table of Contents

- [Text](#text)
- [Element Sizing](#element-sizing)
  - [Units](#units)
  - [Fit-Content](#fit-content)
- [Element Positioning](#element-positioning)
  - [Positioning Algorithm](#positioning-algorithm)
  - [Justification](#justification)
  - [Off-axis Alignment](#off-axis-alignment)
  - [Wrapping](#wrapping)
  - [Margins and Padding](#margins-and-padding)
    - [Margins](#margins)
    - [Padding](#padding)
    - [Margin-Padding Interaction](#margin-padding-interaction)
    - [Positioning Calculations](#positioning-calculations)
- [Z-Order (Implicit Stacking)](#z-order-implicit-stacking)

## Text

### Inline Text Nodes (Quoted Strings)
Quoted strings create inline text that flows horizontally with sibling elements, wrapping at container edges.

```css
panel {
    "Hello, "
    "World!"
    "."
}
```

**Result**: `Hello, World!.` (flows as single line)

### Styled Text
For styling, use `panel` elements.

```css
panel {
    panel color=#00f {"Hello, "}
    panel font-size=1.2em color=#f00 { "World!" }
    panel font-size=0.8em { "." }
}
```

### Mixed Content Flow
Text nodes flow inline with blocks until they hit a block element.

```css
panel direction=row {
    "Label: "
    panel width=100 { input }
    " (optional)"
}
```

**Result**: `Label: [input] (optional)`

### Text Properties

| Property | Description | Default / Values |
|----------|-------------|------------------|
| `value` | The text content to display | `""` |
| `align` | Text alignment | `left` |
| `font-size` | Text size | `12` |
| `font` | Font file path | (no default) |
| `color` | Text color | `"#000000"` |

**Text nodes flow inline** with blocks; `text` elements are blocks with fit-content sizing.


## Element Sizing

Each UI element has `width` and `height` attributes that control its size, known as dimensions. Set them explicitly for a fixed size; otherwise, they default to `fit-content`, sizing the element to its content. Root elements behave like they are children of a panel that covers the whole screen with 0 padding.

Example
```css
panel width=50% height=3em { ... }
```

### Units

Dimensions support these units:

- **Pixels** (default): Fixed pixel values. E.g., `width=200` for 200 pixels.

- **em**: Scales with element's font size. E.g., `height=2em` for twice the font height.

- **%**: Percentage of parent (or viewport for roots). E.g., `width=50%` for half the parent's width.

- **fit-content**: Auto-size to content. For panels, sums child sizes. E.g., `width=fit-content`.

Mix units freely, e.g., `width=50% height=2em`.

### Fit-Content

```js
function fitContentSize(container):
    size = 0
    for child in container.children:
        size += child.size + child.margin
    size += container.padding
    return size
```

When using `fit-content` sizing, margins are included in the total size calculation for containers, ensuring spacing between children is preserved. Padding is added to the computed fit-content size, so the container's total size is the sum of child sizes (plus margins) and its own padding.

#### With Percentage Units

When a parent container uses `fit-content` sizing and child elements specify dimensions in percentage units (%), the layout algorithm faces an edge case: percentage values are relative to the parent's size, but the parent's size is being determined based on the children's sizes.

In this implementation, children are measured assuming 0 available space. For elements with percentage-based dimensions, percentages resolve to 0 since 0% of any size is 0.

## Element Positioning

The layout system positions elements within containers using this algorithm:

### Positioning Algorithm

```js
function layoutContainer(container):
    // 1. Determine container size
    if container.width == 'fit-content' or container.height == 'fit-content':
        // Measure children with 0 available space
        for each child in container.children:
            child.size = calculateSize(child, 0)
        
        // Calculate container size as sum of children
        containerSize = sumChildSizes(container.children, container.direction)
    else:
        // Use fixed or inherited size
        containerSize = getSize(container)
        
        // Measure children with container constraints
        for each child in container.children:
            child.size = calculateSize(child, containerSize)

    // 2. Arrange by direction with wrapping
    mainAxis = (container.direction == 'row') ? 'horizontal' : 'vertical'
    if container.wrap == 'wrap':
        // Wrap children into multiple lines/columns
        lines = wrapChildrenIntoLines(container.children, mainAxis, containerSize)
        for each line in lines:
            positionChildrenSequentially(line.children, mainAxis, container)
            justifyChildren(line.children, container.justifyContent, mainAxis, line.size)
    else:
        positionChildrenSequentially(container.children, mainAxis, container)
        justifyChildren(container.children, container.justifyContent, mainAxis, containerSize)
        // For 'clip', overflowing content is not rendered

    // 3. Incorporate margins and padding
    applyMarginsAndPadding(container.children, container)

    // 4. Calculate positions
    for each child in container.children:
        child.absolutePosition = computeAbsolutePosition(child, container)
```

### Justification

```js
function positionChildrenSequentially(children, mainAxis, container):
    // Start positioning from container's padding
    if mainAxis == 'horizontal':
        currentPosition = container.paddingLeft
    else:  // vertical
        currentPosition = container.paddingTop

    for each child in children:
        // Position child at current position plus its margin
        if mainAxis == 'horizontal':
            child.relativeX = currentPosition + child.marginLeft
            child.relativeY = container.paddingTop + child.marginTop
            // Advance position by child's size plus margin
            currentPosition += child.width + child.marginLeft + child.marginRight
        else:  // vertical
            child.relativeX = container.paddingLeft + child.marginLeft
            child.relativeY = currentPosition + child.marginTop
            currentPosition += child.height + child.marginTop + child.marginBottom
```

The `justify-content` property then adjusts these positions to achieve the desired distribution. Options include:

- **`start`**: Elements are placed sequentially starting from the container's start edge plus padding. Each subsequent element is positioned immediately after the previous, accounting for size and margin.
  ```
  +---------------------------------+
  |[elem1][elem2][elem3]            |
  +---------------------------------+
  ```

- **`center`**: Elements are centered as a group. The total combined size (including margins) is calculated, and the group is offset by (container_size - total_combined_size) / 2.
  ```
  +---------------------------------+
  |      [elem1][elem2][elem3]      |
  +---------------------------------+
  ```

- **`end`**: Elements are placed starting from the container's end edge minus padding. Laid out in reverse order, with each preceding element before the next.
  ```
  +---------------------------------+
  |          [elem1][elem2][elem3]  |
  +---------------------------------+
  ```

- **`space-between`**: Elements are evenly distributed with the first at start and last at end. Remaining space (container_size - total_sizes - margins - padding) is divided equally among n-1 gaps. With a single child, `space-between` behaves like `start`.
  ```
  +---------------------------------+
  |[elem1]      [elem2]      [elem3]|
  +---------------------------------+
  ```

- **`space-around`**: Equal space is added around each element. Total space is divided equally around n elements, with each getting space / (2n) on both sides. With a single child, `space-around` behaves like `center`.
  ```
  +---------------------------------+
  |   [elem1]   [elem2]   [elem3]   |
  +---------------------------------+

### Off-axis alignment

Off-axis alignment controls how child elements are positioned along the axis perpendicular to the container's main axis. For `direction=row`, the off-axis is vertical; for `direction=column`, it's horizontal.

The `align-items` property specifies alignment for children.

Options include:

- **`start`**: Elements are aligned to the start of the off-axis (top for row, left for column).
  ```
  Container (direction=row, align-items=start):
  +----------------+
  |[elem1] [elem2] |
  |                |
  +----------------+
  ```

- **`center`**: Elements are centered along the off-axis.
  ```
  Container (direction=row, align-items=center):
  +-----------------+
  |                 |
  | [elem1] [elem2] |
  |                 |
  +-----------------+
  ```

- **`end`**: Elements are aligned to the end of the off-axis (bottom for row, right for column).
  ```
  Container (direction=row, align-items=end):
  +-----------------+
  |                 |
  | [elem1] [elem2] |
  +-----------------+
  ```

- **`stretch`** (fill): Elements stretch to fill the available space along the off-axis. This is the default behavior.
  ```
  Container (direction=row, align-items=stretch):
  +-----------------+
  | +-----+ +-----+ |
  | |elem1| |elem2| |
  | +-----+ +-----+ |
  +-----------------+
  ```

When `align-items=stretch`, elements expand to match the container's size in the off-axis direction, minus padding and margins.

### Wrapping

The `wrap` property controls whether child elements wrap to new lines or columns when they exceed the container's size along the main axis. When `wrap=true`, elements that don't fit on the current line move to the next line (for `direction=row`) or next column (for `direction=column`).

- **`wrap=false`** (default): Elements stay on a single line/column, potentially overflowing the container.
  ```
  Container (direction=row, wrap=false):
  +----------------------+
  |[elem1][elem2][elem3] | <- overflows elem4,5...
  +----------------------+
  ```

- **`wrap=true`**: Elements wrap to new lines when they don't fit.
  ```
  Container (direction=row, wrap=true):
  +----------------------+
  |[elem1][elem2][elem3] |
  |[elem4][elem5]        |
  +----------------------+
  ```

Justification is applied to each row/column separately.

### Margins and Padding

Margins and padding add space around and inside elements to control layout and appearance.

```
+-----------------------------+
|          margin             |
|  +-----------------------+  | <- total size
|  |      padding          |  |
|  |  +-----------------+  |  |
|  |  |   content       |  |  |
|  |  | (width x height)|  |  |
|  |  +-----------------+  |  |
|  |      padding          |  |
|  +-----------------------+  | <- total size
|          margin             |
+-----------------------------+
```

- Total width = content width + left padding + right padding
- Total height = content height + top padding + bottom padding
- Margins are external and don't affect total size.

#### Margins
Margins provide external spacing between elements and their containers, affecting position but not size.

- Added outside the element's border.
- Example: `margin=10` creates 10px space around the element.

##### Margin Collapsing
Adjacent margins combine into the larger value to prevent excessive gaps.

- Between siblings: uses the maximum of adjacent margins.
- Does not combine with padding.
- Both vertical and horizontal margins collapse.

```
Before collapse (margins would add up to 30px space):
+--------+       +--------+
| Elem1  | = = = | Elem2  |
+--------+       +--------+
        10px + 20px

After collapse (maximum margin used, 20px space):
+--------+     +--------+
| Elem1  | = = | Elem2  |
+--------+     +--------+
          20px
```

#### Padding
Padding adds internal space within the element's border, expanding its total size.

- Increases the content area.
- Example: `padding=10` adds 10px inside the border.

#### Margin-Padding Interaction
The child's margin provides spacing from the parent's content edge (inside the padding), positioning elements within the parent's padded area.

- Margins are external to the child element.
- Padding defines the parent's inner content boundary.

#### Positioning Calculations
Child element position relative to parent:
- x = parent.x + parent.padding_left + child.margin_left
- y = parent.y + parent.padding_top + child.margin_top

## Z-Order (Implicit Stacking)

In UI systems, z-order determines the visual layering of elements, controlling which elements appear on top of others. Unlike many UI frameworks that provide an explicit `z-index` property, this system uses implicit stacking based on the widget hierarchy and declaration order.

### Stacking Rules

Stacking is determined by the following implicit rules:

1. **Parent-Child Relationship**: Parents are rendered before their children, meaning children are drawn on top of their parents. This creates a natural layering where nested elements appear above their containers.

2. **Sibling Order**: Among siblings within the same parent, elements declared or added later in the markup or at runtime appear on top of those declared earlier. This follows a "last-in, top-most" principle.

3. **Tree Depth**: Deeper elements in the widget tree (grandchildren, etc.) naturally stack above shallower elements due to the parent-child rendering order.

### Rendering and Input Handling

- **Rendering Order**: Elements are drawn in the order defined by the stacking rules, with later elements compositing over earlier ones.
- **Hit-Testing**: Input events (mouse clicks, touches) are dispatched to the top-most visible element under the pointer. The system traverses the widget tree in reverse stacking order, testing elements from top to bottom until a hit is found.
- **Event Propagation**: Events bubble up through the hierarchy, but only the top-most element receives the initial event.

### Manipulating Stacking

To change an element's stacking position:

- **Reorder Siblings**: Move a widget to a different position in its parent's child list. Widgets at the end of the list appear on top.
- **Reparent Elements**: Attach a widget to a different parent to change its stacking context.
- **Runtime Manipulation**: Use API calls to reorder children dynamically, such as bringing a widget to the front by moving it to the end of its parent's children.
