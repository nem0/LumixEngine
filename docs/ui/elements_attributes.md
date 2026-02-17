# Elements and attributes

## Every element

| Attribute | Description | Default / Values |
|---|---|---|
| `id` | Assigns a unique identifier to the element, allowing it to be referenced or manipulated individually. Values must be enclosed in `""` | (no default) |
| `class` | Specifies one or more class names for the element, enabling grouping and shared styling or behavior. Values must be enclosed in `""`. | (no default) |
| `visible` | Controls the visibility of the element. | `true` |

Note: sizing-related attributes (see below) accept numeric values, percentages, `em` units, or the keyword `fit-content` to size an element to its content (subject to parent constraints).

## Blocks

Block elements (`panel`, `image`, `input`, `canvas`, `text`) support sizing/layout properties.

| Attribute | Description | Default / Values |
|---|---|---|
| `width` | Sets the width of the element. | `fit-content` |
| `height` | Sets the height of the element. | `fit-content` |
| `margin` | Space outside the element border in all directions. | `0` |
| `padding` | Space inside the element border, around the content in all directions. | `0` |
| `align` | Text alignment. Values: `left`, `center`, `right`. | `left` |

These attributes accept `NUMBER`, `PERCENT` (e.g. `50%`), `em` units (e.g. `2em`), or the `fit-content` keyword (for example: `width=fit-content`).

## panel

Rectangular container that can have children. Parent attributes cascade down to children.

| Attribute | Description | Default / Values |
|---|---|---|
| `background-image` | Sets a background image for the panel. Accepts a path to an image file or a URL. | (no default) |
| `background-fit` | How the background image is scaled. Values: `cover` — Scales the image to cover the entire panel area while preserving its aspect ratio (may be cropped); `contain` — Scales the image to fit entirely within the panel while preserving its aspect ratio (may leave empty space); `fill` — Stretches the image to fill the panel's bounds (may distort aspect ratio); `none` — Displays the image at its original size. | `fill` |
| `bg-color` | Background color of the panel. | `transparent` |
| `direction` | Layout direction of child elements. Values: `row` (horizontal), `column` (vertical). | `row` |
| `wrap` | Controls layout overflow behavior. Values: `wrap` — child elements wrap onto multiple lines/columns; `nowrap` — no wrapping (default); `clip` — no wrapping, overflowing content not rendered. | `nowrap` |
| `justify-content` | How child elements are distributed along the main axis. `start` — Align items to the start of the container; `center` — Center items; `end` — Align items to the end; `space-between` — Distribute items evenly with first at start and last at end; `space-around` — Distribute items with equal space around them. | `start` |
| `align-items` | Default off-axis alignment for children. Values: `start`, `center`, `end`, `stretch` (fill). | `stretch` |

## text

Syntactic sugar for `panel width=fit-content height=fit-content` containing text content. Supports all block properties. Extends panel's attributes with one new attribute:

| Attribute | Description | Default / Values |
|---|---|---|
| `value` | The text content to display. | `""` |

## image

Displays an image.

| Attribute | Description | Default / Values |
|---|---|---|
| `src` | Path to the image file. | `""` |
| `fit` | How the image should fit within its bounds. `fill` — Stretches the image to fill the element's bounds (may distort aspect ratio); `contain` — Scales the image to fit entirely within the bounds while preserving aspect ratio (may leave empty space); `cover` — Scales the image to cover the entire bounds while preserving aspect ratio (may be cropped); `none` — Displays the image at its original size. | `contain` |

## input

// TODO

## canvas

A drawable area for custom rendering.


## Inline text – Quoted strings

We cannot specify attributes for quoted strings, but they can inherit relevant attributes from their parents.

## Inheritable Attributes

The following attributes are inherited from parent elements to their descendants:

- `visible` - Controls the visibility of the element and its descendants.
- `align` - Text alignment, inherited for text content.
- `color` - Text color, inherited for inline text content.
- `font` - Font file path, inherited for text rendering.
- `font-size` - Font size, inherited for text rendering.
