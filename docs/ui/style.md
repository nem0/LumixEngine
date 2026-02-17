# Style

This document describes the in-game GUI styling system: a small, CSS-like language
optimized for simplicity and runtime performance. Style is defined in the same file as UI. The style is defined in `style` block:

```css
style {
	.some_class {
		width: 50%;
	}
}
```

## Goals

- Simple, familiar syntax (type / .class / $id selectors)
- Minimal selector complexity to keep matching fast
- Styles can change any attribute on elements. See [list of all attributes](elements_attributes.md).

## Selectors & precedence

- Supported selectors: `type`, `.class`, `$id`, and `parent > child` (direct child).
- Precedence (highest -> lowest): inline style > `$id` > `.class` > `type` > stylesheet order.

## Inheritance

Certain style attributes are inherited from a widget's parent when the child
doesn't supply its own value. This keeps styles compact while allowing
explicit overrides where needed.

- Inherited by default: `font`, `font-size`, `color`, `visibility`, and simple text-related attributes.
- Other are not inherited.

Rules:

- A child uses the parent's computed value for an attribute only when the
	child has no value provided by any source (inline style, `$id`, `.class`,
	`type` selector, or earlier stylesheet rules).
- Precedence still applies: inline styles and higher-precedence selectors
	override inherited values.
- Shorthands follow normal expansion rules; inheritance applies per
	sub-attribute where meaningful. For example, a parent `padding` does not
	implicitly force child `padding` values unless the child is left unset.
- Inheritance flows along the widget tree (parent -> child). Selector forms
	such as `parent > child` affect matching and specificity but do not change
	the fundamental inheritance mechanism.
