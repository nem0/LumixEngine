# UI Snippets

## Table of Contents

- [Basic Elements](#basic-elements)
  - [Simple Box](#simple-box)
  - [Box with ID and Class](#box-with-id-and-class)
  - [Nested Boxes](#nested-boxes)
- [Text Elements](#text-elements)
  - [Basic Text](#basic-text)
  - [Styled Text with Span](#styled-text-with-span)
  - [Text Alignment](#text-alignment)
  - [Multi-line Text](#multi-line-text)
- [Images](#images)
  - [Basic Image](#basic-image)
  - [Image with Fit Options](#image-with-fit-options)
- [Layout Examples](#layout-examples)
  - [Horizontal Layout (Row)](#horizontal-layout-row)
  - [Vertical Layout (Column)](#vertical-layout-column)
  - [Centered Content](#centered-content)
  - [Grid-like Layout with Wrapping](#grid-like-layout-with-wrapping)
- [Styling](#styling)
  - [Basic Style Block](#basic-style-block)
  - [Type Selector](#type-selector)
  - [ID Selector](#id-selector)
  - [Child Selector](#child-selector)
- [Forms and Inputs](#forms-and-inputs)
  - [Button-like Box](#button-like-box)
- [Advanced Examples](#advanced-examples)
  - [Menu Layout](#menu-layout)
  - [Card Layout](#card-layout)
  - [Responsive Layout](#responsive-layout)
- [Units and Sizing](#units-and-sizing)
  - [Percentage Sizing](#percentage-sizing)
  - [EM Units](#em-units)
  - [Fit Content](#fit-content)
  - [Grow](#grow)
- [Colors and Theming](#colors-and-theming)
  - [Hex Colors](#hex-colors)

## Basic Elements

### Simple Box

```css
[box width=200 height=100 bg-color=#ffffff] {
    Hello World
}
```

### Box with ID and Class

```css
[box id="my_panel" .container width=300 height=200] {
    Content here
}
```

### Nested Boxes

```css
[box] {
    [box width=50%] {
        Left side
    }
    [box width=50%] {
        Right side
    }
}
```

## Text Elements

### Basic Text

```css
[box] {
    This is some text
}
```

### Styled Text with Span

```css
[box] {
    [span color=#ff0000 value="Red text"]
     and normal text
}
```

### Text Alignment

```css
[box align=center width=200] {
    Centered text
}
```

### Multi-line Text

```css
[box] {
    This is multiline text
    that spans several lines
}
```

## Images

### Basic Image

```css
[image src="path/to/image.png" width=100 height=100]
```

### Image with Fit Options

```css
[image src="path/to/image.png" width=200 height=150 fit=cover]
```

## Layout Examples

### Horizontal Layout (Row)

```css
[box direction=row] {
    [box width=100 height=50 bg-color=#ff0000] { }
    [box width=100 height=50 bg-color=#00ff00] { }
    [box width=100 height=50 bg-color=#0000ff] { }
}
```

### Vertical Layout (Column)

```css
[box direction=column] {
    [box width=100 height=50 bg-color=#ff0000] { }
    [box width=100 height=50 bg-color=#00ff00] { }
    [box width=100 height=50 bg-color=#0000ff] { }
}
```

### Centered Content

```css
[box width=100% height=100% justify-content=center align-items=center] {
    [box width=200 height=100 bg-color=#cccccc] {
        Centered content
    }
}
```

### Grid-like Layout with Wrapping

```css
[box width=200 direction=row wrap=true] {
    [box width=100 height=100 bg-color=#ff0000] { 1 }
    [box width=100 height=100 bg-color=#00ff00] { 2 }
    [box width=100 height=100 bg-color=#0000ff] { 3 }
    [box width=100 height=100 bg-color=#ffff00] { 4 }
    [box width=100 height=100 bg-color=#ff00ff] { 5 }
}
```

## Styling

### Basic Style Block

```css
[style] {
    .my_class {
        bg-color: #ffffff;
        color: #000000;
        padding: 10;
    }
}

[box .my_class] {
    Styled text
}
```

### Type Selector

```css
[style] {
    [box] {
        bg-color: #f0f0f0;
    }
}
```

### ID Selector

```css
[style] {
    $my_id {
        width: 200;
        height: 100;
    }
}

[box id="my_id"] {
    Specific box
}
```

### Child Selector

```css
[style] {
    box > [box] {
        margin: 5;
    }
}
```

## Forms and Inputs

### Button-like Box

```css
[box .button width=120 height=40 bg-color=#007bff] {
    Click Me
}
```

## Advanced Examples

### Menu Layout

```css
[box direction=column padding=20] {
    [box align=center] { Game Title }
    [box direction=column] {
        [box .menu_item] { Start Game }
        [box .menu_item] { Options }
        [box .menu_item] { Quit }
    }
}
```

### Card Layout

```css
[box .card width=300 padding=15 bg-color=#ffffff] {
    [box] { Card Title }
    [box] { Card content goes here... }
    [box direction=row justify-content=end] {
        [box .button] { OK }
        [box .button] { Cancel }
    }
}
```

### Responsive Layout

```css
[box direction=row] {
    [box width=30%] {
        Sidebar
    }
    [box width=70%] {
        Main content
    }
}
```

## Units and Sizing

### Percentage Sizing

```css
[box width=50% height=50%] {
    Half size box
}
```

### EM Units

```css
[box width=10em height=2em] {
    Sized with em units
}
```

### Fit Content

```css
[box width=fit-content] {
    This box sizes to content
}
```

### Grow

```css
[box direction=row width=300] {
    [box width=100] { Fixed size }
    [box grow=1] { Fills remaining space }
}
```

Left-fill-right (fixed on both sides, growing middle):

```css
[box direction=row width=400] {
    [box width=100] { Left }
    [box grow=1] { Middle }
    [box width=100] { Right }
}
```

Split remaining space in a 2:1 ratio:

```css
[box direction=row width=400] {
    [box grow=2] { Gets 2/3 }
    [box grow=1] { Gets 1/3 }
}
```

## Colors and Theming

### Hex Colors

```css
[box bg-color=#ff5733] {
    Orange background
}
```
