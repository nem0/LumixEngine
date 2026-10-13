# Elements and attributes

## Every element

| Attribute | Description | Default / Values |
|---|---|---|
| `id` | Unique identifier for the element; allows referencing or manipulating the element individually. Values should be enclosed in double quotes ("..."). | (no default) |
| `visible` | Controls the visibility of the element and its descendants. When an ancestor is not visible, descendants are not visible even if their `visible` is set to `true`. | `true` |
| `opacity` | Opacity multiplier for the element and its descendants. Accepts `NUMBER` (`0` to `1`) or `PERCENT` (`0%` to `100%`). Values outside range are clamped. | inherited (`1`) |
| `clipping` | Enables clipping to the element rectangle. When `true`, both the element content and all descendants are clipped to that element bounds (after layout, in screen space). | `false` (`true`/`false`) |

Classes can be assigned to elements using the `.classname` syntax, e.g., `[box .myclass]`. Multiple classes can be assigned by chaining them: `[box .class1 .class2]`.

Note: sizing-related attributes (see below) accept numeric values, percentages, `em` units, or the keyword `fit-content`. To make an element grow into available space use the `grow` attribute instead.

## Sizing and layout properties

Supported by all box elements.

| Attribute | Description | Default / Values |
|---|---|---|
| `width` | Sets the element's width. | `fit-content` |
| `height` | Sets the element's height. | `fit-content` |
| `position` | Positioning mode or anchor preset. Values: `relative`, `absolute`, `center`, `top-left`, `top-center`, `top-right`, `middle-left`, `middle-center`, `middle-right`, `bottom-left`, `bottom-center`, `bottom-right`. Relative elements participate in normal flow, absolute elements and presets are placed using offsets relative to the parent content box. | `relative` |
| `top` | Vertical offset for positioned elements. Moves the element down when positive. | `0` |
| `left` | Horizontal offset for positioned elements. Moves the element right when positive. | `0` |
| `pivot-x` | Horizontal pivot for `position=absolute`. Interpreted in element space and subtracted from final absolute x position. | `0` |
| `pivot-y` | Vertical pivot for `position=absolute`. Interpreted in element space and subtracted from final absolute y position. | `0` |
| `grow` | Grow weight along the parent's main axis, similar to CSS `flex-grow`. A value of `0` (default) means no growing. Growing elements share remaining space proportionally to their weights after all fixed-size siblings are measured. | `0` |
| `margin` | Space outside the element border in all directions. | `0` |
| `margin-left` | Space outside the element border on the left side. | `0` |
| `margin-right` | Space outside the element border on the right side. | `0` |
| `margin-top` | Space outside the element border on the top side. | `0` |
| `margin-bottom` | Space outside the element border on the bottom side. | `0` |
| `padding` | Space inside the element border, around the content in all directions. | `0` |
| `padding-left` | Inner space on the left side. | `0` |
| `padding-right` | Inner space on the right side. | `0` |
| `padding-top` | Inner space on the top side. | `0` |
| `padding-bottom` | Inner space on the bottom side. | `0` |
| `align` | Text alignment. Values: `left`, `center`, `right`. | `left` |

The `width` and `height` attributes accept `NUMBER`, `PERCENT` (e.g. `50%`), `em` units (e.g. `2em`), or the `fit-content` keyword (for example: `width=fit-content` or `width=50%`).

`top` and `left` apply when `position` is set. For `position=relative`, offsets are applied after normal flow placement. For `position=absolute`, offsets are resolved from the parent origin (padding is ignored).

`pivot-x` and `pivot-y` apply only when `position=absolute` and use the same unit format as `top`/`left` (`NUMBER`, `%`, `em`). Percent values are relative to the element's own size on the corresponding axis (`pivot-x` from element width, `pivot-y` from element height). Final absolute position is computed as base absolute position plus `left`/`top` minus pivot offsets. Defaults are `0`, which preserves top-left behavior.

