# UI Snippets

## Table of Contents

- [Basic Elements](#basic-elements)
  - [Simple Panel](#simple-panel)
  - [Panel with ID and Class](#panel-with-id-and-class)
  - [Nested Panels](#nested-panels)
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
  - [Button-like Panel](#button-like-panel)
- [Advanced Examples](#advanced-examples)
  - [Menu Layout](#menu-layout)
  - [Card Layout](#card-layout)
  - [Responsive Layout](#responsive-layout)
- [Units and Sizing](#units-and-sizing)
  - [Percentage Sizing](#percentage-sizing)
  - [EM Units](#em-units)
  - [Fit Content](#fit-content)
- [Colors and Theming](#colors-and-theming)
  - [Hex Colors](#hex-colors)

## Basic Elements

### Simple Panel

```css
panel width=200 height=100 bg-color=#ffffff {
    "Hello World"
}
```

### Panel with ID and Class

```css
panel id="my_panel" class="container" width=300 height=200 {
    "Content here"
}
```

### Nested Panels

```css
panel {
    panel width=50% {
        "Left side"
    }
    panel width=50% {
        "Right side"
    }
}
```

## Text Elements

### Basic Text

```css
panel {
    "This is some text"
}
```

### Styled Text with Span

```css
panel {
    span color=#ff0000 value="Red text"
    " and normal text"
}
```

### Text Alignment

```css
panel text-align="center" width=200 {
    "Centered text"
}
```

### Multi-line Text

```css
panel {
    "This is multiline text
that spans several lines"
}
```

## Images

### Basic Image

```css
image src="path/to/image.png" width=100 height=100
```

### Image with Fit Options

```css
image src="path/to/image.png" width=200 height=150 fit="cover"
```

## Layout Examples

### Horizontal Layout (Row)

```css
panel direction="row" {
    panel width=100 height=50 bg-color=#ff0000 { }
    panel width=100 height=50 bg-color=#00ff00 { }
    panel width=100 height=50 bg-color=#0000ff { }
}
```

### Vertical Layout (Column)

```css
panel direction="column" {
    panel width=100 height=50 bg-color=#ff0000 { }
    panel width=100 height=50 bg-color=#00ff00 { }
    panel width=100 height=50 bg-color=#0000ff { }
}
```

### Centered Content

```css
panel width=100% height=100% justify-content="center" align-items="center" {
    panel width=200 height=100 bg-color=#cccccc {
        "Centered content"
    }
}
```

### Grid-like Layout with Wrapping

```css
panel width=200 direction="row" wrap="true" {
    panel width=100 height=100 bg-color=#ff0000 { "1" }
    panel width=100 height=100 bg-color=#00ff00 { "2" }
    panel width=100 height=100 bg-color=#0000ff { "3" }
    panel width=100 height=100 bg-color=#ffff00 { "4" }
    panel width=100 height=100 bg-color=#ff00ff { "5" }
}
```

## Styling

### Basic Style Block

```css
style {
    .my_class {
        bg-color: #ffffff;
        color: #000000;
        padding: 10;
    }
}

panel class="my_class" {
    "Styled text"
}
```

### Type Selector

```css
style {
    panel {
        bg-color: #f0f0f0;
    }
}
```

### ID Selector

```css
style {
    $my_id {
        width: 200;
        height: 100;
    }
}

panel id="my_id" {
    "Specific panel"
}
```

### Child Selector

```css
style {
    panel > panel {
        margin: 5;
    }
}
```

## Forms and Inputs

### Button-like Panel

```css
panel class="button" width=120 height=40 bg-color=#007bff {
    "Click Me"
}
```

## Advanced Examples

### Menu Layout

```css
panel direction="column" padding=20 {
    panel text-align="center" { "Game Title" }
    panel direction="column" {
        panel class="menu_item" { "Start Game" }
        panel class="menu_item" { "Options" }
        panel class="menu_item" { "Quit" }
    }
}
```

### Card Layout

```css
panel class="card" width=300 padding=15 bg-color=#ffffff {
    panel { "Card Title" }
    panel { "Card content goes here..." }
    panel direction="row" justify-content="end" {
        panel class="button" { "OK" }
        panel class="button" { "Cancel" }
    }
}
```

### Responsive Layout

```css
panel direction="row" {
    panel width=30% {
        "Sidebar"
    }
    panel width=70% {
        "Main content"
    }
}
```

## Units and Sizing

### Percentage Sizing

```css
panel width=50% height=50% {
    "Half size panel"
}
```

### EM Units

```css
panel width=10em height=2em {
    "Sized with em units"
}
```

### Fit Content

```css
panel width="fit-content" {
    "This panel sizes to content"
}
```

## Colors and Theming

### Hex Colors

```css
panel bg-color=#ff5733 {
    "Orange background"
}
```
