# UI Layout

## Table of Contents


- [Layout Algorithm Overview](#layout-algorithm-overview)
- [Text](#text)
  - [Whitespace Handling](#whitespace-handling)
  - [Inline Flow and Spacing](#inline-flow-and-spacing)
  - [Text Wrapping](#text-wrapping)
  - [Line Breaks](#line-breaks)
  - [Text Alignment](#text-alignment)
- [Element Sizing](#element-sizing)
  - [Units](#units)
  - [Fit-Content](#fit-content)
  - [Grow](#grow)
- [Element Positioning](#element-positioning)
  - [Positioning Algorithm](#positioning-algorithm)
  - [Justification](#justification)
  - [Off-axis alignment](#off-axis-alignment)
  - [Wrapping](#wrapping)
  - [Margins and Padding](#margins-and-padding)
    - [Margins](#margins)
    - [Padding](#padding)
    - [Margin-Padding Interaction](#margin-padding-interaction)
    - [Positioning Calculations](#positioning-calculations)
- [Z-Order (Implicit Stacking)](#z-order-implicit-stacking)

## Layout Algorithm Overview

| Index | Step | What Happens | Computed / Updated Properties |
| --- | --- | --- | --- |
| 1.1. | Base widths (top-down) | Resolve explicit width-like units and measure unwrapped text for spans | `width` (pixels/`em`, `%` for known-width parents), horizontal `margin-left`/`margin-right`, horizontal `padding-left`/`padding-right`, text intrinsic width for spans (unwrapped) |
| 1.2. | Fit-content widths (bottom-up) | Infer container widths from children | `width` for `fit-content` parents (row: sum of child widths + margins + padding; column: max child width + margins + padding) |
| 1.3. | Form lines | Split children into lines/columns when `wrap=true` | Per-container `lines` (child index ranges per line), per-line max cross size seed |
| 1.4. | Compute parent-relative widths - grow, %, stretch (top-down, per line) | Share remaining main-axis space among `grow>0` children and resolve percentage-based widths using final parent sizes | Updated child `width` on each line; respects horizontal padding and collapsed margins; only along main axis (`direction=row`). Also stretch children if parent is `align-items=stretch direction=column`. Percentage widths resolved using finalized parent width. |
| 2. | Wrap text | Break span text into visual lines and place them horizontally | Per-span `lines` (with positions), text `height` from wrapped lines, per-line horizontal offset using `align` (`left`/`center`/`right`) |
| 3.1. | Base heights (top-down) | Resolve explicit height-like units | `height` (pixels/%, `em`), vertical `margin-top`/`margin-bottom`, vertical `padding-top`/`padding-bottom` |
| 3.2. | Fit-content heights (bottom-up) | Infer container heights from children | `height` for `fit-content` parents (column: sum of child heights + margins + padding; row: max child height + margins + padding) |
| 3.3. | Compute parent-relative heights | Share remaining main-axis space among `grow>0` along height | Updated `height` where the main axis is vertical (`direction=column`); may stretch cross-axis when `align-items=stretch` is in effect |
| 4. | Compute positions | Final placement along main and cross axes | `position.x`, `position.y`; margin collapsing; `justify-content` (main-axis distribution); `align-items` (cross-axis alignment per line); per-line application when `wrap=true` |

_Note: The implementation may reorder the steps internally, but the behavior must match the sequence described above._

## Text

Text content in UI panels is created using `span` elements with a `value` attribute or unquoted text directly inside a panel block. Text flows inline and can wrap to multiple lines when `wrap=true` and the content exceeds the panel's available width (after subtracting padding).

```css
[panel] {
  Some unquoted text
  [span color=#ff0000 value="Styled text"]
}
```

### Whitespace Handling

Whitespace in text content is normalized similarly to HTML:
- Newlines (`\n`), carriage returns (`\r`), tabs, and spaces are treated as whitespace.
- Consecutive whitespace characters are collapsed into a single space.
- Leading and trailing whitespace is trimmed.

Unquoted text spanning multiple lines in markup is treated as a single line:

```css
[panel] {
  This is multiline
  text in markup
}
```

Renders as: `This is multiline text in markup`

### Inline Flow and Spacing

Text elements (`span`s and unquoted text) are arranged horizontally in inline flow, regardless of the container's `direction`:
- Text always renders left-to-right.
- No space is added between adjacent spans; they are placed contiguously.
- To include spaces, they must be part of the text content.

Examples:
- `[span value="Hello"] [span value="world"]` renders as `Helloworld`
- `[span value="Hello "] [span value="world"]` renders as `Hello world`

#### Baseline Alignment

For visual consistency, inline elements align to a baseline:
- Text baselines are determined by font metrics.
- Non-text elements (e.g., icons) align to their bottom edge.
- The line's baseline is set by the tallest text element.

### Text Wrapping

When `wrap=true`, text wraps to new lines using word boundaries:
- Words are split at spaces, and lines break when a word doesn't fit.
- Spaces are preserved between words on the same line.
- Wrapping occurs at the panel's content width (width minus padding).

### Line Breaks

Line breaks occur only at block-level elements. Inline elements flow continuously until a block element forces a new line.

**Row direction example**:

```css
[panel direction="row"] {
  First line text flows here
  [panel width=100 height=50] { Block panel causes line break }
  [span value="and text continues on new line below"]
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
[panel direction="column"] {
  [span value="Text above"]
  [panel width=100 height=50 bgColor="red"] { }
  [span value="Text below"]
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

Text alignment controls how text is positioned horizontally within its container. The `align` attribute can be set to `left`, `center`, or `right`, with `left` as the default.

```css
[panel align=center] {
  "Centered text"
}
```

For multi-line text, each line is aligned independently according to the `align` value.

Text alignment is inherited by child elements that contain text.

## Element Sizing

Each UI element has `width` and `height` attributes that control its size, known as dimensions. Set them explicitly for a fixed size; otherwise, they default to `fit-content`, sizing the element to its content. Root elements behave like they are children of a panel that covers the whole screen with 0 padding and `direction=column`.

Example
```css
[panel width=50% height=3em] { ... }
```

### Units

Dimensions support these units:

- **Pixels** (default): Fixed pixel values. E.g., `width=200` for 200 pixels.

- **em**: Scales with element's font size. E.g., `height=2em` for twice the font height.

- **%**: Percentage of parent (or viewport for roots). E.g., `width=50%` for half the parent's width.

- **fit-content**: Auto-size to content. For panels, sums child sizes. E.g., `width=fit-content`.

Mix units freely, e.g., `width=50% height=2em`.

### Fit-Content

When using `fit-content` sizing, margins are included in the total size calculation for containers, ensuring spacing between children is preserved. Padding is added to the computed fit-content size, so the container's total size is the sum of child sizes (plus margins) and its own padding.

### Grow

The `grow` attribute controls how an element expands to fill available space in its parent container along the main axis, similar to CSS `flex-grow`. It is a numeric weight (default `0`, meaning no growing). Elements with a non-zero `grow` value share the remaining space in proportion to their weights after all fixed-size children have been measured.

```css
[panel direction="row" width=350] {
  [panel width=100] { Fixed width }
  [panel grow=1] { Grows to fill the rest (250px) }
}
```

Multiple growing elements split the remaining space proportionally:

```css
[panel direction="row" width=400] {
  [panel width=100] { Fixed (100px) }
  [panel grow=2] { Gets 2/3 of remaining 300px = 200px }
  [panel grow=1] { Gets 1/3 of remaining 300px = 100px }
}
```

Left-fill-right (the classic toolbar pattern) works correctly because growing is computed in a two-pass manner — fixed children are sized first, then remaining space is distributed among all `grow` children:

```css
[panel direction="row" width=400] {
  [panel width=100] { Left }
  [panel grow=1] { Middle (200px) }
  [panel width=100] { Right }
}
```

Grow respects the parent's padding: available space is the content area after subtracting padding, and each growing child's margin is also subtracted before distributing.

When `wrap=true`, growing is applied per row (or per column for `direction=column`) independently, distributing the leftover space within each line after fixed-size children on that line are measured.

#### With Percentage Units

Percentage-based dimensions (e.g., `width=100%`) are resolved in **step 1.4**, after the parent's width has been finalized by grow distribution. This ensures that:

- A child with `width=100%` correctly resolves to 100% of the parent's **final** width, even if the parent has `grow=1`
- Percentage widths are deferred during step 1.1 (base widths) and only resolved after all parent sizing is complete
- The layout algorithm avoids the circular dependency where a parent's size depends on children, but children's percentage sizes depend on the parent

**Example**: A parent with `grow=1` and a child with `width=100%`:
1. Step 1.4 grows the parent to fill available space (e.g., 300px)
2. Step 1.4 then resolves the child's `width=100%` to 300px (100% of the parent's final width)

This approach ensures correct sizing in all cases, including nested growing containers with percentage-based children.

#### With Text Elements

For text elements (inline text nodes or `text` blocks), measuring with 0 available space means assuming infinite width: text does not wrap and is treated as a single line. The width is calculated as the full rendered width of the text string in pixels, and height is based on the font size (typically the line height). This prevents collapsing to zero size while avoiding the need for a predefined width constraint.

## Element Positioning

The `direction` attribute controls the primary axis along which child elements are arranged within a container. When set to `row`, children are positioned horizontally from left to right. When set to `column` (the default), children are positioned vertically from top to bottom.

The layout system positions elements within containers using this algorithm:

### Justification

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
  |            [elem1][elem2][elem3]|
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

- **`stretch`**: Elements stretch to fill the available space along the off-axis.
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

Justification and item aligment are applied to each row/column separately.

When `wrap=true` creates multiple lines (or columns), and the container's size along the cross-axis is larger than the combined size of all lines, the lines are distributed starting from the container's start edge along the cross-axis. This means lines bunch at the top (for `direction=row`) or left (for `direction=column`), with any extra space remaining unused.

When `wrap=true`, `align-items` is applied to each wrapped line or column individually, rather than to the entire container. Each line's cross-axis size is determined by the tallest element in that line (for `direction=row`) or the widest element in that column (for `direction=column`), and alignment is calculated relative to that line's size.

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
- Margins do not collapse when there is no sibling, such as with a single child or when the first child has left padding.

```
Before collapse (margins would add up to 30px space):
  +--------+       +--------+
= | Elem1  | = = = | Elem2  | ==
  +--------+       +--------+
           10px + 20px

After collapse (maximum margin used, 20px space):
  +--------+     +--------+
= | Elem1  | = = | Elem2  | ==
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
