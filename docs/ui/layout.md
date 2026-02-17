# UI Layout

## Table of Contents

- [Text](#text)
  - [Inline Flow](#inline-flow)
  - [Text Alignment](#text-alignment)
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

Text paragraphs are created using `span` elements with a `value` attribute or quoted strings inside a `panel`. Text flows inline within the panel and wraps to multiple lines when the unwrapped width exceeds the available panel width (minus padding) and `wrap=true`.

```css
panel {
  "Some text"
  span color=#ff0000 value="Some other, red text"
}
```

### Inline Flow

Inline flow arranges **separate inline elements** (`span`s, text strings) sequentially along the container's main axis:
- `direction="row"` -> spans side-by-side -> `"A" "B"` becomes `A B`
- `direction="column"` -> spans stacked -> `"A"` above `"B"`

Text strings always render **horizontally** (left-to-right), regardless of `direction`.

**Breaks occur** only from **block elements**.

**Row direction example**:

```css
panel direction="row" {
  "First line text flows here"
  panel width=100 height=50 "Block panel causes line break"
  span "and text continues on new line below"
}
```

Visual result:

```
+------------------------------+
| First line text flows here   |
| +---------------------+      |
| | Block panel causes  |      |
| | line break          |      |
| +---------------------+      |
| and text continues on new    |
| line below                   |
+------------------------------+
```

**Column direction example** (note: `"Text above"` renders horizontally within its inline slot):

```css
panel direction="column" {
  span "Text above"
  panel width=100 height=50 bgColor="red" { }
  span "Text below"
}
```

Visual result:

```
+-------+
| Text  |
| above |  <- horizontal text
+-------+
|       |
|       | <- red panel
|       |
+-------+
| Text  |
| below |  <- horizontal text
+-------+
```

This approach treats inline elements as a unified flow, with blocks serving as natural separators.

### Text Alignment

Text alignment controls how text is positioned horizontally within its container. The `text-align` attribute can be set to `left`, `center`, or `right`, with `left` as the default.

```css
panel text-align="center" {
  "Centered text"
}
```

For multi-line text, each line is aligned independently according to the `text-align` value.

Text alignment is inherited by child elements that contain text.

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

#### With Text Elements

For text elements (inline text nodes or `text` blocks), measuring with 0 available space means assuming infinite width: text does not wrap and is treated as a single line. The width is calculated as the full rendered width of the text string in pixels, and height is based on the font size (typically the line height). This prevents collapsing to zero size while avoiding the need for a predefined width constraint.

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
        
        // Calculate container size as sum of children plus padding
        containerSize = sumChildSizes(container.children, container.direction) + container.padding
        if container.width == 'fit-content':
            container.width = containerSize.width
        if container.height == 'fit-content':
            container.height = containerSize.height
    else:
        // Use fixed or inherited size
        containerSize = getSize(container)
        
        // Measure children with container constraints
        for each child in container.children:
            child.size = calculateSize(child, containerSize)

    // 2. Arrange by direction with wrapping
    mainAxis = (container.direction == 'row') ? 'horizontal' : 'vertical'
    crossAxis = (mainAxis == 'horizontal') ? 'vertical' : 'horizontal'
    if container.wrap == 'wrap':
        // Wrap children into lines (each line is an object with children, size, and crossPos)
        lines = wrapChildrenIntoLines(container.children, mainAxis, containerSize[mainAxis])
        
        // Position lines along cross-axis starting from container padding
        currentCrossPos = container.padding[crossAxis == 'vertical' ? 'top' : 'left']
        for each line in lines:
            line.crossPos = currentCrossPos
            // Position children sequentially within the line along main-axis
            positionChildrenSequentially(line.children, mainAxis, 0)  // No padding for lines
            // Justify children within the line
            justifyChildren(line.children, container.justifyContent, mainAxis, containerSize[mainAxis])
            // Align children off-axis within the line
            alignChildrenOffAxis(line.children, container.alignItems, crossAxis, line.size[crossAxis])
            // Advance cross position by line size
            currentCrossPos += line.size[crossAxis]
    else:
        // No wrapping: treat all children as one line
        positionChildrenSequentially(container.children, mainAxis, container.padding[mainAxis == 'horizontal' ? 'left' : 'top'])
        justifyChildren(container.children, container.justifyContent, mainAxis, containerSize[mainAxis])
        alignChildrenOffAxis(container.children, container.alignItems, crossAxis, containerSize[crossAxis])
        // For 'clip', overflowing content is not rendered

    // 3. Incorporate margins and padding (adjust positions for margins if not already handled)
    applyMarginsAndPadding(container.children, container)

    // 4. Calculate absolute positions
    for each child in container.children:
        child.absolutePosition = computeAbsolutePosition(child, container)
```

### Justification

```js
function positionChildrenSequentially(children, mainAxis, paddingStart, paddingOff):
    // Start positioning from given padding
    currentPosition = paddingStart
    previousMargin = 0

    for each child in children:
        // Calculate gap with margin collapsing along main axis
        if mainAxis == 'horizontal':
            gap = max(previousMargin, child.marginLeft)
            child.relativeX = currentPosition + gap
            child.relativeY = paddingOff + child.marginTop  // Off-axis margin (no collapsing assumed)
            currentPosition += gap + child.width
            previousMargin = child.marginRight
        else:  // vertical
            gap = max(previousMargin, child.marginTop)
            child.relativeX = paddingOff + child.marginLeft  // Off-axis margin
            child.relativeY = currentPosition + gap
            currentPosition += gap + child.height
            previousMargin = child.marginBottom

function justifyChildren(children, justifyContent, mainAxis, availableSize):
    totalSize = 0
    for each child in children:
        totalSize += child.size[mainAxis == 'horizontal' ? 'width' : 'height']
        // Include margins in totalSize for spacing
        if mainAxis == 'horizontal':
            totalSize += child.marginLeft + child.marginRight
        else:
            totalSize += child.marginTop + child.marginBottom
    // Subtract margins already accounted for in gaps (simplified: assume first/last margins not double-counted)
    remainingSpace = availableSize - totalSize

    if justifyContent == 'start':
        // No change needed
        pass
    elif justifyContent == 'center':
        offset = remainingSpace / 2
        for each child:
            if mainAxis == 'horizontal':
                child.relativeX += offset
            else:
                child.relativeY += offset
    elif justifyContent == 'end':
        offset = remainingSpace
        for each child:
            if mainAxis == 'horizontal':
                child.relativeX += offset
            else:
                child.relativeY += offset
    elif justifyContent == 'space-between' and len(children) > 1:
        gap = remainingSpace / (len(children) - 1)
        currentOffset = 0
        for i in 1 to len(children)-1:
            currentOffset += gap
            if mainAxis == 'horizontal':
                children[i].relativeX += currentOffset
            else:
                children[i].relativeY += currentOffset
    elif justifyContent == 'space-around':
        gap = remainingSpace / len(children)
        currentOffset = gap / 2
        for each child:
            if mainAxis == 'horizontal':
                child.relativeX += currentOffset
            else:
                child.relativeY += currentOffset
            currentOffset += gap
```

The `justify-content` property adjusts positions along the main axis to achieve the desired distribution. Options include:

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

```js
function alignChildrenOffAxis(children, alignItems, crossAxis, availableSize):
    for each child in children:
        if alignItems == 'start':
            // No change (already at start)
            pass
        elif alignItems == 'center':
            offset = (availableSize - child.size[crossAxis == 'vertical' ? 'height' : 'width']) / 2
            if crossAxis == 'vertical':
                child.relativeY += offset
            else:
                child.relativeX += offset
        elif alignItems == 'end':
            offset = availableSize - child.size[crossAxis == 'vertical' ? 'height' : 'width']
            if crossAxis == 'vertical':
                child.relativeY += offset
            else:
                child.relativeX += offset
        elif alignItems == 'stretch':
            if crossAxis == 'vertical':
                child.height = availableSize - child.marginTop - child.marginBottom
            else:
                child.width = availableSize - child.marginLeft - child.marginRight
```

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

When `wrap=true` creates multiple lines (or columns), and the container's size along the cross-axis is larger than the combined size of all lines, the lines are distributed starting from the container's start edge along the cross-axis. This means lines bunch at the top (for `direction=row`) or left (for `direction=column`), with any extra space remaining unused.

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
Child element position relative to parent (for sequential layout with margin collapsing):
- x = parent.x + parent.padding_left + collapsed_margin_left
- y = parent.y + parent.padding_top + collapsed_margin_top

Where `collapsed_margin_left` is the maximum of the child's left margin and the previous sibling's right margin (or just the child's left margin for the first child). Similarly for vertical.

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
