# Elements and attributes

## Every element

| Attribute | Description | Default / Values |
|---|---|---|
| `id` | Assigns a unique identifier to the element, allowing it to be referenced or manipulated individually. Values must be enclosed in `""` | (no default) |
| `class` | Specifies one or more class names for the element, enabling grouping and shared styling or behavior. Values must be enclosed in `""`. | (no default) |
| `visible` | Controls the visibility of the panel. | `true` |
| `font-size` | Specifies the size of the text font. | `12` |
| `font` | Path to the font file used for text rendering. | (no default) |
| `color` | Text color of the panel content. | `"#000000"` |

Note: sizing-related attributes (see below) accept numeric values, percentages, the `size` token, `em` units, or the keyword `fit-content` to size an element to its content (subject to parent constraints). Parsers should treat `fit-content` as an identifier value.

## Blocks

This applies to `panel`, `button`, `image`, `input` and `canvas`. Does not apply to `text`.

| Attribute | Description | Default / Values |
|---|---|---|
| `width` | Sets the width of the element. | `100%` |
| `height` | Sets the height of the element. | `100%` |
| `size` | Defines both width and height in a single attribute (e.g., `size=100x50`). | (no default) |
| `margin` | Space outside the panel border in all directions. | `0` |
| `padding` | Space inside the panel border, around the content in all directions. | (no default) |

These attributes accept `NUMBER`, `PERCENT` (e.g. `50%`), `SIZE` tokens (e.g. `100x50`), `em` units (e.g. `2em`), or the `fit-content` keyword (for example: `width=fit-content`).

## panel

Rectangular area with optional children. Only element that can have children.

| Attribute | Description | Default / Values |
|---|---|---|
| `background-image` | Sets a background image for the panel. Accepts a path to an image file or a URL. | (no default) |
| `background-fit` | How the background image is scaled. Values: `cover` — Scales the image to cover the entire panel area while preserving its aspect ratio (may be cropped); `contain` — Scales the image to fit entirely within the panel while preserving its aspect ratio (may leave empty space); `fill` — Stretches the image to fill the panel's bounds (may distort aspect ratio); `none` — Displays the image at its original size. | `fill` |
| `background-color` | Background color of the panel. | `transparent` |
| `direction` | Layout direction of child elements. Values: `row` (horizontal), `column` (vertical). | `row` |
| `wrap` | Controls whether child elements wrap onto multiple lines or columns when there isn't enough space. | `false` |
| `justify-content` | How child elements are distributed along the main axis. `start` — Align items to the start of the container; `center` — Center items; `end` — Align items to the end; `space-between` — Distribute items evenly with first at start and last at end; `space-around` — Distribute items with equal space around them. | `start` |

## text

Displays a line or block of text.

| Attribute | Description | Default / Values |
|---|---|---|
| `value` | The text content to display. | `""` |
| `align` | Text alignment. Values: `left`, `center`, `right`. | `left` |

## button

Clickable panel that triggers an action.

## image

Displays an image.

| Attribute | Description | Default / Values |
|---|---|---|
| `src` | Path to the image file. | `""` |
| `fit` | How the image should fit within its bounds. `fill` — Stretches the image to fill the element's bounds (may distort aspect ratio); `contain` — Scales the image to fit entirely within the bounds while preserving aspect ratio (may leave empty space); `cover` — Scales the image to cover the entire bounds while preserving aspect ratio (may be cropped); `none` — Displays the image at its original size. | `contain` |

## input

Panel that accepts user input.

| Attribute | Description | Default / Values |
|---|---|---|
| `value` | The current text or value of the input. | `""` |
| `placeholder` | Text displayed when the input is empty. | `""` |

## canvas

A drawable area for custom rendering.
