# Text-based markup

*Example:*

```c++
[box id="main" width=800 height=600] {
    [box .title] {
        Game Menu
    }
    [box] {
        [box .primary width=8em height=2em] { Start }
        [box width=8em height=2em] { Options }
        [box .danger width=8em height=2em] { Quit }
    }
    [box bg-color=#f0f0f0] {
        Some text with background color
    }
    [box] {
        Some text
        [image src="path/to/image.png" width=100 height=100]
        [image src="path/to/image2.png" width=100 height=100]
    }
    [box] {
        Multiline text
            is on 
            multiple lines
        [box font="path/to/font.ttf" font_size=13] { Some other text }
        How to "escape" quotes
    }
    [box] {
        [box width=50%] {
            Left box content
        }
        [box width=50%] {
            Right box content
            [image src="path/to/image.png" width=100 height=100]
        }
    }
}
```

The markup language also supports CSS-like [style] blocks for defining reusable styles. See [Style documentation](style.md) for details.

## Grammar

### Tokens

- **Identifier**: element and attribute names, e.g. `box`, `text`, `width`.
- **String**: double-quoted text for attribute values, e.g. "center".
- **Number**: integer or float, e.g. `100`, `50.5`.
- **Text**: unquoted text for content, e.g. `Game Menu`.
- **Percentage**: number followed by `%`, e.g. `50%`.
- **EM**: number followed by `em`, e.g. `2em`.
- **Color**: hexadecimal color values prefixed with `#`, e.g. `#FF0000`.
- **Assignment**: the `=` sign used to bind attributes to values (whitespace around `=` is allowed).
- **Braces**: `{` and `}` used to group children inside an element.
- **Colon**: `:` used in style property declarations.
- **Semicolon**: `;` used to terminate style properties.
- **Dot**: `.` prefixed to class names for assignment and selectors in styles.
- **Dollar**: `$` prefixed to IDs for assignment and selectors in styles.
- **Whitespace**: separates tokens, ignored except inside strings.
- **Comment**: `//` to end of line or `/* */` block comments (comments are skipped by the lexer).
- **Brackets**: `[` and `]` used to enclose tag + attributes.

### Syntax

```
markup ::= element*

element ::= '[' identifier attribute* ']' '{' (element | text)* '}'

attribute ::= identifier '=' (string | number | number '%' | number 'em' | '#' hexdigit{6} | identifier) | '.' identifier | '$' identifier

string ::= '"' (any char except '"' and '\' | '\\' ('"' | '\' | 'n' | 't' | 'r'))* '"'

text ::= (any char except '{' '}' '[' ']')*

number ::= digit+ ('.' digit+)?

identifier ::= letter (letter | digit | '_' | '-')*

digit ::= '0'-'9'

hexdigit ::= '0'-'9' | 'a'-'f' | 'A'-'F'

letter ::= 'a'-'z' | 'A'-'Z'
```

### Notes
- Elements are case-sensitive identifiers that name the UI widget/type.
- Attributes are key/value pairs grouped in square brackets `[]` with a tag. Values may be quoted strings, identifiers, numbers, percentages, or colors. Whitespace around `=` is allowed.
- Classes can be assigned using `.classname` syntax.
- IDs can be assigned using `$id` syntax.
- Colors are specified in hexadecimal format with a `#` prefix, e.g. `#FF0000` (6-digit).
- A block (braced) element contains child elements.
- Text content inside a block can be unquoted text, treated as spans. Unquoted text may span multiple lines for readability, but newlines are treated as whitespace and do not create line breaks in the rendered output.
- Unquoted text creates an inline text span that flows with other elements.
- Inside double-quoted attribute values use `\"` to escape quotes, `\\` for backslash, and common escapes `\n`, `\t`, `\r` are supported.