Position presets are shorthand for `position=absolute` + offsets + pivots:

- `center` and `middle-center` = `position=absolute left=50% top=50% pivot-x=50% pivot-y=50%`
- `top-left` = `position=absolute left=0 top=0 pivot-x=0 pivot-y=0`
- `top-center` = `position=absolute left=50% top=0 pivot-x=50% pivot-y=0`
- `top-right` = `position=absolute left=100% top=0 pivot-x=100% pivot-y=0`
- `middle-left` = `position=absolute left=0 top=50% pivot-x=0 pivot-y=50%`
- `middle-right` = `position=absolute left=100% top=50% pivot-x=100% pivot-y=50%`
- `bottom-left` = `position=absolute left=0 top=100% pivot-x=0 pivot-y=100%`
- `bottom-center` = `position=absolute left=50% top=100% pivot-x=50% pivot-y=100%`
- `bottom-right` = `position=absolute left=100% top=100% pivot-x=100% pivot-y=100%`

For spacing attributes, values are applied in declaration order (last one wins). This means side-specific attributes can override shorthand when written later, and shorthand can override side-specific attributes when written later.

## box

Rectangular container that can have children.

| Attribute | Description | Default / Values |
|---|---|---|
| `bg-image` | Background image for the box. Accepts a path to an image file. | (no default) |
| `bg-fit` | How the background image is scaled. Values: `cover`, `contain`, `fill`, `none`. | `fill` |
| `bg-color` | Background color of the box. | `transparent` |
| `on-click` | Action name emitted as an `ACTION` UI event when mouse button is released over this box. | (no default) |
| `direction` | Layout direction of child elements. Values: `row` (horizontal), `column` (vertical). | `column` |
| `wrap` | Layout overflow behavior. Values: `true`, `false`. | `true` |
| `justify-content` | Distribution of child elements along the main axis. Values: `start`, `center`, `end`, `space-between`, `space-around`. | `start` |
| `align-items` | Cross-axis alignment for children. Values: `start`, `center`, `end`, `stretch`. | `start` |

`bg-fit` value behavior:

- `fill`: Stretches the background image to match the box width and height. Aspect ratio is not preserved.
- `contain`: Scales the image uniformly so the whole image is visible inside the box. Aspect ratio is preserved; empty space may remain on one axis.
- `cover`: Scales the image uniformly to fully cover the box. Aspect ratio is preserved; parts of the image may be cropped.
- `none`: Draws the image at its original size (no scaling).

## image

Displays an image.

| Attribute | Description | Default / Values |
|---|---|---|
| `src` | Path to the image file. | `""` |
| `width` | Display width of the image. | intrinsic bitmap width |
| `height` | Display height of the image. | intrinsic bitmap height |

Sizing behavior:

- If only `width` is set, `height` is derived from the intrinsic aspect ratio.
- If only `height` is set, `width` is derived from the intrinsic aspect ratio.
- If both `width` and `height` are set, both values are used as-is.
- If neither is set, intrinsic bitmap width and height are used.

## canvas

