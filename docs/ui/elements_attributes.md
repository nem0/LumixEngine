# Elements and attributes

## Every element

| Attribute | Description | Default / Values |
|---|---|---|
| `id` | Unique identifier for the element; allows referencing or manipulating the element individually. Values should be enclosed in double quotes ("..."). | (no default) |
| `class` | One or more class names for grouping and shared styling or behavior. Values should be enclosed in double quotes. | (no default) |
| `visible` | Controls the visibility of the element. | `true` |

Note: sizing-related attributes (see below) accept numeric values, percentages, `em` units, or the keyword `fit-content` to size an element to its content (subject to parent constraints).

## Sizing and layout properties

Supported by all elements.

| Attribute | Description | Default / Values |
|---|---|---|
| `width` | Sets the element's width. | `fit-content` |
| `height` | Sets the element's height. | `fit-content` |
| `margin` | Space outside the element border in all directions. | `0` |
| `padding` | Space inside the element border, around the content in all directions. | `0` |
| `text-align` | Text alignment. Values: `left`, `center`, `right`. | `left` |

These attributes accept `NUMBER`, `PERCENT` (e.g. `50%`), `em` units (e.g. `2em`), or the `fit-content` keyword (for example: `width=fit-content`).

## panel

Rectangular container that can have children.

| Attribute | Description | Default / Values |
|---|---|---|
| `background-image` | Background image for the panel. Accepts a path to an image file. | (no default) |
| `background-fit` | How the background image is scaled. Values: `cover`, `contain`, `fill`, `none`. | `fill` |
| `bg-color` | Background color of the panel. | `transparent` |
| `direction` | Layout direction of child elements. Values: `row` (horizontal), `column` (vertical). | `row` |
| `wrap` | Layout overflow behavior. Values: `true`, `false`. | `false` |
| `justify-content` | Distribution of child elements along the main axis. Values: `start`, `center`, `end`, `space-between`, `space-around`. | `start` |
| `align-items` | Cross-axis alignment for children. Values: `start`, `center`, `end`, `stretch`. | `stretch` |

## image

Displays an image.

| Attribute | Description | Default / Values |
|---|---|---|
| `src` | Path to the image file. | `""` |
| `fit` | How the image fits within its bounds. Values: `fill`, `contain`, `cover`, `none`. | `contain` |

## span

Inline text container for styling text without creating a block element. It does no have children, text is in `value` attribute.

| Attribute | Description | Default / Values |
|---|---|---|
| `value` | The text content to display within the span. | (no default) |
| `color` | Text color for the span content. | inherited |
| `font` | Font file path for the span content. | inherited |
| `font-size` | Font size for the span content. | inherited |

Quoted string can be used in place of a span.

```css
span value="text"
// is the same as
"text"
```

### Quoted strings

In place where span can be used, we can put just quoted string, which is equivalent to span with only value attribute.

```css

## input

**TODO:** Document input attributes.

## canvas

Drawable area for custom rendering.

## Inline text – Quoted strings

Attributes cannot be specified directly on quoted strings, but they can inherit relevant attributes from their parent elements.

## Inheritable Attributes

The following attributes are inherited from parent elements to their descendants:

- `visible` - Controls the visibility of the element and its descendants.
- `text-align` - Text alignment, inherited for text content.
- `color` - Text color, inherited by inline text content.
- `font` - Font file path, inherited for text rendering.
- `font-size` - Font size, inherited for text rendering.
