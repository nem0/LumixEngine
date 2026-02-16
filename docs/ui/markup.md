# Text-based markup

We adopt a braces-based, human-friendly syntax as the canonical form. Braces are preferred for grouping, but a self-closing shorthand (no braces) is allowed for leaf elements.

*Example:*

```
panel id=main width=800 height=600 {
    panel class=title {
        text value="Game Menu"
    }
    panel {
        button class=primary width=8em height=2em { "Start" }
        button width=8em height=2em { "Options" }
        button class=danger width=8em height=2em { "Quit" }
    }
    panel background_color=#f0f0f0 {
        "Some text with background color"
    }
    panel {
        "Some text"
        image src="path/to/image.png" width=100 height=100
        image src="path/to/image2.png" width=100 height=100
    }
    panel {
        "Multiline text
            is on 
            multiple lines"
        text value="Some other text" font="path/to/font.ttf" font_size=13
        "How to \"escape\" quotes"
    }
    panel {
        panel width=50% {
            "Left panel content"
        }
        panel width=50% {
            "Right panel content"
            image src="path/to/image.png" width=100 height=100
        }
    }
}
```

## Grammar

### Tokens

- **Identifier**: element and attribute names, e.g. `panel`, `text`, `class`.
- **String**: double-quoted text for strings, e.g. "Game Menu". Can span multiple lines.
- **Number**: integer or float, e.g. `100`, `50.5`.
- **Percentage**: number followed by `%`, e.g. `50%`.
- **Color**: hexadecimal color values prefixed with `#`, e.g. `#FF0000`.
- **Assignment**: the `=` sign used to bind attributes to values (whitespace around `=` is allowed).
- **Braces**: `{` and `}` used to group children inside an element.
- **Whitespace**: separates tokens, ignored except inside strings.
- **Comment**: `//` to end of line or `/* */` block comments (comments are skipped by the lexer).

### Syntax

```
document      := element*

element       := STRING                // text node (quoted)
               | IDENT attribute* element_terminator

element_terminator := block | terminator

block         := '{' element* '}'

terminator    := NEWLINE | EOF | '}'    // element without block is a leaf

attribute     := IDENT '=' value

value         := STRING
               | NUMBER
               | PERCENT
               | COLOR
               | IDENT

STRING        := '"' (escaped_char | character | newline)* '"'    // double-quoted strings may contain newlines
IDENT         := /[A-Za-z_][A-Za-z0-9_-]*/
NUMBER        := /[0-9]+(\.[0-9]+)?/
PERCENT       := NUMBER '%'
COLOR         := '#' [0-9A-Fa-f]{6}
```

### Notes
- Elements are case-sensitive identifiers that name the UI widget/type.
- Attributes are key/value pairs. Values may be quoted strings, identifiers, numbers, percentages, or colors. Whitespace around `=` is allowed.
- Colors are specified in hexadecimal format with a `#` prefix, e.g. `#FF0000` (6-digit).
- A block (braced) element contains child elements and/or text children.
- Leaf elements **must** be written without braces when they have no children. Example: `button text="Start" class=primary`.
- Text content inside a block must use quoted strings. Quoted strings may span multiple lines, allowing natural multiline content without special delimiters.
- A quoted string used where an element is expected is treated as an implicit `text` element (equivalent to `text value="..."`). Unquoted bare text is not allowed.
- Inside double-quoted strings use `\"` to escape quotes, `\\` for backslash, and common escapes `\n`, `\t`, `\r` are supported. Parsers MAY trim a common indentation prefix from multi-line strings to improve authoring ergonomics.