Drawing surface for custom shapes such as rings, gauges or icons. It has no background or text of its own; its content is geometry set from script, or shapes written in markup (see [Shapes](#shapes)).

| Attribute | Description | Default / Values |
|---|---|---|
| `width` | Width of the drawing area. | `fit-content` (zero) |
| `height` | Height of the drawing area. | `fit-content` (zero) |
| `src` | Texture the geometry samples, loaded like the `src` of an [image](#image). | `""` (solid color) |

Set `width` and `height` explicitly, or use `grow` in a flex parent. With `src`, nothing is drawn until the texture is ready.

Geometry is set from script, using `element` from `core:ui/element` and `vertex` from `core:ui/vertex`:

```
element.setGeometry(canvas : element.Element, vertices : []const vertex.Vertex, indices : []const u32) : void
element.clearGeometry(canvas : element.Element) : void
```

- `x`, `y`: position in element-local units, origin at the canvas top-left. DPI and reference-size scaling is applied automatically.
- `u`, `v`: texture coordinates, `0` to `1`, origin at the top-left. Ignored without `src`.
- `r`, `g`, `b`, `a`: color, `0` to `255`. With a texture it multiplies the texel, so white leaves it unchanged.
- `indices`: three per triangle, each smaller than the vertex count. Invalid input is logged and ignored.
- Geometry persists until replaced or cleared, and takes effect on the next frame.
- Drawn before children, so children appear on top. One texture per canvas.

Example, a red triangle:

```
import "core:ui/element" as element
import "core:ui/vertex" as vertex

fn drawRedTriangle(canvas : element.Element) : void {
	const vertices : []const vertex.Vertex = [
		vertex.Vertex { .x = 0.0, .y = 0.0, .u = 0.0, .v = 0.0, .r = 255, .g = 0, .b = 0, .a = 255 },
		vertex.Vertex { .x = 100.0, .y = 0.0, .u = 0.0, .v = 0.0, .r = 255, .g = 0, .b = 0, .a = 255 },
		vertex.Vertex { .x = 0.0, .y = 100.0, .u = 0.0, .v = 0.0, .r = 255, .g = 0, .b = 0, .a = 255 }
	];
	const indices : []const u32 = [0, 1, 2];
	element.setGeometry(canvas, vertices, indices);
}
```

### Shapes

Shapes are tags that can only appear inside a `[canvas]`. The engine converts them to the same geometry that `setGeometry` produces, once when the document loads. Shapes are drawn in document order, and a `setGeometry` call from script replaces all of them.

- `[path]`: body text is a list of points separated by whitespace. Each point is `x,y`, optionally followed by `#color` and `@u,v`, in that order. A point without `#color` takes the color of the point before it, so the first point must have one (otherwise the path is an error). Invalid points are logged and skipped. Attributes: `width` (default `1`, in element-local units) and `closed` (`true` closes the shape back to the first point, default `false`; a closed path does not repeat its first point) and `filled` (`true` fills the polygon through the points, default `false`). A filled path can be concave but should not intersect itself; point colors are interpolated across the fill. A filled path is only stroked if `width` is set explicitly, and then its outline is always closed. A path with two points is a line. Edges are feathered: they fade from the shape's color to transparent over `1` element-local unit outside the shape, which smooths them when drawn at pixel scale. Example:

  ```
  [canvas] {
  	[path width=2 closed=true] { 0,0#ff0000 1,0 0.5,1 }
  	[path width=4] { 0,0#ff0000 1,0#00ff00@1,0 0.5,1#0000ff }
  }
  ```

  `@u,v` is only used with `src`, and points without it take `u,v` of `0,0`.

## span

Inline text container for styling text without creating a block element. It does no have children, text is in `text` attribute.

| Attribute | Description | Default / Values |
|---|---|---|
| `text` | The text content to display within the span. | (no default) |
| `color` | Text color for the span content. | inherited (default black) |
| `font` | Font file path for the span content. | inherited |
| `font-size` | Font size for the span content. | inherited |

Text outside of `[]` can be used in place of a span.

```css
[box] { [span value="text"] }
// is the same as
[box] { text }
```

## Inheritable Attributes

The following attributes are inherited from parent elements to their descendants:

- `align` - Text alignment, inherited for text content.
- `color` - Text color, inherited by inline text content.
- `font` - Font file path, inherited for text rendering.
- `font-size` - Font size, inherited for text rendering.
- `opacity` - Opacity multiplier, inherited and multiplied through the hierarchy.

Color format note:

- `color` and `bg-color` accept hex values in `#RRGGBB` or `#RRGGBBAA` format.
- The optional `AA` suffix is alpha (`00` transparent to `FF` opaque).
