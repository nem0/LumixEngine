# UI Layout

## Final size of elements

The width and height of UI elements are determined by the same set of rules, collectively referred to as dimensions. You can specify both dimensions at once using the syntax `size=widthxheight`. When a dimension is explicitly set, it defines the element's final size in that direction. If a dimension is omitted, it defaults to 100% of the available space.

For example, the following two declarations are equivalent:

```css
panel { 
```

is the same as

```css
panel width=100% {
```

### Units

You can use several units when specifying dimensions:

- (no unit) — pixels (default)  
    Example: `panel width=200 { ... }` sets the width to 200 pixels.

- `em` — relative to the font size  
    Example: `button height=2em { ... }` sets the height to twice the current font size.

- `%` — percentage of the parent element's size  
    Example: `container width=50% { ... }` sets the width to half of the parent element.

- `fit-content` — automatically sizes the element to fit its content  
    Example: `label width=fit-content { ... }` sets the width to match the label's content.

You can use different units for each dimension.

Example: `panel width=50% height=2em { ... }`

## Step-by-step algorithm for computing final position

1. **Determine available space**: The parent container's size is established, either from its own dimensions or from its parent.

2. **Determine children sizes**: 
Measure each child element based on its specified dimensions, units, and layout constraints. 

3. **Layout children in direction**: Arrange child elements sequentially along the main axis (horizontal for `row`, vertical for `column`) based on the `direction` attribute.

4. **Apply justify-content**: Distribute the child elements along the main axis according to the `justify-content` attribute:
   - `start`: Place elements starting from the beginning of the container.
   - `center`: Center the elements within the container.
   - `end`: Place elements starting from the end of the container.
   - `space-between`: Distribute elements evenly with the first at the start and last at the end.
   - `space-around`: Distribute elements evenly with equal space around each.

5. **Apply margins and padding**: Adjust the positions and sizes of elements by adding margins (space outside the element) and padding (space inside the element border).

6. **Compute final positions**: Calculate the absolute screen positions for each element based on the layout and parent positions. 

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