# Text-based markup

*Example:*

```c++
[panel id="main" width=800 height=600] {
    [panel class="title"] {
        "Game Menu"
    }
    [panel] {
        [panel class="primary" width=8em height=2em] { "Start" }
        [panel width=8em height=2em] { "Options" }
        [panel class="danger" width=8em height=2em] { "Quit" }
    }
    [panel bg-color=#f0f0f0] {
        "Some text with background color"
    }
    [panel] {
        "Some text"
        [image src="path/to/image.png" width=100 height=100]
        [image src="path/to/image2.png" width=100 height=100]
    }
    [panel] {
        "Multiline text
            is on 
            multiple lines"
        [panel font="path/to/font.ttf" font_size=13] { "Some other text" }
        "How to \"escape\" quotes"
    }
    [panel] {
        [panel width=50%] {
            "Left panel content"
        }
        [panel width=50%] {
            "Right panel content"
            [image src="path/to/image.png" width=100 height=100]
        }
    }
}
```

The markup language also supports CSS-like style blocks for defining reusable styles. See [Style documentation](style.md) for details.

## Grammar

### Tokens

- **Identifier**: element and attribute names, e.g. `panel`, `text`, `class`.
- **String**: double-quoted text for strings, e.g. "Game Menu". Can span multiple lines.
- **Number**: integer or float, e.g. `100`, `50.5`.
- **Percentage**: number followed by `%`, e.g. `50%`.
- **EM**: number followed by `em`, e.g. `2em`.
- **Color**: hexadecimal color values prefixed with `#`, e.g. `#FF0000`.
- **Assignment**: the `=` sign used to bind attributes to values (whitespace around `=` is allowed).
- **Braces**: `{` and `}` used to group children inside an element.
- **Colon**: `:` used in style property declarations.
- **Semicolon**: `;` used to terminate style properties.
- **Dot**: `.` used for class selectors in styles.
- **Dollar**: `$` used for ID selectors in styles.
- **Whitespace**: separates tokens, ignored except inside strings.
- **Comment**: `//` to end of line or `/* */` block comments (comments are skipped by the lexer).
- **Brackets**: `[` and `]` used to enclose tag + attributes.

### Syntax

```
markup ::= element*

element ::= '[' identifier attribute* ']' '{' (element | string)* '}'

attribute ::= identifier '=' (string | number | number '%' | number 'em' | '#' hexdigit{6} | identifier)

string ::= '"' (any char except '"' and '\' | '\\' ('"' | '\' | 'n' | 't' | 'r'))* '"'

number ::= digit+ ('.' digit+)?

identifier ::= letter (letter | digit | '_' | '-')*

digit ::= '0'-'9'

hexdigit ::= '0'-'9' | 'a'-'f' | 'A'-'F'

letter ::= 'a'-'z' | 'A'-'Z'
```

### Notes
- Elements are case-sensitive identifiers that name the UI widget/type.
- Attributes are key/value pairs grouped in square brackets `[]` with a tag. Values may be quoted strings, identifiers, numbers, percentages, or colors. Whitespace around `=` is allowed.
- Colors are specified in hexadecimal format with a `#` prefix, e.g. `#FF0000` (6-digit).
- A block (braced) element contains child elements.
- Text content inside a block must use quoted strings. Quoted strings may span multiple lines for readability, but newlines are treated as whitespace and do not create line breaks in the rendered output.
- A quoted string creates an inline text node that flows with other elements.
- `text` element is syntactic sugar for `panel width=fit-content height=fit-content` containing text nodes.
- Unquoted bare text is not allowed.
- Inside double-quoted strings use `\"` to escape quotes, `\\` for backslash, and common escapes `\n`, `\t`, `\r` are supported.

