# This is in early prototype/exploration stage, everything can change

# Evox

Evox is a small, statically typed scripting language for Lumix Engine.

* **simple** - easy to learn; simple, fast compiler
* **efficient** - no unnecessary allocations, fast runtime
* **safe** - try to be as safe as possible without sacrificing the first two goals. Borrow checker - not simple. Minimal UB - simple and mostly efficient.

See the [benchmark results](benchmarks/results.md) for current performance comparisons.

## Table of contents

- [Quick example](#quick-example)
- [Declarations](#declarations)
	- [Imports](#imports)
	- [Structs](#structs)
		- [Extern structs](#extern-structs)
		- [Attributes](#attributes)
	- [Enums](#enums)
	- [Functions](#functions)
	- [Comptime](#comptime)
	- [Templates](#templates)
	- [Operators](#operators)
- [Types](#types)
	- [Untyped literals](#untyped-literals)
	- [Any values](#any-values)
	- [Nullable values](#nullable-values)
	- [Pointers](#pointers)
	- [Tagged unions](#tagged-unions)
		- [Narrowing](#narrowing)
	- [String literals](#string-literals)
	- [Function types](#function-types)
	- [Static-sized arrays](#static-sized-arrays)
	- [Slices](#slices)
- [Variables](#variables)
	- [Union extraction and propagation](#union-extraction-and-propagation)
	- [Temporaries](#temporaries)
- [Statements](#statements)
	- [Blocks](#blocks)
	- [Assignment](#assignment)
	- [If / else](#if--else)
	- [Match](#match)
	- [Compile-time branches](#compile-time-branches)
	- [While](#while)
	- [For](#for)
	- [Custom iterators](#custom-iterators)
	- [Break / continue / labels](#break--continue--labels)
	- [Defer](#defer)
	- [Return](#return)
- [Expressions](#expressions)
	- [Literals](#literals)
	- [Arithmetic](#arithmetic)
	- [Integer overflow](#integer-overflow)
	- [Casts](#casts)
	- [Sizeof and alignof](#sizeof-and-alignof)
	- [Comparison and boolean operators](#comparison-and-boolean-operators)
		- [Slice equality](#slice-equality)
	- [Operator precedence](#operator-precedence)
	- [Ternary operator](#ternary-operator)
	- [Calls](#calls)
		- [Panic](#panic)
	- [Argument-dependent lookup](#argument-dependent-lookup)
	- [UFCS](#ufcs)
- [Memory](#memory)
- [Compile-time introspection](#compile-time-introspection)
	- [typeof](#typeof)
	- [Type members](#type-members)
	- [TypeKind](#typekind)
	- [Type equality](#type-equality)
	- [Reflection sequences](#reflection-sequences)
	- [Unroll for](#unroll-for)
	- [Field iteration](#field-iteration)
	- [Enum iteration](#enum-iteration)
	- [Union iteration](#union-iteration)
	- [Function type introspection](#function-type-introspection)
	- [What an unrolled loop binds](#what-an-unrolled-loop-binds)
	- [Comptime-to-runtime materialization](#comptime-to-runtime-materialization)
	- [Instantiation limits](#instantiation-limits)
- [Runtime model](#runtime-model)
	- [Native functions](#native-functions)
- [Diagnostic](#diagnostic)
- [Known underspecified areas](#known-underspecified-areas)
- [Design decisions](#design-decisions)

## Quick example

```cpp
import "core:vec3"

struct Player {
	hp: i32;
	height: f32;
}

fn add(a : Vec3, b : Vec3) : Vec3 {
	const x = a.x + b.x;
	const y : f32 = a.y + b.y;
	const z = a.z + b.z;
	return { x, y, z };
}

// comment
fn main() : void {
	var a : Vec3 = { 10, 20, 30 };
	const b = Vec3 { 40, 50, 60 };
	a = add(a, b);

	var i : i32 = 10;
	while i > 0 {
		a = Vec3 { a.x + i as f32, a.y, a.z };
		i -= 1;
	}
}
```

- A unit contains top-level `import`, `comptime`, `struct`, `extern struct`, `enum`, `fn`, `extern fn`, and variable declarations.
- Whitespace is not significant.
- Line comments start with `//`.

## Declarations

### Imports

Imports load another unit by path.

```cpp
import "player"                // project-local file
import "weapon" as w           // import with alias
import "std:math"              // built-in langauge import
import "core:collections/list" // only std: is reserved
```

* The `.evox` filename suffix is omitted.
* Imports map source names to declarations in imported units. They are lookup sources, not scope injection.
* Imported units also contribute operator declarations to overload resolution.
* Import aliases qualify ordinary names such as functions and types; operators are selected automatically from the imported declarations when resolving expressions such as `a + b`.

Symbol lookup:

```cpp
import "core:vec4"
import "core:vec3"
import "core:vec2" as my_vec

fn main() : f32 {
	const v4 = Vec4 { 1, 2, 3, 4};
	const v3 = Vec3 { 1, 2, 3 };
	const v2 = my_vec.Vec2 { 4, 5 };
	
	// Vec2 without my_vec qualifier can not be resolved
	// const v2_fail = Vec2 { 4, 5 }; 
	
	// compile time error if length is defined in both core:vec3 and core:vec4
	// no overloading
	// var l = length(v3); 
	return v3.x + v2.x;
}
```

Builtin math functions live under `std:`. Use `std:math` for `sin`, `cos`, and `sqrt`.
The `std:` prefix is reserved for builtin modules and cannot be used for user-defined imports.

Rules:

- alias collisions are compile-time errors
- import cycles are compile-time errors
- imports are not transitive for symbol visibility
- an alias-qualified name resolves only through that alias
- a bare name resolves against the current module and then unaliased imports
- if no match is found and the call has at least one argument, the first argument's type namespace is also searched (see [Argument-dependent lookup](#argument-dependent-lookup))
- if a bare name matches more than one declaration, using it is a compile-time error
- unaliased imports are not a separate namespace and do not override local declarations

```cpp
import "core:vec3" as core
import "core:quat" as core // compile-time error: alias collision
```

```cpp
// a.evox
import "b"

// b.evox
import "a" // compile-time error: import cycle
```

### Structs

`struct` declares a named, nominal product type. A struct value contains one
value for each declared field, in declaration order:

```cpp
struct Transform {
	x : f32;
	y : f32;
	visible : bool;
}
```

The declaration binds `Transform` as a compile-time type value. Anonymous
struct types can also be created with `struct { ... }` in a compile-time
expression; see [Comptime](#comptime).

#### Fields

- fields are declared as `name : type;`; the semicolon after every field is
  required
- field names must be unique within the struct
- fields are ordered; that order is used by positional literals, reflection,
  and the runtime representation
- non-`extern` struct layout is implementation-defined; do not rely on its
  size, alignment, padding, or field offsets for native ABI interop
- a field type may be any valid concrete type, including a primitive, enum,
  function type, array, slice, nullable, union, or another struct
- a struct type must be available when it is used as a field type; declarations
  are not forward declarations
- recursive containment by value is not allowed, including through arrays,
  nullable types, unions, or another struct; use an indirection such as a
  pointer or slice where appropriate
- an empty struct is valid and has no fields
- a trailing semicolon after the closing `}` is a compile-time error

Two separate struct declarations are different nominal types even when their
fields have identical names and types. Structs do not receive implicit casts to
or from other structs.

#### Extern structs

`extern struct` declares a nominal struct whose runtime layout matches an
existing native C struct using the target C ABI. It keeps the ABI boundary
explicit and restricts fields to C-ABI-compatible types. Use it for native
interop when script code needs to read or write fields of a C struct directly:

```cpp
extern struct Vec2 {
	x : f32;
	y : f32;
}

extern fn length(v : *const Vec2) : f32;
```

Rules:

- syntax is the same as `struct`, prefixed by `extern`
- field order is declaration order and follows the target C ABI for size,
  alignment, padding, and field offsets
- field types must be C-ABI-compatible Evox types, such as primitives,
  pointers, `cptr`, `cstr`, other `extern struct` types, or static arrays of
  compatible element types
- slices, tagged unions, function values, ordinary script-only structs, and
  nullable value types are not C-ABI-compatible fields
- recursive containment by value is not allowed; use a pointer field for C
  self-referential structs
- `sizeof`, `alignof`, field access, pointers, and passing by value or pointer
  use the extern C ABI layout
- `extern struct` is still a distinct nominal Evox type; it does not
  implicitly convert to another struct with the same fields
- no C header is imported automatically; the declaration must exactly match the
  native definition used by the host application

#### Values

Struct values are constructed with positional literals. The values correspond
to fields in declaration order, and every field must be supplied:

```cpp
const t : Transform = { 1.0, 2.0, true };
const u = Transform { 3.0, 4.0, false };
var empty : Transform = undefined;
```

- `{ ... }` requires an expected struct type from its context
- `Type { ... }` supplies the struct type explicitly
- the literal must contain exactly one value per field
- each value is checked against its field type; untyped numeric literals may
  be concretized to that type
- named-field literals are not supported

#### Access and assignment

Fields are selected with `.`, and the selected field is an lvalue when the
base value is writable:

```cpp
fn set_visible(t : *Transform) : void {
	t.visible = true;
}

fn is_visible(t : Transform) : bool {
	return t.visible;
}
```

Nested fields can be chained (`object.transform.x`). A field of a temporary
struct value is readable but cannot be assigned to or used to form a pointer. A
struct passed by value is a copy; use a pointer parameter to modify the
caller's value. Fields can also be selected with a compile-time string:

```cpp
value["field"] = 42;
```

`value["field"]` selects the same field and has the same lvalue behavior as
`value.field`; it is not runtime map lookup. The index must evaluate to a
compile-time `[]const u8`, and its bytes must exactly match a declared field
name. The selected field is not a copy, and names such as
`"position.x"` are not interpreted as nested paths.

Structs have no built-in equality or ordering. Operators for a struct must be
provided with `operator` declarations, subject to the rules in [Operators](#operators).
Struct fields themselves retain their declared types; there is no automatic
conversion of a whole struct.

#### Attributes

Attributes are typed metadata attached to declarations. They can be attached
to a type:

```cpp
#[some_tag]
struct Foo {}
```

or to a field:

```cpp
struct range_attr {
	min : f32;
	max : f32;
}

struct label_attr {
	value : []const u8;
}

struct Settings {
	#[label_attr {"Radius"}, range_attr {0, 100}]
	radius : f32;
}
```

An attribute is declared as an ordinary struct. Its arguments are a positional
struct value, so the attribute name and its payload are checked by the
compiler. Attributes may therefore carry different, user-defined payload types
and multiple attributes may be attached to the same declaration.

Attributes are compile-time metadata. They can be inspected by compile-time
code and are intended for uses such as serialization, GUI descriptions (labels,
ranges, and similar information), and tagging. 

Evox does not provide runtime reflection over attributes yet. Runtime
script code cannot enumerate attributes or read their values; native C++ code
is the current runtime consumer.

### Enums

Enums define a named set of integer-like constants:

```cpp
enum State {
	Idle,
	Running,
	Paused = 42, // explicit values are allowed
	Done // == 43
}

// const fail : i32 = Keycode.W; // compile-time error, no implicit conversion
const key_code : i32 = Keycode.W as i32;

```

* Enums are strongly typed 
	- no implicit conversion between enums and integers
	- use explicit `as` casts when needed
* a trailing semicolon after the closing `}` is a compile-time error
* Shorthand member syntax works when enum type is unambiguous:
	```cpp
	fn handle_state(state : State) : void {
		if state == .Running {
			// equivalent to state == State.Running
		}
	}

	var priority : Priority = .High;
	```

### Functions

Functions are executable values. A function can be declared with a name or
created as an anonymous function literal. Every function has a parameter list
and a return type.

```cpp
fn clamp_min(v : i32, min_value : i32) : i32 {
	if v < min_value {
		return min_value;
	}
	return v;
}
```

The `fn name(...) : T { ... }` form declares a named module-level function.
The function name is available for calls and recursion:

```cpp
fn factorial(n : i32) : i32 {
	if n <= 1 { return 1; }
	return n * factorial(n - 1);
}
```

An anonymous function literal can be stored in a binding, passed to another
function, returned, or called indirectly:

```cpp
const add = fn(a : i32, b : i32) : i32 {
	return a + b;
};

fn apply(f : fn(i32, i32) : i32, a : i32, b : i32) : i32 {
	return f(a, b);
}

const result = apply(add, 2, 3);
```

A final parameter may use variadic syntax. `...T` is syntactic sugar for a
slice parameter `[]T`; inside the function it behaves as a slice of `T`:

```cpp
fn sum(values : ...i32) : i32 {
	var result : i32 = 0;
	for value in values {
		result += value;
	}
	return result;
}

sum();
sum(1, 2, 3);
```

Inside the function, a variadic parameter is an ordinary slice. Additional declaration and call rules are listed below.

The type of a function value is written `fn(parameters) : return_type`; see
[Function types](#function-types). Since `...T` is sugar for `[]T`, a variadic
function value has the corresponding slice function type and retains the
variadic call syntax when its static type uses `...T`.

Rules:

- parameter names must be unique
- a parameter binding is immutable; a pointer parameter can still modify the
  pointed-to storage when its pointee is writable
- non-variadic arguments must match the declared parameter types and count
- a variadic parameter accepts zero or more arguments matching its element type
- variadic syntax is only valid on the final parameter
- a variadic parameter cannot be `comptime`
- there are no overloaded function declarations
- a non-`void` function must return a value compatible with its return type;
  `void` functions may use `return;`
- named `fn name(...) : T { ... }` declarations are module-level; nested
  declarations in that form are not supported
- function literals are expressions and can be bound locally with `const` or
  `comptime`, for example `comptime helper = fn(v : i32) : i32 { return v; };`
- a function literal does not need its own name; the binding name, if any,
  names the function value
- function declarations do not include `operator` declarations; operators are
  a separate declaration form

```cpp
struct Stats {
	hp : i32;
};

struct Player {
	stats : Stats;
};

var global_counter : i32 = 0;

fn bump(v : *i32) : void {
	v.* += 1;
}

fn main() : void {
	var p = Player { Stats { 10 } };
	bump(&global_counter);
	bump(&p.stats.hp);
}
```

### Comptime

`comptime` declares an immutable binding whose initializer is evaluated during
compilation. It is valid at module scope and inside a function body:

```cpp
comptime global_limit = 32;

fn main() : i32 {
	comptime local_limit = 8;
	return global_limit + local_limit;
}
```

The initializer must produce a value the compiler can know without reading
runtime storage. A runtime-only value in a `comptime` initializer is an error:

```cpp
var runtime_value : i32 = 16;
comptime invalid = runtime_value; // compile-time error
```

#### Compile-time values

Primitive expressions, types, and function values can be bound with
`comptime`:

```cpp
comptime count = 32;
comptime enabled = true;
comptime Vec2 = struct { x : f32; y : f32; };
comptime State = enum { Idle, Running };
comptime add = fn(a : i32, b : i32) : i32 {
	return a + b;
};
```

Type values exist only during compilation. Function values produced by a
`comptime` binding can still be called by runtime code:

```cpp
fn main() : i32 {
	return add(20, 22);
}
```

An unannotated numeric `comptime` binding remains an untyped compile-time
constant until a later use supplies a concrete numeric type. An explicit
annotation fixes its type immediately:

```cpp
comptime value = 12;
comptime narrow : i16 = 12;
```

#### Compile-time calls

A `comptime` initializer may call a function that is known at compile time:

```cpp
fn double(value : i32) : i32 {
	return value * 2;
}

comptime result = double(16); // 32
```

Functions returning `type` can construct types during compilation. These type
factories are documented with generic functions in [Templates](#templates).

Rules:

- `comptime` bindings are immutable and are not runtime storage
- a binding may contain a primitive value, type, function value, aggregate,
  or another compile-time value
- a runtime value cannot be used where a compile-time value is required
- a compile-time call can call only a compile-time-known function value
- a type value has no runtime representation; parameters of type `type` are
  handled during compilation
- compile-time evaluation has an implementation-defined recursion and step
  limit

### Templates

Templates are functions specialized at compile time. They can have ordinary
runtime parameters, inferred or explicit type parameters, compile-time value
parameters, or any combination of these. Each distinct set of compile-time
arguments produces a concrete instantiation; ordinary arguments remain
runtime values and there is no runtime template-dispatch overhead.

#### Inferred type parameters

Prefix `$` introduces an inferred type parameter. After its first occurrence,
use the parameter name without `$`:

```cpp
fn identity(value : $T) : T {
	return value;
}

fn swap(a : *$T, b : *T) : void {
	const temporary = a.*;
	a.* = b.*;
	b.* = temporary;
}

fn main() : void {
	const integer = identity(42); // T is i32
	const decimal = identity(3.14); // T is f32

	var a : i32 = 1;
	var b : i32 = 2;
	swap(&a, &b); // T is i32
}
```

Type parameters are inferred from argument types, not from the expected
return type. The same parameter name cannot be introduced with `$` more than
once in a signature, and parameter names must be unique.

#### Explicit type parameters

A parameter annotated with `type` is supplied explicitly as a type argument:

```cpp
fn make(T : type) : T {
	return undefined;
}

fn main() : void {
	const value : i32 = make(i32);
}
```

Type parameters may be combined with inferred parameters:

```cpp
fn first(a : $A, b : $B) : A {
	return a;
}
```

#### Type factories

A function returning `type` is a type factory. Calling it produces a concrete
type, and the call uses ordinary parentheses in type positions and struct
literals:

```cpp
fn Pair(T : type) : type {
	return struct {
		first : T;
		second : T;
	};
}

fn main() : void {
	var integers : Pair(i32) = Pair(i32) { 1, 2 };
	var decimals : Pair(f32) = Pair(f32) { 1.0, 2.0 };
}
```

A fully instantiated factory result, such as `Pair(i32)` or
`Box(Pair(i32))`, is a concrete type and can be used in variable declarations,
parameters, return types, and struct fields. Factory calls may also be made
through imported module aliases, for example `lib.Pair(i32)`.

#### Compile-time value parameters

A parameter annotated with `comptime` must receive a compile-time value at the
call site. This permits dependent return types and array sizes:

```cpp
fn splat(value : f32, count : comptime i32) : [count]f32 {
	var result : [count]f32 = undefined;
	for i in 0..count {
		result[i] = value;
	}
	return result;
}

fn main() : void {
	const values = splat(1.0, 4); // [4]f32
}
```

Compile-time value parameters can be combined with type parameters, including
in a type factory: `fn array_type(T : type, N : comptime i32) : type`.

#### Rules

- all template arguments are resolved and checked during compilation
- inferred type parameters come from value arguments; an expected return type
  does not provide inference
- template arguments must satisfy the requirements of the instantiated body
- a factory-produced type is concrete only after all its arguments are known
- recursive factory-produced structs are invalid when recursion requires an
  inline value; recursion through a slice is allowed
- a concrete template instantiation can be assigned, passed, returned, or
  stored wherever its function type is valid
- imported templates use the same alias-qualified call syntax as ordinary
  functions
- operator declarations cannot be templates

### Operators

Operator overloads are declared as top-level functions with the `operator` keyword:

```cpp
operator +(a : Vec3, b : Vec3) : Vec3 {
	return Vec3 { a.x + b.x, a.y + b.y, a.z + b.z };
}

operator -(a : Vec3) : Vec3 {
	return Vec3 { -a.x, -a.y, -a.z };
}
```

Rules:

- operators are declared at top level
- operator names are fixed tokens, not identifiers
- overloadable operators are:
  - `+`
  - `-`
  - `*`
  - `/`
  - `%`
  - `==`
  - `!=`
  - `<`
  - `<=`
  - `>`
  - `>=`
  - unary `-`
- `and`, `or`, and `not` remain built-in boolean operators and are not overloaded
- declaring an operator overload for a built-in primitive signature, such as `operator +(f32, f32)`, is a compile-time error
- declaring `operator ==` or `operator !=` with slice parameters is a compile-time error for the same reason; slice equality is built in (see [Slice equality](#slice-equality))
- declaring an operator overload where any parameter is an enum type is a compile-time error; use a wrapper struct for bit-flag patterns instead
- overload resolution uses exact type matching on the operands' natural types
- an untyped numeric expression matches a parameter when all of its literals fit and adopts that parameter type
- a typeless struct literal operand `{ ... }` cannot select an operator overload; write its type explicitly, such as `Vec2 { ... }`
- no implicit casts are performed to make an operator applicable
- imported modules participate in operator lookup
- if multiple declarations match equally well, the expression is ambiguous and is a compile-time error
- primitive built-in operator behavior still applies when no overload is involved
- compound assignment on a non-primitive left-hand target uses the corresponding binary operator, for example `x += y` behaves like `x = x + y`
- compound assignment evaluates the left-hand side once // TODO test
- primitive compound assignment means the left-hand target has a primitive type; it keeps the built-in behavior and cannot be overridden
- for primitive compound assignment, the right-hand operand must be implicitly convertible to the left-hand target type; for example, `5 *= Vec2 { 1, 2 }` is invalid because `Vec2` cannot be converted to the integer type of `5`

## Types

Built-in and user types:

- `void`
- `bool`
- `i8`, `u8`, `i16`, `u16`, `i32`, `u32`, `i64`, `u64`
- `isize`
- `byte`
- `f32`, `f64`
- `cstr`
- `cptr`
- typed pointers (`*T`, `*const T`, `?*T`, and `?*const T`; see [Pointers](#pointers))
- `type` (compile-time only)
- `TypeKind` (compile-time only, see [TypeKind](#typekind))
- user-defined `struct` types
- user-defined `enum` types
- function types
- tagged union types (`A | B`)
- `any` values (see [Any values](#any-values))

`isize` is the signed integer type used for memory sizes, slice lengths, and indices. It is signed and a fixed 64 bits on all targets (not platform/pointer-width dependent).

- it is the parameter type of the raw-memory allocator's size/alignment arguments, the result type of array/slice `.length`, and the type used for indexing
- indexing is bounds-checked against `0 <= i < length`, so a negative index is a runtime error

`byte` is the smallest addressable unit of raw memory. It is distinct from `u8`: `u8` is a numeric type with a fixed width, while `byte` represents untyped storage. `sizeof` and `alignof` are measured in bytes. The raw-memory allocator works in terms of `[]byte` (a byte slice), and `[]byte` can be reinterpreted as a typed slice (see [Casts](#casts)).

### Untyped literals

Numeric literals begin without a concrete numeric type:

- integer literals are untyped integers
- decimal literals are untyped floats
- arithmetic made entirely from untyped numeric constants remains untyped, so
  `12 + 13` is an untyped integer constant with value `25`

The compiler concretizes an untyped numeric expression when its surrounding
context supplies a type. This includes an explicit annotation, assignment
target, function parameter (including a `comptime` parameter), function return
type, array or struct literal field, concrete arithmetic operand, ternary
branch, range bound, numeric pattern, or explicit cast:

```cpp
fn takes_i16(value : i16) : void {}

const a : i16 = 12;
var b : i16 = 0;
b = 12;
takes_i16(12);
```

An unannotated `comptime` binding is deliberately different: its numeric
initializer remains untyped until a later use supplies a type. An explicit
annotation concretizes it immediately:

```cpp
comptime deferred = 12;
comptime fixed : i16 = 12;
```

When no context supplies a type, the compiler uses these defaults:

- integers use `i32` when representable, then `i64`, then `u64`
- integers that do not fit `u64` are compile-time errors
- decimal literals use `f64`
- untyped range bounds and comparisons use `i32`

A union target supplies numeric context only when exactly one member has a
compatible numeric category. Representability does not resolve ambiguity:

```cpp
const number : i32 | []const u8 = 12; // 12 becomes i32
const ambiguous : i32 | i64 = 2147483648; // compile-time error: both numeric members match
```

Concretization requires the value to be representable by the selected type; it
is not an implicit cast between already-concrete numeric types. `typeof` is
not a concretizing context, so `typeof(1)` and `typeof(deferred)` are errors.
Cast or annotate the expression first.

### Any values

`any` is a runtime type-erased, non-owning value. It stores the concrete runtime type together with a pointer to the original value storage; it does not copy or own the payload.

```cpp
fn handle(value : any) : void {
	match value {
		case i32:
			// value is promoted to i32 in this arm
			print(value);
		case []const u8:
			print(value);
		case:
			print("unsupported value");
	}
}
```

Only runtime-materializable values can be assigned to `any`. When the source is an rvalue or literal, the compiler materializes a hidden temporary and `any` points to that temporary. The temporary remains valid for the lifetime of the `any` value that refers to it. Existing lvalues are referenced directly.

`any` is only consumed through [`match`](#match). It does not support direct member access, operators, `is`, or `as` conversions. A match case names a concrete type and performs an exact runtime type comparison. The subject is promoted to that type inside the selected arm. Since the set of possible runtime types is open, a match on `any` must contain an unpatterned `case:` fallback; the fallback body may contain ordinary statements.

An `any` reference must not outlive the value or temporary it references. Returning or storing an `any` whose source storage cannot remain valid is a compile-time error. `any` does not extend the lifetime of pointers, slices, or other referenced data stored in its payload.

### Nullable values

Nullable syntax uses `?Type`.

```cpp
fn find_entity() : ?entity.Entity {
	return null;
}

fn main() : void {
	const e = find_entity();
	if e != null {
		// e is promoted to entity.Entity in this branch
	}
}
```

Promotion also continues after a guard branch that always returns. In this case the only path that reaches `e` is the non-null one:

```cpp
if e == null { return; }

use_entity(e); // e is promoted to entity.Entity
```

The same promotion applies when the `else` branch returns:

```cpp
if e != null {
	use_entity(e);
} else {
	return;
}

use_entity(e); // e is promoted to entity.Entity
```

A nullable value can also use the `else return` declaration form. The target
annotation is optional and defaults to the nullable type's non-null inner type.
The non-null value initializes the variable; the null case returns immediately:

```cpp
fn load_entity() : void {
	var e = find_entity() else return;
	use_entity(e); // e is non-null here
}
```

Using a nullable value without a required null check is a compile-time error.

**Nullable values are not fully safe**

The null check only applies to the value as it exists at that point in the control flow; if the variable is reassigned or otherwise mutated afterward, the earlier check does not keep later uses safe.

```cpp
if a != null {
	foo(a);
	a = bar();
	foo(a); // unsafe unless `bar()` is guaranteed to return a non-null value
}

if a != null {
	bar(); // `bar` may mutate `a`
	foo(a); // unsafe if `bar` can clear or replace `a`
}
```

### Pointers

Typed pointers provide persistent, non-owning indirection. They are distinct
from parameter aliases: a pointer is a value that can be stored in variables
and struct fields.

Pointer types use prefix `*`, while dereference uses postfix `.*`:

```cpp
struct S {
	field : i32;
}

fn set_field(s : *S) : void {
	s.field = 42;
}

fn main() : void {
	var value : S = undefined;
	var p : *S = &value;
	p.field = 10;
	const copy : S = p.*;
}
```

Pointer rules:

- `*T` is a non-null pointer to `T`; `?*T` is a nullable pointer to `T`
- `&value` takes the address of addressable runtime storage and produces a
  `*T` or `*const T` according to the storage's mutability
- `pointer.*` reads or writes the pointed-to value; it is an lvalue when the
  pointer refers to writable storage
- `*const T` points to a read-only `T`; dereferencing it with `pointer.*` produces a read-only
  value and cannot be used as an assignment target
- `const p : *T` makes the pointer binding immutable, but the `T` reached
  through `p` remains writable
- field selection with `.` automatically dereferences a non-null pointer, so
	`p.field` is equivalent to `p.*.field`
- nullable pointers must be checked before dereference or field selection;
  `if p != null` promotes `p` to `*T` in that branch
- `null` is valid for `?*T`, but not for `*T`
- pointers are copied by value and do not copy their pointees
- `*T` converts implicitly to `*const T`; the reverse conversion is invalid
- the same conversion applies through nullability: `?*T` converts to
  `?*const T`
- pointers do not imply ownership, allocation, or automatic lifetime
- pointer equality compares addresses; pointer ordering and pointer arithmetic
  are not defined
- taking the address of a local or parameter does not extend its lifetime;
  returning or storing such a pointer after the storage expires is a
  compile-time error
- recursive containment through a pointer is allowed; recursive containment
  by value remains invalid

For example, a linked-list node can point to another node:

```cpp
struct Node {
	value : i32;
	next : ?*Node;
}
```

Read-only links can use a const pointee:

```cpp
struct View {
	root : ?*const Node;
}
```

Pointer allocation and ownership are separate from the pointer type. The
language does not implicitly free or retain pointees; an allocation API and
its lifetime rules must be used when pointers outlive their source variables.

### Tagged unions

A union type is written as members separated by `|`. A union value holds exactly one of its member types at a time; a tag records which one (the active variant). The member type itself is the tag - there are no named variants.

```cpp
struct ButtonEvent {
	button : Button;
}

struct MouseMoveEvent {
	x : i32;
	y : i32;
}

comptime InputEvent = ButtonEvent | MouseMoveEvent;

var b : ButtonEvent = foo();
var e : InputEvent = b;                        // active variant becomes ButtonEvent
var e2 : InputEvent = MouseMoveEvent { 0, 1 }; // active variant becomes MouseMoveEvent
e = MouseMoveEvent { 1, 0 };                   // e's active variant switches to MouseMoveEvent
```

Union type expressions are compile-time type values like any other type, so they can be bound with `comptime` or written anonymously anywhere a type is valid: variable declarations, parameters, return types, and struct fields.

```cpp
fn parse(source : Code) : Error | ASTNode;
```

**Identity**

Unions are structural with set semantics:

- member order does not matter: `A | B` and `B | A` are the same type
- a union member that is itself a union flattens: if `comptime AB = A | B`, then `AB | C` is `A | B | C`
- two union types with the same member set are the same type, regardless of how or where they were spelled
- duplicate members collapse into the set, whether written directly or introduced by flattening: `A | A` is `A`, and if `comptime AB = A | B` and `comptime BC = B | C`, then `AB | BC` is `A | B | C`

**Members**

Every union member must be a concrete runtime type. This includes structs, enums, primitives, nullable types, slices, static arrays, and function types.

`void` cannot be a member because it has no runtime value or value syntax for selecting a `void` union variant. Union types are not members themselves; they flatten into their constituent members, as described above.

All members must be pairwise distinct types. Because the member type is the tag, two semantically different variants with the same payload type require wrapper structs.

**Coercion**

- a member value coerces implicitly to any union containing its type: `var e : InputEvent = b;`
- a union value coerces implicitly to any union whose member set is a superset (the tag is remapped at the coercion site): an `A | B` value can be assigned where `A | B | C` is expected - this is what lets error unions propagate across call layers
- no other implicit conversions apply; narrowing (superset to subset, or union to member) is never implicit

**Testing: `is`**

- `e is ButtonEvent` evaluates to `bool`: whether the active variant is `ButtonEvent`
- `if e is ButtonEvent { ... }` promotes `e` to `ButtonEvent` inside the branch, like nullable promotion in `if e != null`; the `else` branch and the code after an early return narrow it too (see [Narrowing](#narrowing))
- `is` with a type that is not a member of the union is a compile-time error

#### Narrowing

A branch that rules out some members narrows the value to the **residual type**: the subject's member set minus the members the branch excluded. With one member left the residual is that member type, and narrowing to it is the promotion described above; with several left it is the smaller union.

```cpp
comptime Shape = Circle | Square | Triangle;

fn area(s : Shape) : f32 {
	if s is Circle {
		return s.r * s.r * 3.14159; // s is Circle
	} else {
		// s is Square | Triangle
		if s is Square {
			return s.w * s.w;       // s is Square
		}
		return s.b * s.h * 0.5;     // s is Triangle: last member left
	}
}
```

The same subtraction applies after a branch that always leaves the enclosing scope, so an early return narrows the code that follows it:

```cpp
fn handle(e : Error | Warning | Value) : void {
	if e is Error { return; }
	// e is Warning | Value here
	if e is Warning { log_warning(e); return; }
	use(e); // e is Value
}
```

Rules:

- narrowing applies to the `if` branch, the `else` branch, and the statements after a branch that always exits the scope (`return`, `break`, `continue`), matching the [nullable](#nullable-values) guard forms
- the residual of an `else if` chain accumulates: each arm narrows against what the arms before it already excluded
- narrowing to an empty member set is a compile-time error; it means the condition can never hold
- only a bare `e is T` on a named subject narrows. A negated or compound condition (`not (e is T)`, `e is T and flag`) is not analyzed, and the subject keeps its declared type in both branches
- a narrowed subject keeps the residual type for member access, `is`, `match`, and [`typeof`](#typeof)
- narrowing is flow-typing with the same accepted unsoundness as promotion: assigning to the subject inside a narrowed region is allowed and is not re-checked (see [Nullable values](#nullable-values))

**Match**

`match` on a union value branches on the active variant. Cases are member types; the subject is promoted to the case's member type within that case.

```cpp
match e {
	case ButtonEvent:
		print(e.button); // e reads as ButtonEvent here
	case MouseMoveEvent:
		print(e.x, " ", e.y);
}
```

Rules:

- a union match must be exhaustive unless an empty `case:` fallback is present
- duplicate member cases are compile-time errors
- a case pattern that is not a member type of the subject is a compile-time error
- an unqualified member type resolves against the union first, so `case ButtonEvent:` works even if the union was declared with `events.ButtonEvent`; qualify it only to disambiguate members with the same name
- comma-separated alternatives (`case A, B:`) narrow the subject to `A | B`, the [residual](#narrowing) of that arm, rather than promoting it to a single member type
- inside an empty `case:` fallback the subject narrows to the residual of every member matched by the arms above it, so a fallback after `case A:` on an `A | B | C` subject reads the subject as `B | C`
- promotion is flow-typing, same as nullable promotion: assigning to the subject inside a case (which may switch the active variant) is allowed and is not re-checked; the earlier promotion does not keep later uses safe (see [Nullable values](#nullable-values) for the analogous caveat)

**Namespaces, ADL, and UFCS**

A structural union has no declaring namespace. Union-typed values do not participate in [argument-dependent lookup](#argument-dependent-lookup), and method syntax on a union receiver resolves only against the current module and unaliased imports.

**Initialization and literals**

- `var e : InputEvent = undefined;` is allowed: the tag is zeroed (the active variant becomes the union's canonical-first member, which is implementation-defined) and the payload is undefined
- a typeless struct literal cannot pick a variant: `var e : InputEvent = { 0, 1 };` is a compile-time error; write the member type explicitly (`MouseMoveEvent { 0, 1 }`)

**Comparison**

`==` and `!=` are not defined for union values; comparing two unions is a compile-time error. Narrow first with `is` or `match`, then compare the payloads.

**Layout**

- storage is a tag followed by payload space sized for the largest member
- the tag is an `i32` holding the member's index in the union's canonical member order; the canonical order is deterministic but implementation-defined (member sets are unordered at the language level)
- `sizeof(A | B)` is `sizeof(i32) + max(sizeof(members))`
- `alignof(A | B)` is `max(alignof(i32), alignof(members))`
- the tag is not directly observable; there is no union-to-integer cast

### String literals

A string literal has type `[]const u8`. It is a read-only slice over
statically allocated UTF-8 bytes, and its length does not include the trailing
NUL byte stored for native interoperability:

```cpp
fn greet(name : []const u8) : void {
	print("Hello ");
	print(name);
}

const text : []const u8 = "Lumix";
const bytes = "a\0b"; // bytes.length == 3
```

String literals have no dedicated runtime type. They use ordinary slice
operations such as indexing, slicing, iteration, and `.length`. Their elements
cannot be modified because the slice is `const`.

There is no built-in concatenation operator. In particular, `"a" + "b"` is a
compile-time error. Code that needs concatenation must copy the bytes into an
owned container supplied by a library or application allocator.

Interpolated string literals are delimited by backticks and are supported when
passed as function-call arguments. Ordinary string literals treat braces as
literal characters. In an interpolated string, an expression enclosed in `{}`
is emitted as a separate argument, with the literal text before and after it
emitted as `[]const u8` arguments. Use `{{` for one literal `{`; `}` is literal
outside an interpolation:

```cpp
fn foo(a : i32, prefix : []const u8, value : i32, suffix : []const u8, c : f64) : i32 {
	return value;
}

fn main() : i32 {
	var value : i32 = 42;
	return foo(42, `some {value} abc`, 69.0);
}
```

The call above is equivalent, for argument checking, to
`foo(42, "some ", value, " abc", 69.0)`. Interpolated expressions use the
normal expression syntax, including operators, function calls, member access,
and indexing. Braces cannot be nested inside an interpolation.

Slices, including `[]const u8`, compare by content with `==` and `!=`, so
string literals and byte slices can be compared directly:

```cpp
fn is_quit(command : []const u8) : bool {
	return command == "quit";
}
```

See [Slice equality](#slice-equality) for the exact rule.

`cstr` is a distinct borrowed, NUL-terminated C string type for native interop. It maps to `const char*` in C and is intended for declarations such as:

```cpp
extern fn puts(text : cstr) : i32;

fn main() : void {
	puts("hello");
}
```

String literals convert implicitly to `cstr` without copying because their
backing storage is NUL-terminated:

```cpp
var native_text : cstr = "hello";
```

Literals containing an embedded null byte cannot convert to `cstr`; C APIs
would stop at the first null and could not observe the complete slice. A
general `[]const u8` does not implicitly convert to `cstr`, because a slice
does not guarantee a trailing NUL byte. The language currently provides no
conversion for this case; only string literals can be passed directly where a
`cstr` is expected. `cstr` has no ownership or freeing behavior.

There is no built-in conversion from `cstr` to `[]const u8`: determining the
length requires a scan, and the resulting lifetime would depend on native
storage. Native-facing libraries can expose that operation with an explicit
lifetime policy.

`cptr` is a separate opaque raw native pointer type. Use it for handles, raw memory, and dynamic-library symbol lookup; do not use it for C text when a `cstr` parameter is available. The `null` literal is valid wherever a `cptr` is expected and represents a null native pointer:

```cpp
extern fn MessageBoxA(window : cptr, text : cstr, caption : cstr, flags : u32) : i32;

MessageBoxA(null, "Hello", "Evox", 0);
```

### Function types

Function type syntax:

```cpp
fn(i32, i32) : i32
fn(a : i32, b : i32) : void
fn([]i32) : i32
fn(prefix : cstr, values : ...i32) : void
```

Parameters may optionally be named. Named and unnamed parameters can be mixed in the same function type. `fn(...i32) : i32` has the same ABI as `fn([]i32) : i32`, but enables variadic argument packing at call sites.

Example:

```cpp
fn add(a : i32, b : i32) : i32 {
	return a + b;
}

fn multiply(a : i32, b : i32) : i32 {
	return a * b;
}

fn apply(f : fn(i32, i32) : i32, a : i32, b : i32) : i32 {
	return f(a, b);
}

fn choose(use_add : bool) : fn(i32, i32) : i32 {
	if use_add {
		return add;
	}
	return multiply;
}
```

Function values can be stored, passed, returned, and called.

### Static-sized arrays

Fixed-size arrays use prefix size and ordinary indexing:

```cpp
var d : [16]i32 = undefined;
d[0] = 42;
const first : i32 = d[0];
```

Array literals use brackets and infer a fixed-size array type from their
elements:

```cpp
var a = [1, 2, 3];             // inferred as [3]i32
var b : [3]i32 = [1, 2, 3];
var c : []i32 = [1, 2, 3];     // array-to-slice conversion
comptime values : []i32 = [1, 2, 3];
```

Literal rules:

- `[x, y, z]` has array type `[3]T`, where `T` is inferred from the elements
  or supplied by the expected type.
- Every element must be compatible with the inferred or expected element type.
- Assignment to `[N]T` requires exactly `N` elements.
- Assignment to `[]T` creates a slice view of the literal's backing array; it
  does not copy the elements.
- Empty array literals are not currently supported; `[N]T` requires a
  positive compile-time `N`.
- Nested literals construct nested arrays, for example
  `[[1, 2], [3, 4]]` has type `[2][2]i32`.

Type rules:

- size must be a compile-time positive integer literal
- element type is fixed for all entries
- assignment requires exact same element type and size
- index expression must have an integer type
- `.length` on a `[N]T` produces the untyped compile-time integer constant `N`, so it can be used where a compile-time integer is required (`unroll for` bounds, array sizes, `comptime` parameters). When context does not require another type it concretizes to `isize`, matching `.length` on a slice
- postfix `[` in type position (after a type constructor like size and element) means indexing or slicing on runtime values; array types always use prefix `[N]T` notation

### Slices

Slices are lightweight views over contiguous storage. A slice does not own its elements; it stores a pointer to the first element plus a length.

```cpp
var arr : [4]i32 = foo();
var slice : []i32 = arr[1:2];
```

Slice syntax uses `[]T`, where `T` is the element type. `[]const T` is a
read-only slice view over elements of type `T`:

```cpp
var values : [3]u8 = [1, 2, 3];
var writable : []u8 = values[:];
var readable : []const u8 = values[:];
```

`[]T` and `[]const T` have the same pointer-and-length representation. The
`const` qualifier applies to the viewed elements, not to the slice binding:
the binding can be reassigned, but an element cannot be written through a
`[]const T` view.

Slice creation forms:

- `arr[start:end]` creates a slice from a static array or another slice
- `value[:]` creates a one-element slice view over writable addressable runtime
  storage of type `T`
- `arr[start:]` uses the remainder of the storage to the end
- `arr[:end]` starts at the beginning
- `arr[:]` creates a slice over the whole range
- slicing uses half-open bounds: `start` is inclusive and `end` is exclusive
- omitted bounds default to the beginning or end of the source range
- slicing never copies elements
- slicing a slice produces another slice over the same backing storage
- an array can be passed to a parameter of either slice type implicitly
- a mutable `[]T` can be converted to `[]const T` without copying
- a `[]const T` cannot be converted to `[]T` implicitly
- For a scalar variable, `value[:]` creates a one-element `[]T` view, where
  `T` is the variable's type. It does not create an array or copy the value;
  `slice[0]` reads and writes the variable itself, and `slice.length` is
  always one
- creating a writable `[]T` view requires writable, addressable runtime
  storage; `const` variables and pointers to read-only values cannot be used
  as such slice sources
- a `[]const T` source may be sliced and passed to another `[]const T`
  parameter, but cannot be used to create a writable view
- a writable pointer's pointee can be used as a slice source because it refers
  to writable caller storage
- arbitrary expressions and temporaries cannot be used as writable slice
  sources; an expression specifically producing immutable backing storage may
  be used as a `[]const T` source

```cpp
var arr : [16]i32 = bar();
var x : []i32 = arr[1:2];
var y : []i32 = arr[1:];
var z : []i32 = arr[:7];
var z2 : []i32 = z[2:4];
var w : []i32 = arr[:];
var sub : []i32 = z[1:3];
var q = arr[3:4];
var read_only : []const i32 = arr[:];

fn foo(slice : []i32) : void {}
fn inspect(slice : []const i32) : void {}
foo(arr); // automatic conversion
inspect(arr); // automatic conversion to a read-only view
inspect(read_only);
```

Slice operations:

- slices can be indexed with `slice[i]`
- indexing is bounds-checked at runtime
- `slice.length` returns the number of elements in the slice
- `==` and `!=` compare two slices by content when the element type has built-in
  equality (see [Slice equality](#slice-equality))
- a slice can be initialized with `null`, which creates an empty slice
- slices can be stored in variables, passed to functions, and returned from functions
- assigning one slice to another copies only the pointer and length
- a slice remains valid only while the backing storage remains alive and stable
- writing an element through a slice mutates the viewed storage, not the slice binding, so element writes are allowed even when the binding itself is immutable (for example a function parameter of slice type)
- indexing a `[]const T` produces a read-only `T` value and assigning through it
  is a compile-time error
- immutable backing storage may be exposed as `[]const T`, but never as a
  writable `[]T` view

#### Slice ABI representation

At runtime, every slice value—including `[]const T` and string values—is a
non-owning pair laid out in declaration order:

```cpp
struct EvoxSlice {
	const void* data; // address of element 0
	isize length;     // number of elements, not bytes
};
```

The representation is exactly a pointer followed by a signed 64-bit length;
`sizeof([]T)` is therefore 16 bytes on the supported targets. The element type
is not stored in the value. `data` is an absolute address, and an empty slice
has length zero (its pointer may be null). Slicing and assignment copy only
this pair; they do not copy or take ownership of the backing storage.

This is also the representation used for slice parameters and return values of
`extern fn`. A native callback can read or write one with the C API's
`ex_slice` type and `EX_ARG`/`EX_RESULT`; it must use the declared element type
and `sizeof(T)` when interpreting the pointed-to bytes. The host must keep the
backing storage alive and stable for as long as script code can use the slice.
The `const` qualifier is a script-level access restriction and is not encoded
in the pair.

```cpp
static void native_sum(ex_runtime* runtime, ex_call_frame frame) {
	EX_ARG(frame, ex_slice, values);
	// Interpret values.data as the declared element type and use values.length.
}
```

```cpp
fn sum(values : []i32) : i32 {
	var total : i32 = 0;
	var i : i32 = 0;
	while i < values.length {
		total += values[i];
		i += 1;
	}
	return total;
}
```

## Variables

```cpp
var counter : i32 = 0;
const step = 1;
comptime max_entities = 1024; // remains an untyped compile-time constant

fn tick() : i32 {
	counter += step;
	return counter;
}
```

Rules:

- top-level variables are module globals
- `comptime` bindings exist only during compilation unless they produce runtime-callable values such as functions
- globals initialize once when runtime first runs the module
- locals use same declaration syntax
- all `var` and `const` declarations must have an initializer
- to intentionally skip initialization, use explicit `undefined` (for example `var x : i32 = undefined`)
- explicit type is optional if inference can resolve from initializer
- `var` and `const` declarations concretize untyped numeric initializers during declaration-time inference. Integers use the narrowest representable type (`i32`, then `i64`, then `u64`); untyped decimal literals infer `f64`. An unannotated `comptime` binding preserves its untyped value until a later use supplies a concrete numeric target
- `const` cannot be reassigned
- variables are block scoped
- shadowing is a compile-time error: a new declaration in the same scope or an inner scope cannot re-use a name that is already visible
- a **name** is any identifier introduced by `var`, `const`, `comptime`, `fn`, `struct`, `enum`, or `import` alias; the same rules apply to all of them

### Union extraction and propagation

A `var` declaration can extract one or more members from a tagged union and return every other member from the enclosing function:

```cpp
fn parse() : IOError | SyntaxError | Warning | ASTNode;

fn load() : IOError | SyntaxError | Warning | Result {
	var node : ASTNode = parse() else return;
	return Result { node };
}
```

The declaration

```cpp
var v : T = expression else return;
```

evaluates `expression` exactly once. Its static type must be a tagged union `U`. Treat a non-union `T` as the singleton member set `{T}`; a union `T` denotes all of its members. The member set denoted by `T` must be a nonempty proper subset of `U`'s member set.

- if the active member of the result belongs to `T`, the result narrowed to `T` initializes `v`
- otherwise, the residual value (with the same active member and payload) is returned with type `U - T`; that residual type must be implicitly convertible to the enclosing function's return type
- when `T` is a union, `v` remains tagged and has static type `T`; the tag is remapped if needed, as with ordinary union widening
- the failure path is an ordinary `return`, so applicable [`defer`](#defer) statements run
- `T` equal to `U`, a `T` containing any member absent from `U`, and a non-union initializer are compile-time errors

Subunion extraction can handle several members locally while propagating the rest:

```cpp
fn inspect() : IOError | SyntaxError | Warning | ASTNode {
	var issue : SyntaxError | Warning = parse() else return;
	return issue;
}
```

Here `issue` retains whichever of `SyntaxError` or `Warning` was active. The failure residual is `IOError | ASTNode`.

### Temporaries

Temporaries produced by expressions are immutable. Attempting to modify a temporary is a compile-time error.

```cpp
fn get_data() : MyStruct {
	return MyStruct { 1, 2, 3 };
}

fn main() : void {
	get_data().field = 5;  // compile-time error: cannot assign to temporary
	get_data().values[0] += 4;  // compile-time error: cannot assign to temporary
}
```

To modify a value, first assign it to a mutable variable:

```cpp
fn main() : void {
	var data = get_data();
	data.field = 5;  // OK
	data.values[0] += 4;  // OK
}
```

## Statements

### Blocks

```cpp
{
	var local = 1;
}
```

### Assignment

```cpp
value = 10;
position.x = position.x + 1;

value += 1;
value -= 1;
value *= 2;
value /= 2;

i++;
i--;
```

Postfix increment/decrement are statement forms equivalent to `+= 1` and `-= 1`.

### If / else

Conditions must be `bool`.

```cpp
if health <= 0 {
	return false;
} else if health < 10 {
	return true;
} else {
	return true;
}
```

### Match

`match` is a non-fallthrough multi-way branch for enum, scalar, and string values. Each `case` can contain any number of statements; execution stops at the end of the selected case and does not fall through to the next case.
An empty `case:` is the fallback arm. `else` is reserved for `if` statements.

Supported patterns:

- enum members
- scalar literals
- string literals, when matching a `[]const u8` value
- ranges (`a..=b`, inclusive on both bounds)
- member types, when matching a tagged union value (see [Tagged unions](#tagged-unions))
- concrete types, when matching an `any` value (see [Any values](#any-values))
- comma-separated alternatives
- empty `case:` fallback

```cpp
match state {
	case .Idle:
		log.logError("idle");
	case .Running:
		log.logError("running");
		update_running_state();
	case .Paused:
		log.logError("paused");
	case:
		log.logError("unknown");
}
```

```cpp
match score {
	case 0:
		log.logError("none");
	case 1..=9:
		log.logError("low");
	case 10..=99:
		log.logError("high");
	case:
		log.logError("overflow");
}
```

```cpp
match key {
	case .W, .Up:
		move_up();
	case .S, .Down:
		move_down();
	case .A, .Left:
		move_left();
	case .D, .Right:
		move_right();
}
```

String cases compare the slice contents, not the backing-pointer identity. This makes matching equivalent to a sequence of `==` comparisons and permits comma-separated alternatives:

```cpp
fn handle_command(command : []const u8) : void {
	match command {
		case "start", "run":
			start();
		case "stop", "quit":
			stop();
		case:
			log.logError("unknown command");
	}
}
```

Enum matches must be exhaustive unless an empty `case:` fallback is present. String matches cannot generally be exhaustive, so they require an empty `case:` fallback. Matches on `any` always require an unpatterned `case:` fallback; its body may contain ordinary statements. Duplicate enum, string, or `any` type cases and multiple fallback cases are compile-time errors.

### Compile-time branches

`if` and `match` become compile-time branches when their condition or scrutinee is a compile-time-known value. There is no separate syntax: the branch is resolved during compilation, one arm is selected, and **the arms that are not selected are parsed but not type-checked**.

That last rule is what makes generic code over heterogeneous types possible. The arms of a type-driven `match` are typically valid for only one `T` each:

```cpp
fn write(v : $T) : void {
	match T::kind {                   // T::kind is compile-time known
		case .Bool: io.write_bool(v);   // never checked unless T is bool
		case .I32:  io.write_i64(v as i64);
		case:       io.write_bytes(T::name);
	}
}
```

Without pruning, `io.write_bool(v)` would be a type error for every `T` that is not `bool`.

`if` behaves the same way and is the form used to special-case one concrete type:

```cpp
if T == Vec3 {
	io.write_bytes("Vec3(");   // only checked when T is Vec3
	print(v.x);
} else if T == Quat {
	print_quat(v);
} else {
	write_generic(v);
}
```

**Compile-time-known conditions**

A condition or scrutinee is compile-time known when it is built only from values the compiler already has: literals, `comptime` bindings, `comptime` parameters, factory arguments, and calls to compile-time-evaluable functions over such values. Reading a `var`, a `const` initialized from runtime data, or the result of a runtime call makes the expression runtime-valued, and the branch is an ordinary runtime branch.

Whether a branch is compile-time is a property of one specific `if` or `match`, decided by that expression alone. It is never inferred from the surrounding function: a template body still contains ordinary runtime branches wherever the condition depends on runtime values.

Rules:

- when the condition or scrutinee is compile-time known, the branch is resolved at compile time; unselected arms are parsed and must be syntactically valid, but are not type-checked and produce no code
- when it is not, the statement keeps its ordinary runtime semantics and every arm is type-checked
- a compile-time `match` otherwise follows all [Match](#match) rules: non-fallthrough, comma-separated alternatives, exhaustive unless an empty `case:` fallback is present, duplicate cases and multiple fallbacks are compile-time errors
- a compile-time `if` otherwise follows all [If / else](#if--else) rules; `else if` chains, and each arm including a plain `else` is pruned independently
- naming the scrutinee changes nothing: `comptime k = T::kind; match k { ... }` is equivalent to matching on `T::kind` directly
- these forms are legal anywhere a statement is legal; there is no requirement to be inside a template or `comptime` function body
- a scrutinee of a compile-time-only type, such as `TypeKind`, can only appear in a compile-time branch, since such values have no runtime representation. A `type` value has no `match` pattern syntax at all and is dispatched with [`==`](#type-equality) or its [`::kind`](#type-members) instead

Skipping the checker on unselected arms means code behind a compile-time-false condition is not validated at all; see [Design decisions](#design-decisions) for the tradeoff.

### While

```cpp
var i = 10;
while i > 0 {
	i -= 1;
}
```

Conditions must be `bool`. Runtime enforces a configurable step budget to limit accidental infinite loops.

### For

```cpp
for i in 0..10 {
	log.logError(i);
}
```

The `a..b` range is exclusive on the upper bound. The range expressions are evaluated once before the loop starts. The loop variable is introduced by the `for` statement and is immutable inside the loop body.

Both bounds must have the same integer type, which becomes the type of the loop variable. An untyped bound adopts the other bound's concrete type (`for i in 0..s.length` iterates with an `isize` loop variable); if both bounds are untyped they default to `i32`.

If the lower bound is greater than or equal to the upper bound, the loop body does not execute.

This is equivalent to evaluating the bounds once, then iterating upward with an internal counter:

```cpp
const from = 0;
const to = 10;
var current = from;
while current < to {
	const i = current;
	log.logError(i);
	++current;
}
```

**For-in over a slice/array**

```cpp
for v in arr {
	log.logError(v);
}

for i, v in arr {
	log.logError(i);
	log.logError(v);
}
```

`for v in arr` is syntax sugar for iterating the index range and binding the element:

```cpp
for i in 0..arr.length {
	const v = arr[i];
	log.logError(v);
}
```

`for i, v in arr` is the same desugaring, additionally exposing the index as `i`:

```cpp
for i in 0..arr.length {
	const v = arr[i];
	log.logError(i);
	log.logError(v);
}
```

- `arr` must be a static-sized array or a slice
- `i` has type `isize`; `v` has the element type of `arr`. This holds for static arrays too: `.length` on a `[N]T` is an untyped compile-time constant, and the desugared range concretizes it to `isize`
- both `i` and `v` are immutable inside the loop body, like the single-variable `for` loop variable

### Custom iterators

A `for` loop can also consume a stateful iterator struct. An iterator is any
struct with a field named `next` whose function type has this shape:

```cpp
next : fn(iter : *IteratorType, out : *ElementType) : bool
```

The iterator type and element type are concrete types. The iterator may contain
any additional fields needed to hold traversal state:

```cpp
struct ListIter {
	next : fn(iter : *ListIter, out : *Node) : bool;

	current : ?*Node;
	reverse : bool;
}

fn list_next(iter : *ListIter, out : *Node) : bool {
	var node = iter.current else return false;
	out.* = node;
	iter.current = iter.reverse ? node.previous : node.next;
	return true;
}
```

A constructor can select any function with the required signature and initialize
any other iterator state:

```cpp
fn values(list : *List) : ListIter {
	return { list_next, list.head, false };
}

for node in values(list) {
	process(node);
}
```

The iterable expression is evaluated once. The loop stores the resulting
iterator in mutable storage and repeatedly invokes `next`, approximately as:

```cpp
var iter = values(list);
var value : *Node = undefined;
while iter.next(&iter, &value) {
	process(value);
}
```

`next` returns `true` when it has written a value to `out`, and `false` when the
iteration is complete. The iterator can therefore contain nullable elements
without using null as the end marker. `break` stops calling `next`; `continue`
starts the next call. The iterator itself must remain valid for the duration of
the loop, and `next` may mutate its state through the mutable iterator pointer.

With an index binding, the index is the zero-based number of values produced by
the iterator, not necessarily an index into the underlying collection:

```cpp
for index, node in values(list) {
	process(index, node);
}
```

This protocol is structural: no interface, trait, or special base type is
required. The compiler only inspects the `next` field and its function type;
all other fields are iterator-specific. Arrays and slices continue to use their
direct indexed `for` lowering, while custom iterators are useful for linked
lists, trees, filtered collections, and engine APIs whose count/get functions
have different names.

A `next` function must assign `out` before returning `true`. The compiler does
not verify that the function actually writes through the output pointer.

### Break / continue / labels

`break` exits a loop immediately. `continue` skips to the next loop iteration.

Basic examples:

```cpp
var i : i32 = 0;
while i < 10 {
	i += 1;
	if i == 3 {
		continue;
	}
	if i == 8 {
		break;
	}
}
```

Named labels allow targeting an outer loop:

```cpp
outer: while true {
	var j : i32 = 0;
	while j < 10 {
		j += 1;
		if j == 5 {
			continue outer;
		}
		if j == 9 {
			break outer;
		}
	}
}
```

Rules:

- unlabeled `break` / `continue` apply to the nearest enclosing loop
- labeled `break label` / `continue label` require a visible loop label
- duplicate labels in the same scope are compile-time errors
- using unknown labels is a compile-time error
- using `break` / `continue` outside a loop is a compile-time error

Status: implemented in parser, checker, runtime, and bytecode backend.

### Defer

`defer` runs when leaving the current scope. Deferred statements execute in LIFO order.

```cpp
fn main() : void {
	defer cleanup();
	// work
}
```

Deferred statements run on normal scope exit and on early `return`.

### Return

```cpp
fn answer() : i32 {
	return 42;
}

fn done() : void {
	return;
}
```

The returned expression must be implicitly convertible to the function's return type. Use an explicit cast when no implicit conversion applies.

## Expressions

### Literals

```cpp
true
false
1
1_000_000
0xABC
0xFF_FF
12.5
12_345.625_0
'0'
'A'
"text"
```

Integer literals are decimal by default. The `0x` or `0X` prefix selects
hexadecimal; hexadecimal digits may use either letter case.

Integer and floating-point literals may use `_` between digits for readability.
Separators do not affect the value. A separator cannot be leading, trailing,
repeated, adjacent to the decimal point, or adjacent to `0x`; forms such as
`_1`, `1_`, `1__0`, `1_.0`, and `0x_FF` are not numeric literals.

Rune literals are enclosed in single quotes and represent one Unicode code point. They are untyped integer constants, like integer literals, and are concretized by their context. For example, `'0'` can be used with a `u8` value and has the value `48` (`U+0030`).

Double-quoted literals produce `[]const u8`; see [String literals](#string-literals).

### Arithmetic

```cpp
a + b
a - b
a * b
a / b
a % b
```

Rules:

- no implicit numeric casts
- arithmetic operands must have the same concrete numeric type
- `%` is integer modulo only

Integer division/modulo behavior:

- division truncates toward zero
- `%` follows `a == (a / b) * b + (a % b)`
- non-zero remainder has same sign as `a`
- zero divisor is runtime error, including constant zero divisors

Floating-point division by a non-zero divisor follows IEEE-754 and may produce `Inf` or `NaN`.
Division by zero is runtime error, including constant zero divisors.

### Integer overflow

Integer arithmetic wraps modulo $2^N$, where $N$ is destination bit width.

```cpp
const a : u8 = 255 as u8;
const b : u8 = (a + 1 as u8) as u8; // b == 0

const c : i8 = 127 as i8;
const d : i8 = (c + 1 as i8) as i8; // d == -128
```

Width-changing integer casts follow the same wrap behavior.

### Casts

Use explicit `as` casts:

```cpp
const whole : i32 = 10;
const decimal = whole as f32;
```

Supported scalar cast targets:

```cpp
x as i8
x as u8
x as i16
x as u16
x as i32
x as u32
x as i64
x as u64
x as f32
x as f64
```

Boolean casts are not supported.

Untyped integer literals inside an explicit cast are materialized to the destination
numeric width when possible, so large values can be cast directly to wider integer
types:

```cpp
const value : i64 = 2147483648 as i64;
```

Enums can cast to integers, and integers can cast to enums. Enum and floating-point casts are invalid:

```cpp
const numeric : i32 = State.Running as i32;
const state : State = numeric as State;
```

Integer-to-enum cast does not validate membership.

Struct casts are not supported.

An addressable runtime variable can be viewed as a one-element slice of the
same element type with `[:]`:

```cpp
fn bump(values : []i32) : void {
	values[0] += 1;
}

	fn main() : i32 {
	var value : i32 = 4;
	var values : []i32 = value[:];
	bump(values);
	return value; // 5
}
```

The view is non-owning, so changing `values[0]` changes `value`. Its length is
one. The source must be writable addressable runtime storage. A `const`
variable, pointer to a read-only value, literal, temporary, or compile-time
value cannot be used as a writable slice source. A writable pointer's pointee
can be used because it refers to writable caller storage. The slice must not
be used after the source storage stops being valid.

Slice reinterpret casts convert between a byte slice and a typed slice:

```cpp
import "std:mem" as mem

fn main() : void {
	var raw : []byte = mem.alloc(4 * sizeof(i32), alignof(i32));
	var ints : []i32 = raw as []i32; // view the same storage as i32
	ints[0] = 42;
	var back : []byte = ints as []byte; // view it as raw bytes again
}
```

- `[]byte as []T` reinterprets a byte slice as a slice of `T` over the same storage; no copy occurs
- `[]T as []byte` reinterprets any typed slice as a byte slice over the same storage; no copy occurs
- the resulting length is recomputed for the destination element size: a `[]byte` of `n` bytes becomes a `[]T` of `n / sizeof(T)` elements, and a `[]T` of `m` elements becomes a `[]byte` of `m * sizeof(T)` bytes
- `[]byte as []T` requires the byte length to be a multiple of `sizeof(T)` and the storage to be suitably aligned for `T`; violations are runtime errors
- only `[]byte` participates as the untyped side; reinterpreting between two unrelated typed slices (`[]f32 as []i32`) is not supported and is a compile-time error

No implicit casts occur in assignments, arguments, returns, struct fields, or binary arithmetic.

### Sizeof and alignof

`sizeof` and `alignof` are compile-time operators that take a type and produce an untyped integer constant:

```cpp
const a = sizeof(i32);   // 4 bytes
const b = alignof(i32);  // 4-byte alignment
const c = sizeof(Vec3);  // number of bytes the struct occupies
```

Rules:

- the operand is a type, not a value
- the operand must be a concrete type. Untyped integer and float values have no size or alignment, and `sizeof`/`alignof` do not default them; use a concrete type such as `sizeof(i32)`, or cast or annotate the value before obtaining its type
- both produce an untyped integer constant, usable wherever a compile-time integer is required (array sizes, type-factory value arguments, `comptime` parameters, other comptime expressions)
- `sizeof(T)` is the size of `T` measured in `byte` units: `byte`, `bool`, `i8`, and `u8` are 1 byte; `i16`/`u16` are 2; `i32`/`u32`/`f32`/enums/function values are 4; `i64`/`u64`/`isize`/`f64`/pointers are 8; a slice is a pointer followed by an `i64` element length (16 bytes on supported targets); an array is `size * sizeof(element)`; a struct is the sum of its field sizes; and a tagged union is `sizeof(i32)` for the tag plus the size of its largest member
- `alignof(T)` is derived from the byte size and capped at pointer alignment
- they are most commonly used with the raw-memory allocator and slice reinterpret casts, for example `alloc(n * sizeof(i32), alignof(i32))`

### Comparison and boolean operators

```cpp
a > b
a < b
a >= b
a <= b
a == b
a != b

ready and visible
ready or fallback
not ready
```

`and` and `or` short-circuit.

`not` is the unary boolean negation operator. Its operand must be `bool`; applying it to any other type, including numeric types and nullable values, is a compile-time error. There is no implicit conversion to `bool`, so test explicitly (`not n == 0`, `p == null`) rather than negating a non-boolean directly.

`not` is a prefix operator that binds looser than the comparisons, `is`, and arithmetic, but tighter than `and` and `or`. It therefore negates a whole comparison or `is` test without parentheses, while still distributing over the operands of a conjunction:

| expression | parses as |
|---|---|
| `not a and b` | `(not a) and b` |
| `a and not b` | `a and (not b)` |
| `not a and not b` | `(not a) and (not b)` |
| `not a == b` | `not (a == b)` |
| `not e is ButtonEvent` | `not (e is ButtonEvent)` |
| `not a is T and not b is U` | `(not (a is T)) and (not (b is U))` |
| `not a + b` | `not (a + b)` - compile-time error, `a + b` is not `bool` |

Binding tighter than `and` and `or` is what keeps De Morgan rewriting direct: `not a and not b` negates each operand rather than the conjunction. Binding looser than the comparisons is what makes `not a == b` read as written; this is the placement `and`, `or`, and `not` have in Python and Lua.

`not` is right-associative with itself, so `not not ready` is `not (not ready)`. Since `not` accepts only `bool`, negating a non-boolean is a compile-time error rather than silently different behaviour - unlike C, where `!a == b` compiles as `(!a) == b`.

Ordering comparisons (`<`, `<=`, `>`, `>=`) require numeric operands of the same type. Equality (`==`, `!=`) is defined for:

- numeric types, `bool`, `byte`, and enums - value comparison
- `cstr` and `cptr` - address comparison (two `cstr` values with equal content but different storage are not equal)
- typed pointers - address comparison
- function values - same function
- nullable values - only against the `null` literal
- `type` values - type identity, compile-time only (see [Type equality](#type-equality))
- slices whose element type has built-in equality - content comparison (see [Slice equality](#slice-equality))

Arrays, unions, and two nullable values have no built-in equality; comparing them is a compile-time error, and so is comparing a slice whose element type has no built-in equality. Structs resolve `==` through `operator` declarations (see below).

If an operator is used with non-builtin value types, the compiler may resolve it to a matching `operator` declaration instead of a built-in primitive rule.
Primitive operands keep their built-in semantics and cannot be overridden by `operator` declarations.
`and` and `or` keep their built-in short-circuit semantics, and `not` keeps its built-in `bool`-only semantics; none of the three are candidates for operator declarations.
Compound assignment follows the same rule: a non-primitive left-hand target uses the corresponding binary operator, while a primitive left-hand target stays on the built-in path. In the latter case, the right-hand operand must be implicitly convertible to the left-hand target type; an expression such as `5 *= Vec2 { 1, 2 }` is therefore invalid.

#### Slice equality

`==` and `!=` on slices compare contents, not identity. This is what makes
string comparison direct, since a [string literal](#string-literals) is an
ordinary `[]const u8`:

```cpp
const a : []const u8 = "quit";
const b : []const u8 = "quit";
const same = a == b;          // true: equal bytes, unrelated storage

var xs : [3]i32 = [1, 2, 3];
var ys : [3]i32 = [1, 2, 3];
const equal = xs[:] == ys[:]; // true
```

Rules:

- two slices are equal when their lengths are equal and every element pair is
  equal; unequal lengths are unequal without inspecting any element
- comparison is defined only when the element type has built-in equality:
  numeric types, `bool`, `byte`, and enums. Slices of structs, arrays, slices,
  unions, nullables, function values, `cstr`, or `cptr` are a compile-time error
  to compare
- `[]T` and `[]const T` over the same element type compare with each other; they
  have the same representation, and `const` restricts writing rather than the
  values being read
- comparing slices with different element types is a compile-time error, even
  when the elements have the same size
- backing storage does not participate: two views of the same storage with the
  same length are equal, and so are two views of unrelated storage holding equal
  elements
- empty slices are equal regardless of where they point, so a slice initialized
  with `null` equals any other empty slice of a comparable element type
- an `operator ==` or `operator !=` declaration taking slice parameters is a
  compile-time error; the built-in rule applies and cannot be overridden
- the comparison inspects up to `length` elements, so it is O(n). It is the only
  built-in `==` that is not constant time

Static arrays have no built-in equality; slice them first, as `xs[:] == ys[:]`
above.

Slice equality is available during compile-time evaluation, so the compile-time
byte slices produced by introspection - [`t::name`](#tname), `f.name`, and
`e.name` - can be compared against string literals:

```cpp
unroll for f in S::fields {
	if f.name == "hp" {
		io.write_bytes(f.name);
	}
}
```

### Operator precedence

From loosest to tightest. Operators on the same row have equal precedence and associate left to right unless noted.

| | operators | associativity |
|---|---|---|
| 1 | `? :` | right |
| 2 | `\|` (union types), `or` | left |
| 3 | `and` | left |
| 4 | `not` (prefix) | right |
| 5 | `==`, `!=` | left |
| 6 | `<`, `>`, `<=`, `>=`, `is` | left |
| 7 | `+`, `-` (binary) | left |
| 8 | `*`, `/`, `%` | left |
| 9 | `as` | left |
| 10 | `-`, `*`, `&` (prefix) | right |
| 11 | `.`, `()`, `[]` | left |

Notes:

- `not` is the only prefix operator below the arithmetic levels; it negates a whole comparison or `is` test without parentheses, while `and` and `or` still separate its operands (see [Comparison and boolean operators](#comparison-and-boolean-operators))
- `as` binds tighter than arithmetic but looser than prefix `-`, so `-x as f32` is `(-x) as f32` and `a + b as f32` is `a + (b as f32)`
- `|` shares a level with `or`, but the two never compete: `|` combines types and `or` combines `bool` values (see [Tagged unions](#tagged-unions))
- assignment (`=`) and compound assignment (`+=`, `*=`, and the rest) are statements, not expressions, so they take no precedence level and cannot appear inside an expression
- `sizeof`, `alignof`, and `typeof` are call-like compile-time operators whose operand is parenthesised, so they need no precedence level either

### Ternary operator

```cpp
condition ? true_expr : false_expr
```

The ternary operator selects between two expressions based on a boolean condition:

```cpp
fn clamp(value : i32, min_val : i32, max_val : i32) : i32 {
	return value < min_val ? min_val : (value > max_val ? max_val : value);
}

fn main() : void {
	var x : i32 = 5;
	var result : i32 = x > 3 ? 10 : 20;  // result = 10
}
```

Rules:

- condition must be `bool`; no implicit conversion
- both true and false branches must have the same type
- the operator is right-associative, enabling nested ternary: `a ? b ? c : d : e` parses as `a ? (b ? c : d) : e`
- the operator has the lowest precedence of any operator, so `a > b ? 1 : 2` evaluates the comparison first (see [Operator precedence](#operator-precedence))
- short-circuit evaluation: only the selected branch is evaluated, not both

### Calls

```cpp
const c = add(a, b);
```

Calls are statically checked for:

- function existence
- argument count
- argument types

If callee expression is a function value, call is indirect.

#### Panic

`panic(msg)` is a built-in, non-returning call. `msg` must be a string or a
`[]const u8` slice. It reports the message with the source location and stops
execution; no caller frames are unwound. The compiler lowers it to the
`PANIC` bytecode opcode rather than a native function call.

### Argument-dependent lookup

When a bare function name cannot be resolved through the current module or unaliased imports, the compiler also searches the declaring namespace of the first argument's type.

```cpp
import "engine:entity" as entity

fn example(e : entity.Entity) : void {
	destroy(e);        // resolves to entity.destroy
	entity.destroy(e); // equivalent explicit form
}
```

Rules:

- only applies when the name has no match in the current module or unaliased imports
- only the declaring namespace of the first argument's type is searched
- if the name matches both a local/unaliased-import declaration and the first argument's namespace, the local declaration is preferred
- alias-qualified calls (`entity.destroy(e)`) are always unambiguous and bypass ADL

### UFCS

Method-style syntax `x.foo(a, b)` is syntactic sugar for a free function call with the receiver inserted as the first argument. The function is looked up in the declaring namespace of the receiver's type.

```cpp
import "engine:world" as world
import "engine:entity" as entity

fn move_up(w : world.World, e : entity.Entity) : void {
	if w.hasEntity(e) {
		e.destroy();
	}
}
```

Equivalent explicit form:

```cpp
fn move_up(w : world.World, e : entity.Entity) : void {
	if world.hasEntity(w, e) {
		entity.destroy(e);
	}
}
```

Rules:

- if `x.foo(a, b)` is not resolved by other language features (enum, struct field, namespace), it's retried as `foo(x, a, b)`
- the receiver is inserted unchanged; a pointer receiver requires a pointer
  expression, such as `p.foo(a, b)` for a function whose first parameter is
  `*T`
- method syntax dispatches on the receiver: the declaring namespace of the receiver's type is searched first, and only if it has no match does lookup fall back to the current module and unaliased imports; a local function never shadows a same-named function from the receiver type's unit
- does not apply to primitive receiver types, e.g. `4.foo(a, b)` is invalid
- alias-qualified calls (`entity.destroy(e)`) are always unambiguous

Example:

```cpp
import "engine:entity" as entity

fn destroy(e : entity.Entity) : void {}

fn example(e : entity.Entity) : void {
	e.destroy();       // calls entity.destroy - method syntax prefers the receiver type's unit
	destroy(e);        // calls local destroy - plain calls stay lexical, local is preferred over ADL
	entity.destroy(e); // calls entity.destroy - explicit namespace, always unambiguous
}
```

Pointer-based mutable container operations use the same method-style syntax:

```cpp
import "core:array" as array

fn example() : void {
	var values : array.Array = undefined;
	values.init();
	values.push(42);
}
```

## Memory

Raw memory is allocated and released through the builtin `std:mem` module. Like other `std:` modules it is imported by path:

```cpp
import "std:mem" as mem

fn main() : void {
	var raw : []byte = mem.alloc(4 * sizeof(i32), alignof(i32));
	var ints : []i32 = raw as []i32;
	ints[0] = 42;
	mem.free(raw);
}
```

The module exposes two functions:

```cpp
fn alloc(size : isize, align : isize) : []byte
fn free(memory : []byte) : void
```

- `alloc(size, align)` returns a `[]byte` of `size` `byte` units (see [`sizeof`](#sizeof-and-alignof)). The contents are zero-initialized. `size` and `align` are `isize`; a non-positive `size` yields an empty slice.
- `align` is accepted for source compatibility but currently has no effect on allocation placement.
- `free(memory)` releases an allocation. Pass the exact slice `alloc` returned (same base and length).
- accessing a slice over freed memory traps at runtime (use-after-free detection): `free` marks the allocation's units dead, and any later `[]byte`/reinterpreted-slice read or write over them aborts execution.
- the returned `[]byte` can be reinterpreted as a typed slice with `as` (see [Casts](#casts)); the length rescales by the element's `sizeof`.

## Compile-time introspection

Types are compile-time values (see [Comptime](#comptime)). Introspection lets compile-time code ask what a type *is* and enumerate its structure, so a single function template can handle every type in the language.

The motivating example is a generic `print`:

```cpp
import "core:io" as io // extern write_bytes, write_i64, write_u64, write_f64, write_bool

fn print(v : $T) : void {
	if T == []const u8 {
		io.write_bytes(v);
		return;
	}

	match T::kind {
		case .Bool:                          io.write_bool(v);
		case .F32, .F64:                     io.write_f64(v as f64);
		case .I8, .I16, .I32, .I64, .ISize:  io.write_i64(v as i64);
		case .U8, .U16, .U32, .U64:          io.write_u64(v as u64); // no .Byte, no .CStr: see below
		case .Nullable:
			if v != null {
				print(v); // v is promoted; the recursive call instantiates at the inner type
			} else {
				io.write_bytes("null");
			}

		case .Slice, .Array:
			io.write_bytes("[");
			for i in 0..v.length {
				if i > 0 { io.write_bytes(", "); }
				print(v[i]);
			}
			io.write_bytes("]");

		case .Enum:
			unroll for e in T::values {
				if v == e.value {
					io.write_bytes(e.name);
					return;
				}
			}
			io.write_bytes("<invalid ");
			io.write_i64(v as i64);
			io.write_bytes(">");

		case .Struct:
			io.write_bytes(T::name);
			io.write_bytes(" { ");
			unroll for i, f in T::fields {
				if i > 0 { io.write_bytes(", "); }
				io.write_bytes(f.name);
				io.write_bytes(" = ");
				print(v[f.name]);
			}
			io.write_bytes(" }");

		case .Union:
			unroll for M in T::types {
				if v is M {
					print(v); // v is promoted to M; recurses at T = M
					return;
				}
			}

		case:
			io.write_bytes("<");
			io.write_bytes(T::name);
			io.write_bytes(">");
	}
}
```

Note what this example does *not* need. Because `T` is fully concrete at instantiation, `v` already has an exact static type in every arm - there is no type narrowing tied to the `case` patterns. In `case .Nullable:` the parameter simply *is* `?U`, so `if v != null` is ordinary [nullable promotion](#nullable-values); the only new rule is that the promoted read feeds `$T` deduction at the recursive call. Likewise `case .Union:` reuses the existing `is` promotion from [Tagged unions](#tagged-unions).

`.Void`, `.Type`, `.CPtr`, and `.Fn` fall through to `case:` because no value of those kinds is meaningfully printable here - there is no value of type `void`, `type` is compile-time only, and `cptr`/`fn` have no byte representation `print` can walk.

`.Byte` and `.CStr` also fall through, but for a narrower reason: both *are* ordinary runtime values blocked only by a missing conversion. [Casts](#casts) lists no conversion between `byte` and an integer type, nor between `cstr` and `[]const u8`, so neither arm can pass a printable value to the corresponding output function.

### typeof

`typeof` is a compile-time operator that takes an expression with an already concrete type and produces that type as a compile-time `type` value:

```cpp
comptime A = typeof((1 + 2) as i32); // i32
comptime B = typeof(v[0]);       // the element type of v
comptime C = typeof(make_vec3()); // the return type of the call, Vec3
```

Rules:

- the operand is an expression, not a type - the mirror of [`sizeof` and `alignof`](#sizeof-and-alignof), which take a type. `sizeof(typeof(e))` is the size of `e`'s type. Reflection that starts from a *type* rather than an expression uses [type members](#type-members) instead
- `typeof` does not default untyped numeric expressions. `typeof(1)`, `typeof(1.0)`, and `typeof(value)` when `value` is an untyped numeric `comptime` binding are compile-time errors. Cast the expression (`typeof(1 as i32)`) or annotate the binding first. Consequently, `sizeof(typeof(1))` and `alignof(typeof(1.0))` are also errors
- like `sizeof` and `alignof`, `typeof` is an operator resolved by the compiler, not a function: it cannot be bound to a name, passed as an argument, used as a function value, or reached through [UFCS](#ufcs); only its result is a value
- the operand is type-checked but **not evaluated**, and no code is generated for it. `typeof(v[0])` on an empty slice is valid and yields the element type
- it produces a `type` value, usable wherever a compile-time type is required (type-factory arguments, variable type positions, `comptime` bindings, `==` comparison)
- `typeof` observes flow typing: after `if v != null`, `typeof(v)` inside the branch is the promoted type `U`, not `?U`. The same applies inside an `is` or `match` arm on a tagged union
- the result is compile-time only and never materializes into runtime code (see [Comptime-to-runtime materialization](#comptime-to-runtime-materialization))

### Type members

A `type` value exposes its structure through members accessed with `::`. A value can use the same syntax: `value::name` is exactly equivalent to `typeof(value)::name`. The value expression is type-checked but not evaluated, so this also works for runtime values. The receiver may therefore be a type expression - typically a `$T` parameter, a `type` parameter, or any `typeof` result - or an ordinary value expression:

| member | result | valid for |
| --- | --- | --- |
| `t::kind` | `TypeKind` | every type |
| `t::name` | compile-time `[]const u8` | every type |
| `t::min` | untyped numeric value | numeric types |
| `t::max` | untyped numeric value | numeric types |
| `t::child` | `type` | `.Nullable`, `.Pointer`, `.Slice`, `.Array` |
| `t::length` | comptime integer | `.Array` |
| `t::fields` | slice of field descriptors | `.Struct` |
| `t::values` | slice of enum descriptors | `.Enum` |
| `t::types` | sequence of member types | `.Union` |
| `t::params` | slice of parameter descriptors | `.Fn` |
| `t::ret` | `type` | `.Fn` |
| `t::attribute(attr_type)` | `?attr_type` | every type |

```cpp
comptime k = i32::kind;       // .I32
comptime lo = i32::min;       // -2147483648
comptime hi = i32::max;       // 2147483647
comptime s = Vec3::kind;      // .Struct
comptime n = Vec3::name;      // "Vec3"
comptime e = []i32::child;  // i32
comptime m = [4]i32::length; // 4
comptime ps = (fn(i32, f32) : bool)::params; // [{ name = "", type = i32 }, { name = "", type = f32 }]
comptime r = (fn(i32, f32) : bool)::ret;     // bool
comptime tag = Settings::attribute(tag);      // ?tag

var v : Vec3 = undefined;
comptime value_name = v::name;                       // same as typeof(v)::name
comptime value_fields = v::fields;                   // same as typeof(v)::fields
```

- `t::kind` classifies the type into one [`TypeKind`](#typekind) discriminant
- `t::name` is the type's source-level name, represented by the bytes shown in the table under [`t::name`](#tname) below
- `t::min` and `t::max` are untyped numeric values containing the lowest and highest finite values representable by a concrete numeric type. They adopt the expected type when consumed. For floating-point types, `t::min` is the most negative finite value, not the smallest positive normal value.
- `t::child` is the single operand of a one-operand type constructor: the `U` of `?U`, `*U`, `[]U`, or `[N]U`; compound type expressions can be written directly before the member, as in `?i32::child`, `*i32::child`, or `[]i32::child`
- `t::length` is the element count `N` of a `[N]T`, an untyped compile-time integer - the same value `v.length` yields on an instance (see [Static-sized arrays](#static-sized-arrays)), but reachable from the type without one, so `unroll for i in 0..t::length` works on a type alone. It is not defined for `.Slice`, whose length is a runtime property
- `t::fields`, `t::values`, and `t::types` are [reflection sequences](#reflection-sequences)
- `t::params` is the [reflection sequence](#reflection-sequences) of a `.Fn` type's parameter descriptors, in declaration order; `t::ret` is its return type, named `ret` rather than `return` to avoid the keyword. Each descriptor is an ordinary compile-time struct with `.name` and `.type` members; `.name` is a compile-time `[]const u8` and is `""` for unnamed parameters.
- `t::attribute(attr_type)` looks up the first attribute on `t` whose declaration type is `attr_type`, returning its value as `?attr_type`. It returns `null` when no matching attribute is attached. The argument must be a compile-time type value and the result is compile-time only.

All type members are compile-time only. A concrete type receiver is checked immediately. A generic receiver such as an uninstantiated `$T` defers the access as a compile-time constraint until the template is instantiated. A value receiver uses its statically known type, including any flow typing in effect at the access site. Type members are not operators or functions - like any member access they cannot be taken as a value on their own, only applied to a type or value receiver.

**Kind-specific members are constrained.** `t::child`, `t::length`, `t::fields`, `t::values`, `t::types`, `t::params`, and `t::ret` exist only for some kinds. A manifest type - a type literal such as `[]i32` or a concrete `Vec3` - is checked immediately. For a generic receiver, the access is valid in the template body and requires an argument of a compatible kind; an incompatible instantiation is a compile-time error. A kind-proving branch remains useful when one template intentionally supports several kinds:

```cpp
match t::kind {
	case .Struct:   unroll for f in t::fields { ... } // ok: kind proven
	case .Nullable: foo(t::child);                    // ok: kind proven
	case: ...
}
```

This is the same [compile-time branch](#compile-time-branches) pruning used everywhere else: the arm that reads `t::fields` is checked only when it is selected, and it is selected only for a struct. Without such a branch, a generic access still forms a valid constraint and is checked when the receiver becomes concrete.

#### `t::name`

`t::name` returns the source-level name of `t`:

| type | `t::name` |
| --- | --- |
| builtins | `"i32"`, `"f64"`, `"u8"` |
| structs and enums | the declaration name, `"Vec3"`, `"State"` |
| factory-produced structs | `"Pair(i32)"`, `"Optional(Vec3)"` |
| nullable | `"?Vec3"` |
| pointers | `"*Vec3"`, `"*const Vec3"`, `"?*Vec3"`, `"?*const Vec3"` |
| slices and arrays | `"[]i32"`, `"[]const u8"`, `"[4]i32"` |
| unions | `"A \| B \| C"` in canonical member order |
| function types | `"fn(i32, i32) : i32"` or `"fn(a : i32, b : i32) : void"` |

`t::kind` and `t::name` are the only members available for every type; `t::min` and `t::max` are restricted to numeric types. The same validity rules apply when the receiver is a value: `v::kind` and `v::name` inspect `typeof(v)`, while `v::fields` requires `typeof(v)` to be a struct. A type's constituent parts are reached through the [reflection sequences](#reflection-sequences), which exist only for structs, enums, and unions.

### TypeKind

`TypeKind` is a built-in enum type, not a declaration in a module. Like `i32` it is always in scope, needs no import, and cannot be shadowed. Its definition is fixed by the language and shown here only for reference:

```cpp
enum TypeKind {
	Bool,
	I8, I16, I32, I64, ISize,
	U8, U16, U32, U64, Byte,
	F32, F64,
	CStr,
	CPtr,
	Void,
	Type,      // the `type` type itself
	Nullable,  // ?T
	Pointer,   // *T
	Slice,     // []T
	Array,     // [N]T
	Enum,
	Struct,
	Union,     // A | B
	Fn         // fn(...) : R
}
```

- the enum is exhaustive: every type has exactly one discriminant.
- a factory-produced struct such as `Pair(i32)` is a `.Struct`
- `TypeKind` values are compile-time only, so a `TypeKind` scrutinee always produces a [compile-time branch](#compile-time-branches)
- member shorthand works as it does for any enum, so `case .Struct:` needs no qualification and the name `TypeKind` rarely has to be written out
- adding a discriminant in a future version is a breaking change; it is reported at every exhaustive `match` that does not have an empty `case:` fallback

`.Void` is reachable through `void::kind` and through `typeof` applied to a void-typed call, but never as the type of a value: a template instantiated with `T = void` is a compile-time error at the call site, because no value of type `void` exists to bind to the parameter.

### Type equality

`==` and `!=` are defined on `type` values and compare type identity. Both operands must be compile-time types, and the result is a compile-time `bool`. This is the way to special-case one concrete type rather than a whole kind:

```cpp
if T == Vec3 {
	print_vec3(v);
} else {
	print_generic(v);
}
```

Type comparison follows the identity rules of the types themselves: structural types compare structurally, so `A | B` equals `B | A` (see [Tagged unions](#tagged-unions)), while two distinct `struct` declarations with identical fields are different types.

A type comparison is always compile-time known, so an `if` on one is always a [compile-time branch](#compile-time-branches) and the branch not taken is not type-checked. That is what lets the taken branch use members that exist only on the compared type.

### Reflection sequences

`t::fields`, `t::values`, `t::types`, and `t::params` produce a struct's fields, an enum's members, a union's member types, or a `.Fn` type's parameter descriptors as ordinary compile-time slices. Their elements may contain `type` values and therefore cannot be materialized at runtime:

| member | element |
| --- | --- |
| `t::fields` | a field descriptor struct: `.name`, `.type` |
| `t::values` | an enum descriptor struct: `.name`, `.value` |
| `t::types` | a `type` |
| `t::params` | a parameter descriptor struct: `.name`, `.type` |

As comptime slices they are first-class: they can be bound, measured with `.length`, indexed, and iterated with `unroll for`, all at compile time. Their element types contain compile-time-only information, so they cannot be materialized or traversed by a runtime `for`. Compiler-provided descriptor structs nevertheless use the same field alignment, padding, and size rules as runtime structs for the representations of their fields.

```cpp
comptime fs    = t::fields;    // element type inferred; see below
comptime n     = fs.length;    // the field count
comptime first = fs[0];       // a single field descriptor
unroll for f in fs { ... }    // re-iterate a bound sequence
```

- **binding requires inference for descriptor slices.** Their compiler-provided struct types are not nameable in source, so `comptime fs = t::fields` and `comptime ps = t::params` are legal, but annotations naming those element types are not. `t::types` is the exception: its element type `type` is nameable, so `comptime ts : []type = t::types` may carry the annotation
- `.length`, indexing, and `unroll for` work as on any comptime slice
- once bound, the value is an ordinary comptime slice with no residual tie to `t`: it can be carried out of the branch that produced it and used anywhere. For a generic receiver, the required kind constraint is checked when the template is instantiated.

Type-side field descriptors carry `.name` and `.type`. They have no `.value`,
because a field's value has a different type for every field while a reflection
sequence has one element type. Use computed field access, such as
`value[f.name]`, when the field name is needed to access a struct value.

### Unroll for

`unroll for` duplicates its body at compile time, once per iteration, binding the loop variable to a different compile-time *value* in each copy. Each copy is then type-checked separately.

```cpp
unroll for i in 0..N { ... }             // range form, N comptime-known
unroll for x in seq { ... }              // sequence form
unroll for i, x in seq { ... }           // sequence form with index
```

- the range form requires both bounds to be compile-time integer constants; `arr.length` on a `[N]T` is one, so `unroll for i in 0..arr.length` unrolls a static array while the same loop over a slice must use a runtime `for`
- the sequence form requires a comptime slice, which includes the [reflection sequences](#reflection-sequences) `t::fields`, `t::values`, `t::types` and any binding of one. A struct, enum, or union value is not iterable; iterate the corresponding type reflection sequence instead
- the loop variable is a compile-time binding and cannot be reassigned in the body
- because it is compile-time, expressions derived from it are resolved per copy: the index in `unroll for i, x in seq` is a constant, so `if i > 0 { ... }` is decided at compile time
- a comptime slice may also be iterated with a runtime `for` when its element type has a runtime representation, in which case the loop variable is an ordinary runtime value and none of the above applies

Control flow inside an unrolled body:

- `return` returns from the enclosing function
- `break` transfers past the last copy; `continue` transfers to the next copy
- labeled `break` / `continue` work normally, including labels on an enclosing `unroll for`

`unroll for` is a compile-time *duplication* construct, not a compile-time *evaluation* construct: the copies are ordinary code. In particular `break` and `continue` emit real runtime branches out of the unrolled sequence, so an unrolled loop is not guaranteed to be free of control flow.

### Field iteration

A struct's fields are iterated in declaration order through the reflection
sequence of its type:

- **type form** - `unroll for f in S::fields`. Descriptors carry `.name` and
  `.type`

```cpp
struct S {
	i : i32;
	f : f32;
}

var v : S = undefined;

unroll for f in S::fields {
	if f.type == i32 {         // compile-time branch: the f32 copy is pruned
		io.write_bytes(f.name);
		v[f.name] = 42;
	}
}
```

The loop variable is a compile-time field descriptor with two members:

- `f.name` - the field's declared name, a compile-time `[]const u8`
- `f.type` - the field's type as a compile-time `type` value

Rules:

- `S::fields` yields descriptors with `.name` and `.type`; naming `.value` on one is a compile-time error
- the form takes the optional index binding, with the same meaning it has over any other sequence:

	```cpp
	unroll for f in S::fields { ... }
	unroll for i, f in S::fields { ... } // index available for separators
	```

- reflection descriptor types are compiler-provided ordinary struct types and are not nameable in source
- an ordinary runtime `for` over a struct value is a compile-time error: each field has a different type, so there is no single type for a runtime loop variable to have
- the number of fields is `S::fields.length`; the `i > 0` idiom covers separators without needing it

The assignment in the example type-checks only because [type equality](#type-equality) is compile-time known, so the copy generated for the `f32` field never checks its body. The same loop without the `if` would be an error on the first field whose type rejects `42`.


### Enum iteration

`State::values` is the [reflection sequence](#reflection-sequences) of an enum **type**; iterating it visits the enum's declared members in declaration order.

```cpp
enum State { Idle, Running, Done }

unroll for e in State::values {
	if v == e.value {
		io.write_bytes(e.name);
		return;
	}
}
```

The loop variable is an enum descriptor struct with two members:

- `e.name` - the member's declared name, a compile-time `[]const u8`
- `e.value` - the member itself, typed as the enum. Its discriminant is `e.value as i32`, following the ordinary [enum-to-integer cast](#casts) rules

Rules:

- the operand is an enum type's `::values`; an enum *value* is not iterable, since it denotes one member rather than the set of them
- a name table can be built with indexed access such as `State::values[i].name`
- the optional index binding works as it does everywhere else: `unroll for i, e in State::values { ... }`
- the descriptor type is compiler-provided and not nameable in source

`e.name` and `e.value as i32` are still ordinary compile-time constants in each copy, so they [materialize](#comptime-to-runtime-materialization) into runtime code exactly like any other compile-time byte slice or integer.

### Union iteration

`T::types` is the [reflection sequence](#reflection-sequences) of a tagged union **type**; iterating it visits the union's member types in the union's canonical order (deterministic but implementation-defined, see [Tagged unions](#tagged-unions)).

```cpp
unroll for M in T::types {
	if v is M { print(v); return; }
}
```

The loop variable is a compile-time `type` value, not a descriptor. A struct field, enum member, or function parameter bundles a name with something else, so each uses a struct descriptor; a union member *is* a type, and its name is `M::name`, so there is nothing to bundle. `T::types` is therefore a plain `[]type`.

Rules:

- the operand is a union type's `::types`; a union *value* is not iterable, since it holds one member at a time - use [`match`](#tagged-unions) or `is` on the value
- `M` is usable anywhere a compile-time type is: `v is M`, `v as M`, a variable's declared type, a type-factory argument
- `T::types` can be bound (`comptime ts : []type = T::types`) and indexed
- the `is` test and the promotion it performs are the ordinary union rules; unrolling emits one test per member
- the optional index binding works as it does everywhere else

### Function type introspection

`t::params` and `t::ret` decompose a `.Fn` type into its parameter types and return type, completing the type-member table for the one kind that previously exposed only `t::kind` and `t::name`.

```cpp
comptime Handler = fn(i32, f32) : bool;

comptime ps = Handler::params; // parameter descriptor structs for i32 and f32
comptime r  = Handler::ret;    // bool

unroll for p in ps {
	io.write_bytes(p.name);
	io.write_bytes(": ");
	io.write_bytes(p.type::name);
	io.write_bytes(" ");
}
```

- `t::params` is the [reflection sequence](#reflection-sequences) of parameter descriptor structs, in declaration order; each has `.name` and `.type`. Named parameters carry their declared name in `.name`; unnamed parameters have `""`. `t::ret` is a single `type`, following the same shape as [`t::child`](#type-members) for other one-operand-or-fewer constructors
- `t::ret` may be `void`; unlike a value's static type, which can never be `void` (see [`TypeKind`](#typekind)), a function type is free to name `void` as its return type, and `t::ret` observes it directly without going through `typeof` on a call
- both are guarded like any [kind-specific member](#type-members): valid only once `t`'s kind is proven `.Fn`
- neither reaches into a parameter's or return type's own structure automatically; a `.Struct` parameter is introspected through its descriptor's type, the same as any other `type` value: `t::params[0].type::fields`

Rules:

- `t::params` can be bound with inference (`comptime ps = Handler::params`) and its compiler-provided struct element type is not nameable, so an explicit slice annotation is not available
- `t::ret` is compile-time only, like `t::child` and every other member that produces a `type`

### What an unrolled loop binds

The iteration forms differ in what the loop variable is, decided by the operand:

| operand | binding | members |
| --- | --- | --- |
| comptime slice | its element | whatever the element type has |
| `t::fields` (struct type) | field descriptor | `.name`, `.type` |
| `t::values` (enum type) | enum descriptor | `.name`, `.value` |
| `t::types` (union type) | a `type` | - |
| `t::params` (function type) | parameter descriptor | `.name`, `.type` |

The reflection sequences are themselves comptime slices, so they are subcases of the first row; they are listed separately because their element types are built in and not otherwise nameable. Struct, enum, and union *values* are not iterable - iterate the corresponding type reflection sequence instead. A function value's parameter and return types can be reached as either `typeof(f)::params` or `f::params`.

### Comptime-to-runtime materialization

Introspection results are compile-time values. Some of them can cross into runtime code as constants.

Materializes into a runtime constant:

- compile-time `[]const u8` values, such as `t::name`, `f.name`, and `e.name`, become read-only byte-slice constants
- comptime integers, such as `sizeof`, `alignof`, `t::length`, and `e.value as i32`, become integer constants
- an enum descriptor's `e.value` is an ordinary enum constant
- an untyped numeric `comptime` binding becomes a runtime constant only after a consuming context concretizes it

These may be passed to functions including `extern fn`, assigned to `var` and `const`, and used in any runtime expression.

Stays compile-time only:

- `type` values, including `typeof(...)`, `t::child`, `t::ret`, `f.type`, `p.type`, and a union iteration's binding (`M`)
- `TypeKind` values, including `t::kind`
- field, enum, and parameter descriptors that contain compile-time-only information
- any slice whose element type is compile-time only, which is every [reflection sequence](#reflection-sequences): `t::fields`, `t::values`, `t::types`, and `t::params`; only `t::types` is a plain `[]type`, while the others contain compiler-provided struct types

Using a compile-time-only value in runtime position is a compile-time error.

### Instantiation limits

Introspection makes it easy to write a template that instantiates itself at a larger type each time:

```cpp
fn f(v : $T) : void {
	f(wrap_in_slice(v)); // T, []T, [][]T, ...
}
```

Template instantiation therefore has an implementation-defined depth limit, using the same mechanism as the [comptime step limit](#comptime). Exceeding it is a compile-time error reporting the instantiation stack:

```txt
demo.evox: line 12, column 2: instantiation depth limit exceeded
  in f([][]i32)
  in f([]i32)
  in f(i32)
```

## Runtime model

Current runtime executes compiled bytecode through the public `ex_runtime` API.

- calls create call frames
- blocks create nested local scopes
- struct values store fields in declaration order
- function values reference existing script or native functions
- bytecode functions consume arguments from the runtime stack

Example C++ shape:

```cpp
ex_arena compile_arena;
ex_default_arena_create(&compile_arena);
ex_host compile_host = {compile_arena};
ex_module* module = ex_module_create(&compile_host);
ex_module_compile(module, source, source_name, nullptr, nullptr);

ex_bytecode* bytecode = ex_bytecode_compile(module, &compile_host);
ex_arena runtime_arena;
ex_default_arena_create(&runtime_arena);
ex_host runtime_host = {runtime_arena};
ex_runtime* runtime = bytecode ? ex_runtime_create(bytecode, &runtime_host) : nullptr;
if (runtime) {
	ex_string_view main_name = { "main", 4 };
	ex_call(runtime, main_name);
	if (ex_bytecode_runtime_result_kind(runtime, main_name) != EX_TYPE_VOID) {
		i32 result = ex_to_i32(runtime, -1);
	}
}
```

### Native functions

Declare native functions with `extern fn` in the script. After compiling the
module, find the declaration in its unit and bind its unit-local index to the
runtime callback:

```cpp
static void native_add(ex_runtime* runtime, ex_call_frame frame) {
	EX_ARG(frame, i32, a);
	EX_ARG(frame, i32, b);
	EX_RESULT(frame, a + b);
}

ex_module* module = ex_module_create(&host);
if (ex_module_compile(module, source, source_name, nullptr, nullptr) == EX_RESULT_OK) {
	ex_bytecode* bytecode = ex_bytecode_compile(module, &host);
	ex_runtime* runtime = bytecode ? ex_runtime_create(bytecode, &host) : nullptr;
	ex_unit* unit = ex_module_get_unit(module, 0);
	if (runtime && unit && ex_unit_get_native_function_count(unit) == 1) {
		ex_runtime_set_native_function_callback(runtime, unit, 0, &native_add);
	}
}
ex_runtime_destroy(runtime);
ex_bytecode_destroy(bytecode);
ex_module_destroy(module);
ex_default_arena_destroy(&runtime_arena);
ex_default_arena_destroy(&compile_arena);
```

The caller owns each arena and must keep it alive until all modules, bytecode,
and runtimes using that host have been destroyed.

Script usage:

```cpp
extern fn native_add(a : i32, b : i32) : i32;

fn main() : i32 {
	return native_add(20, 22);
}
```

`extern` declarations inform the compiler about a function's name and signature but do not provide an implementation. Each declaration is enumerated by `ex_unit_get_native_function_count` and `ex_unit_get_native_function_name`; bind it with `ex_runtime_set_native_function_callback` using the corresponding unit-local index. Use `ex_unit_get_path` to identify declarations from imported units.


## Diagnostic

- compilation currently stops after first reported error
- parser/checker/runtime diagnostics include source, line, and column when the source name is known
- imported source files report the import path, for example `core:vec3`

Examples:

```txt
maps/demo/demo.evox: line 50, column 4: Unexpected token near '_'
core:vec3: line 28, column 14: Arithmetic operands must have the same type
```

## Known underspecified areas

- `IndexingRequiresArrayTypeFails`
- `BytecodeGlobalInitializationOrder`
- `DeferCanNotWrapReturn`
- `NestedFunctionCanNotCaptureOuterLocal`
- `DuplicateDeclarationsFail`
- `ConstCanNotBeUndefined`
- `FunctionCallAssignmentFails`
- `FunctionNamedSinCompilesAndRuns`
- `ImportPathCanMatchPreviousAlias`
- `ImportAliasMissingMemberReportsMemberName`
- `MissingImportFails`
- `ImportResolverRejectsImportFails`
- `DuplicateUnaliasedImportFails`
- `ImportAliasEntityResolution`
- `ImportExternFnDuplicateNotUsed`
- `ImportAliasExternFnReturnTypeRequiresDirectImport`
- `ForLoopRangeBoundsMustMatchTypeFails`
- `ForLoopRangeRequiresNumericBoundsFails`
- `MatchRejectsPatternTypeMismatch`
- `MatchRangeRequiresNumericTypeFails`
- `MatchDuplicateFallbackFails`
- `NullOnlyAssignableToNullable`
- `NonMinusOperatorRequiresTwoParametersFails`
- `BooleanNotRequiresBoolOperandFails`
- `UnaryMinusRequiresNumericOperandFails`
- `RefExpressionOnlyAllowedInArgumentsFails`
- `RefArgumentTypeMismatchFails`
- `match` needs tighter rules for what counts as a valid pattern expression and the exact duplicate/exhaustiveness policy for non-enum subjects.
- `for` ranges should define whether bounds must match exactly, what type the loop variable has, and what happens for descending or overflowing ranges.
- Static-sized arrays still need complete rules for copy semantics and passing/returning by value. They have no built-in equality (see [Slice equality](#slice-equality)); whether that should stay, given that `xs[:] == ys[:]` expresses it, is open. Nesting reads left-to-right: `[4][8]i32` is an array of 4 arrays of 8 ints; `[][4]i32` is a slice of arrays of 4 ints.
- Nullable promotion should define `else if`, compound conditions, and scope boundaries in more detail.
- `defer` should define behavior on `break`, `continue`, runtime errors, and nested scopes, not only normal exit and `return`.
- Imports and `extern` bindings still need explicit collision policy for same-path/same-alias cases, builtin module boundaries, and imported declaration conflicts.
- Function values need clearer rules for equality/identity interactions with function declarations and literals.
- Type member and descriptor names (`t::name`, `f.name`, `e.name`) are unqualified declaration names. Two modules that both declare `Vec3` produce the same `t::name`, and a generic `print` cannot distinguish them; whether these should be module-qualified is unresolved, and interacts with the import collision policy noted above.
- `.length` is the built-in member for arrays, slices, and [reflection sequences](#reflection-sequences). It does not conflict with ordinary functions such as `length(v)` defined by a library for a struct magnitude.


## Design decisions

- type factories use ordinary function-call syntax
	- generic types are functions returning `type`, so nested types read naturally as `Box(Pair(i32))`
	- type arguments and runtime function arguments share the same call syntax

- static-sized arrays and slices use prefix notation
	- arrays are `[N]T` and slices are `[]T`, not postfix `T[N]` or `T[]`
	- `[]const T` is the read-only form of `[]T`; both have the same runtime layout
	- avoids grammar ambiguity: type factories use `Identifier(...)`, while arrays and slices retain their unambiguous prefix notation
	- consistent with prefix nullable `?T`: types read outside-in rather than inside-out
	- consistent direction with slices: `[]T` reads left-to-right like `?T`, unlike C-style postfix where nesting order is confusing (`T[4][8]`)
	- runtime operations (indexing `arr[i]`, slicing `arr[start:end]`) remain postfix on values, creating a clear distinction from prefix array and slice type constructors

- pointer types and dereference use different positions
	- we need an easy way to implement self-referential structures (e.g. linked lists) - pointers
	- pointer types use familiar prefix notation: `*T` and `*const T`
	- dereference uses postfix `p.*`, so `p.* = value` is unambiguously pointee assignment
	- keeping dereference postfix avoids the parser ambiguity in expressions such as `foo(*X)`, where `*X` could otherwise be a pointer type value or a dereferenced pointer
	- pointer field access remains concise: `p.field` automatically dereferences `p`, while `p.*.field` spells the same operation explicitly

- `for` uses `in` for both ranges and sequences
	- `in` is familiar from other languages' loop syntax, reducing the learning curve

- raw memory api
	- we want raw memory api so users can implement their own containers, arenas and other features
	- the primitive currency is the byte slice `[]byte`; `alloc` returns one and `free` takes one back (Zig-style allocator interface)
	- `byte` is a distinct type from `u8` (untyped storage vs a numeric type); its bit width is implementation-defined, so `sizeof`/`alignof` are measured in `byte` units
	- `sizeof`/`alignof` produce untyped integer constants rather than a fixed type, so they concretize to context (array size, type-factory value argument, `comptime` parameter, or `isize` in a size expression)
	- `[]byte as []T` / `[]T as []byte` reinterpret the same storage without copying so containers can expose a typed view over a raw allocation

- signed sizes
	- `isize` (sizes, lengths, indices) is signed, not unsigned
	- fixed 64 bits on all targets (not pointer-width) so size/index arithmetic is portable; 63 bits of range exceeds any realistic allocation
	- modern language-level precedent leans signed: Go `int` for `len`, Swift `Int` for `count`; C++ leadership (Stroustrup, Carruth, Google style) treats unsigned sizes as a mistake
	- a lot have been writen about advantages about both signed and unsigned size, there's no clear winner
		- https://graphitemaster.github.io/aau/
		- https://c3-lang.org/blog/unsigned-sizes-a-five-year-mistake/

- memory safety
	- options: borrow checker, gc, limit features only to memory safe ones, not memory safe, runtime safety like Fil-C
	- borrow checker makes the language and compiler more complicated 
	- limiting features would make the language way too limited
	- forced null checks - can still be unsafe (see [Nullable values](#nullable-values)). Not possible to solve while keepking the language and compiler simple.
	- bound-checked slice as primitive type in the language - while not guaranteed, it makes memory safety errors a bit less probably
	- **open questions**: gc, not memory safe, like Fil-C

- ufcs
	- method like syntax without actual methods
	- easy autocomplete
	- easy to "extend" the type, unlike normal methods

- import
	- solving namespace issues with aliasing at import site
	- without all the issues caused by includes in C or C++ 

- ADL
	- less noise `lib.foo(val)` -> `foo(val)`
	- a way to have overloads without actual overloads

- operators
	- operators are clearly useful, why would we have them for primitive types otherwise
	- very noisy and hard to read without it `add(add(mul(a.x, b.x), mul(a.y, b.y)), mul(a.z, b.z))` vs `a.x * b.x + a.y * b.y + a.z * b.z`
	- enum parameters are disallowed in operator overloads
		- enums are labels, not values to compute with; arithmetic on them is semantically odd
		- every modern language that designed enums carefully (Rust, Swift, Kotlin, Zig) keeps arithmetic off enums and handles bit-flag patterns through a separate mechanism (wrapper struct, macro, or protocol)
		- bit-flag use cases are served by wrapping the integer in a struct
		- allowing enum operators creates resolution complexity (shorthand `.Foo` in match-and-commit overload resolution) for no practical gain
	- `not` binds looser than the comparisons and `is`, but tighter than `and` and `or`
		- word operators keep the word-operator convention: `and`, `or`, and `not` sit together at the bottom of the precedence ladder, as in Python and Lua, rather than `not` sitting with unary `-` the way C's `!` does
		- the two constraints are independent: looser than the comparisons makes `not a == b` and `not e is T` read as written, while tighter than `and` keeps De Morgan rewriting direct (`not a and not b` negates each operand, not the conjunction)
		- C's placement is a known trap - `!a == b` silently means `(!a) == b`. Requiring `bool` operands would turn that into a compile-time error here anyway, so the precedence choice is about ergonomics rather than safety
	- typeless struct literals do not participate in operator overload resolution
		- expected-type inference remains useful when one destination type is already known, such as an annotated variable, return, or function argument
		- using a candidate parameter as that expected type makes overload selection recursive and creates surprising ambiguities
		- requiring `Type { ... }` at an operator boundary keeps resolution based on natural operand types and avoids candidate-specific AST typing

- slice equality is built in
	- strings are `[]const u8`, so without it the single most common comparison in scripting code has no direct spelling
	- a library `equals(a, b)` is unusually awkward here: a slice has no declaring namespace, so neither [UFCS](#ufcs) nor [ADL](#argument-dependent-lookup) finds such a function, and every call site would need an explicit import and qualification - while `slice.length` is built in
	- compile-time introspection makes it necessary rather than merely convenient: `t::name`, `f.name`, and `e.name` are compile-time `[]const u8`, and generic code cannot branch on them at all without comparison. The compiler already matches byte slices against declared names for computed struct access (see [Structs](#structs))
	- content, not pointer identity: identity is nearly useless for a view type, and making the common intent silently wrong is worse than rejecting it
	- restricted to element types with built-in equality so that `==` never dispatches to a user `operator ==` in a loop. That would put an unbounded chain of user calls behind a primitive-looking operator and would need a rule for which side wins against the overload
	- the cost is that `==` is O(n) for slices while every other built-in `==` is constant time. Accepted: the alternative is the same loop written out by hand at every call site
	- a distinct `string` type was considered and rejected for now. It would need a conversion lattice with `[]const u8`, a carve-out so that slicing a string yields a string, a `TypeKind`, and a second spelling at every API boundary - all to hang `==` off a nominal type. It becomes worth revisiting if the type carries a real invariant, such as guaranteed UTF-8 or NUL-termination for free `cstr` interop
	- ordering (`<`, `<=`, `>`, `>=`) is deliberately not included; lexicographic ordering is a library concern until sorting needs it

- match fallback uses an empty `case:`
	- keeps every match arm under the `case` keyword
	- avoids adding a separate `default` keyword
	- `else` remains exclusive to `if`, avoiding dangling-`else` ambiguity when the last statement of a match arm is an `if`
	- alternatives considered were `else:`, `default:`, and `case _:`

- tagged unions
	- the member type is the tag; no named variants (Rust-style `enum` payloads) keeps the feature small - two variants with the same payload type use wrapper structs
		- var a : SomeUnion = SomeMember { 1, "foo" }; is possible and uses only existing language features
	- structural set semantics (order-insensitive, flattening) so anonymous unions like `Error | ASTNode` compose across modules and call layers
	- subset → superset widening is implicit so error unions propagate without manual re-wrapping, including through [`else return`](#union-extraction-and-propagation)
	- promotion in `match`/`is` is flow-typing with the same accepted unsoundness as nullable promotion - keeping the checker simple was preferred over a borrow-like aliasing rule
	- [narrowing](#narrowing) is one residual-type rule (member set minus excluded members) shared by the `else` branch, the `match` fallback arm, and post-early-return flow; promotion to a single member is just the case where one member is left
		- only a bare `e is T` narrows - negated and compound conditions are not analyzed. This keeps the checker's flow analysis to a single syntactic form, at the cost of `not (e is T)` reading as unnarrowed
	- excluded from ADL/UFCS because a structural type has no declaring namespace
	- **open questions**: propagation sugar, canonical member order exposure

- compile-time branches are implicit
	- `if` and `match` become compile-time branches whenever their condition is compile-time known; there is no `comptime if` / `comptime match` spelling
	- one construct instead of two: generic code reads like ordinary code, and a condition that changes from runtime to compile-time known does not require rewriting the statement
	- Zig precedent: the same rule, and the same reason - once type-driven branching is expressible at all, a separate keyword is redundant noise on nearly every generic function
	- the cost is that unselected arms are unchecked and nothing in the syntax says so - a rename or signature change is not reported until some build actually selects that arm:

		```cpp
		comptime DEBUG = false;

		if DEBUG {
			log_state(v); // not checked while DEBUG is false, even if log_state no longer exists
		}
		```

		this is accepted deliberately: without pruning, generic code could not branch on type at all. Branches over configuration flags should be exercised by building every configuration
	- alternative considered was an explicit `comptime if` / `comptime match` opt-in, which makes the unchecked region auditable at the cost of a second form of every branch

- attributes use typed struct declarations
	- attributes are inspectable at compile time and are intended for serialization, GUI metadata (labels, ranges, and similar properties), tagging, and native integration; runtime reflection is not provided yet
	- the attribute metadata is also exposed to C++ so native code can consume it
	- a string payload such as `#"min = 32, max = 52"` was rejected because typos and malformed values are unchecked
	- an array of primitive key/value pairs was rejected because names remain unchecked and user-defined structured values such as `#[range {0, 10}]` do not fit naturally
	- function attributes were rejected because a function such as `fn min_attr(i : i32) : void {}` has no meaningful execution semantics in an attribute position
	- Rust-style macros were rejected as a new, complex macro system with the disadvantages of the other approaches
	- C#-style attributes were rejected because they require a base class, while Evox has no inheritance
	- sidecar declarations were rejected because they require more code and make associating metadata with the intended type error-prone
	- representing attributes as one generated struct was rejected because access through its fields is cumbersome; a heterogeneous attribute sequence is more direct even though iteration requires compile-time type checks
	- Java-style typed lookup was rejected because iteration would remain heterogeneous and it would introduce a new `::function()` call concept

- reflection members use `::`
	- `T::kind`, `T::fields`, `T::values` and the rest live in a namespace disjoint from `.`, so they never compete with user-declared names
	- the collision is enum-specific: enum member access is `State.Running`, so on `.` a reflection member and a declared member would fight for one spelling. Structs have no `Type.member` access, and compiler-provided descriptors have no user-declarable members, so both keep `.`
	- the deciding argument is extensibility, not disambiguation. Reserving `kind`, `name`, `child`, `length`, `fields`, `values`, `types` as enum member names would work today, but it makes adding a reflection member later a breaking change for every enum already using one of those names. A disjoint namespace costs nothing to grow
	- `::` is otherwise unused in the grammar, and unlike `.` it has no meaning to overload
	- `@` was considered - `T@kind` - and rejected because `@` has two other claimants: explicit field offsets (`x : f32 @ 4`, still open) and a possible attribute syntax
	- a Zig-style `typeinfo(T)` returning a tagged union over per-kind info types was considered and rejected. It is attractive because the kind guard on `T::fields` would collapse into ordinary union promotion, but it needs the scrutinee bound to a name before matching, it requires union membership to admit compile-time-only types, and it needs roughly eight nameable built-in info types - trading seven reserved enum member names for eight reserved global type names
	- the cost of `::` is that reflection is a second member-access syntax to learn, and the kind guard on `T::fields` stays a bespoke checker rule rather than falling out of the type system

- undefined behavior
	- compared to C or C++, try to define as much behavior as possible
	- defined signed integer overflow
	- **open questions**: should we have any undefined behavior?

# TODO

* if var a = some_nullabe { ... } else { ... } and same for match
* union tag is always 4 bytes
* https://verdagon.dev/grimoire/grimoire#the-list
* should we make string literal to cstr cast explicit?
* hex - 0x1234ABCD
* FourCC? `ABCD`
* bit set / flags / something else?
* null chaining a?.b?.c;
* list of keywords and forbid identifier colliding with keywords
* debugger:
	- modify variables while paused
	- conditional breakpoints
	- data breakpoints?
	- REPL?

* editor plugins in evox
* get rid of std::free
* how to expose Span<const Item> foo() to script?
* how can we push unions if we don't know the tag value of variants, i.e. U = A | B - we don't know if A's tag is 0 or 1
* use case - compile-time string hash
* MT typecheck

---

* enum backing type
* do temporaries survive until the end of statement? e.g. foo(bar().view())
* jit/llvm/AOT?
* AST API in evox?
* getter/setter?
* traits/interfaces?
* with/when/where/using?
* context object?
* multiple returns?
* runtime reflection? (compile-time introspection is specified, see [Compile-time introspection](#compile-time-introspection))
* gc?
* attributes?
* fibers/coroutines?
* closures?
* generators/yield (custom pull iterators use the `next` protocol above)
	fn each(a : arr) : yield i32 { ...
	for x in each(a) { ... }

