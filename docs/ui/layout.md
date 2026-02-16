# UI Layout

## Root Elements

Root elements (elements at the top level of the document) are laid out as if they are children of an implicit panel with `direction=column`. This means they are arranged vertically, one below the other, starting from the top of the screen.

For example, if you have:

```
panel width=200 height=100 { ... }
panel width=150 height=80 { ... }
```

The first panel will be positioned at (0, 0), and the second at (0, 100), assuming no margins.

### Margins and Padding for Root Elements

For root elements, margins are applied relative to the viewport edges, creating space between the element and the screen boundaries. Padding on root elements is added to the implicit panel's content area, effectively offsetting the layout of child elements within the root.

For example:
- A root panel with `margin=10` will position its content 10 pixels from the top and left edges of the viewport.
- A root panel with `padding=20` will inset its child elements by 20 pixels from the panel's edges.

## Final size of elements

The width and height of UI elements are determined by the same set of rules, collectively referred to as dimensions. When a dimension is explicitly set, it defines the element's final size in that direction. If a dimension is omitted, it defaults to fit-content.

For example, the following two declarations are equivalent:

```css
panel { 
```

is the same as

```css
panel width=fit-content height=fit-content {
```

### Units

You can use several units when specifying dimensions:

- (no unit) — pixels (default)  
    Example: `panel width=200 { ... }` sets the width to 200 pixels.

- `em` — relative to the font size  
    Example: `button height=2em { ... }` sets the height to twice the current font size.

- `%` — percentage of the parent element's size. For root elements, this is the viewport size.  
    Example: `container width=50% { ... }` sets the width to half of the parent element.

- `fit-content` — automatically sizes the element to fit its content. For containers, "content" refers to the combined size of all child elements after layout.  
    Example: `label width=fit-content { ... }` sets the width to match the label's content.

You can use different units for each dimension.

Example: `panel width=50% height=2em { ... }`

## Fit-Content Interactions

### With Margins
Margins are applied outside the element's content area and do not affect the size calculated by `fit-content`. The element is first sized to fit its content, then margins are added to determine its final position and spacing relative to siblings.

### With Padding
Padding is included in the content size calculation. When an element uses `fit-content`, the padding is added to the natural size of the content to determine the element's dimensions.


## Step-by-step algorithm for computing final position

1. **Determine available space**: The parent container's size is established, either from its own dimensions or from its parent.

2. **Determine children sizes**: 
Measure each child element based on its specified dimensions, units, and layout constraints. 

3. **Layout children in direction**: Arrange child elements sequentially along the main axis (horizontal for `row`, vertical for `column`) based on the `direction` attribute.

4. **Apply justify-content**: Distribute the child elements along the main axis according to the `justify-content` attribute:
   - `start`: Elements are placed sequentially starting from the beginning of the container. The first element's position is set to the container's start edge plus the container's padding. Each subsequent element is positioned immediately after the previous element, accounting for the previous element's size and margin.
   - `center`: Elements are centered as a group within the container, regardless of their individual sizes. The total combined width of all elements along the main axis (including their margins) is calculated, and the group is offset so that its center aligns with the container's center. The offset is (container_size - total_combined_width) / 2.
   - `end`: Elements are placed starting from the end of the container. The last element is positioned at the container's end edge minus the container's padding. Elements are laid out in reverse order, with each preceding element positioned before the next, accounting for sizes and margins.
   - `space-between`: Elements are distributed evenly with the first element at the start and the last at the end. The remaining space (container_size - total_element_sizes - total_margins - container_padding) is divided equally among the gaps between elements. If there are n elements, there are n-1 gaps, each getting space = remaining_space / (n-1).
   - `space-around`: Elements are distributed with equal space around each. The total available space is divided equally around all elements. For n elements, the space per element is total_space / (2n), applied to both sides of each element.

5. **Apply margins and padding**: Adjust the positions and sizes of elements by adding margins (space outside the element) and padding (space inside the element border).

6. **Compute final positions**: Calculate the absolute screen positions for each element based on the layout and parent positions. 

### Justify-Content Visual Examples

The following ASCII art illustrates how each `justify-content` option distributes elements horizontally within a container (assuming `direction=row`):

#### `start`
```
+---------------------------------+
| [elem1] [elem2] [elem3]         |
+---------------------------------+
```
Elements are placed at the start of the container.

#### `center`
```
+---------------------------------+
|      [elem1] [elem2] [elem3]     |
+---------------------------------+
```
Elements are centered as a group within the container.

#### `end`
```
+---------------------------------+
|         [elem1] [elem2] [elem3] |
+---------------------------------+
```
Elements are placed at the end of the container.

#### `space-between`
```
+---------------------------------+
|[elem1]      [elem2]      [elem3]|
+---------------------------------+
```
Elements are evenly distributed with the first at start and last at end.

#### `space-around`
```
+---------------------------------+
|  [elem1]   [elem2]   [elem3]    |
+---------------------------------+
```
Equal space is distributed around each element.

## Margins and Padding

Margins and padding control spacing around and within elements.

### Margins
Margins create space outside the element's border and affect only the element's positioning, not its size. They determine the distance between elements and their containers.

- Margins do not increase the element's width or height; they are added externally.
- Example: `panel margin=10 { ... }` adds 10 pixels of space around the panel.

### Padding
Padding creates space inside the element's border and affects the element's size by expanding its content area.

- Padding increases the element's total size.
- Example: `panel padding=10 { ... }` adds 10 pixels of space inside the panel, increasing its size.

### Visual Representation
The following ASCII art illustrates the relationship between margin, padding, and the element's content area:

```
+-----------------------------+ 
|          margin             |
|  +-----------------------+  | <- border/size of the element
|  |      padding          |  |
|  |  +-----------------+  |  |
|  |  |                 |  |  |
|  |  |    content      |  |  |
|  |  |   (width x      |  |  |
|  |  |    height)      |  |  |
|  |  |                 |  |  |
|  |  +-----------------+  |  |
|  |      padding          |  |
|  +-----------------------+  | <- border/size of the element
|          margin             |
+-----------------------------+
```

- **Width**: The horizontal size of the content area.
- **Height**: The vertical size of the content area.
- **Total element width** = width + padding-left + padding-right
- **Total element height** = height + padding-top + padding-bottom
- **Margins** are outside the element and do not affect its size.

### Positioning Formulas
The exact position of a child element is calculated as follows:
- child.x = parent.x + parent.padding_left + child.margin_left
- child.y = parent.y + parent.padding_top + child.margin_top

## Z-order (implicit stacking)

There is no explicit `z-order` property in the system. Stacking
is determined implicitly by the widget tree and the order widgets are
declared or added at runtime:

- Rendering order: a parent is rendered before its children; children
  are therefore drawn on top of their parent.
- Sibling order: later siblings (declared later in markup or appended at
  runtime) are rendered on top of earlier siblings.
- Hit-testing and input dispatch follow this same top-to-bottom order:
  the visually top-most, visible widget receives pointer events first.

To change stacking, reorder or reparent widgets in markup or at runtime
(for example, move a widget to the end of its parent's children). For
transient UI that must appear above everything else (popups, overlays),
attach it to a dedicated top-level container.
