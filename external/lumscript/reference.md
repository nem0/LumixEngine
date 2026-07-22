# This is in early prototype/exploration stage, everything can change

# TODO

* 1'000'000 / 1_000_000
* FourCC? `ABCD`
* i32::min, i32::max, T::size, T::alignmennt, T::kind
* bit set / flags / something else?
* null propagation a?.b?.c;
* list of keywords and forbid identifier colliding with keywords
* debugger:
	- modify variables while paused
	- conditional breakpoints
	- data breakpoints?
	- REPL?

* editor plugins in lumscript
* getNumControllerHits + getControllerHit to slices
* get rid of std::free
* how to expose Span<const Item> foo() to script?
* how can we push unions if we don't know the tag value of variants, i.e. U = A | B - we don't know if A's tag is 0 or 1
* use case - comptime string hash
* varargs - [Compile-time introspection](#compile-time-introspection) covers single-argument `print`; `print(a, " ", b)` still needs a variadic mechanism
* string interpolation
* MT typecheck

---

* jit/llvm/AOT?
* AST API in lumscript?
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
* iterators/yield 
	fn each(a : arr) : yield i32 { ...
	for x in each(a) { ... }
* extern struct or explicit field offset (so we can access C struct directly)
	struct S {
		x : f32 @ 4; // 4bytes offset 
	}
	or 
	extern struct S { // automatically matches c abi

# Goals
 * **simple** - string concatenation: `"Hello " + "World!"`. Avoid verbose low level code.
 * **safe**	- nullable values with forced null check to access
 * **efficient** - no unnecessary allocations, fast

# LumScript

LumScript is a small, statically typed scripting language for Lumix Engine.

See the [benchmark results](benchmarks/results.md) for current performance comparisons.

## Table of contents

- [Design goals](#design-goals)
- [Design decisions](#design-decisions)
- [Quick example](#quick-example)
- [Source files](#source-files)
- [Declarations](#declarations)
	- [Imports](#imports)
	- [Structs](#structs)
	- [Enums](#enums)
	- [Functions](#functions)
	- [Comptime](#comptime)
	- [Templates](#templates)
	- [Operators](#operators)
	- [Ref parameters](#ref-parameters)
- [Types](#types)
	- [Untyped literals](#untyped-literals)
	- [Nullable values](#nullable-values)
	- [Tagged unions](#tagged-unions)
		- [Narrowing](#narrowing)
	- [Strings](#strings)
	- [Function types](#function-types)
	- [Static-sized arrays](#static-sized-arrays)
	- [Slices](#slices)
- [Memory](#memory)
- [Variables](#variables)
	- [Union extraction and propagation](#union-extraction-and-propagation)
- [Statements](#statements)
	- [Blocks](#blocks)
	- [Assignment](#assignment)
	- [If / else](#if--else)
	- [Match](#match)
	- [Compile-time branches](#compile-time-branches)
	- [While](#while)
	- [For](#for)
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
	- [Operator precedence](#operator-precedence)
	- [Ternary operator](#ternary-operator)
	- [Calls](#calls)
	- [Argument-dependent lookup](#argument-dependent-lookup)
	- [UFCS](#ufcs)
	- [Field access](#field-access)
	- [Struct literals](#struct-literals)
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

Current implementation includes:

- parser
- type checker
- bytecode runtime
- `.lum` asset registration
- basic Studio editor integration

JIT is intentionally out of scope for the first version.

## Design goals

- simple: readable high-level code with minimal boilerplate
- safe: nullable values require explicit null checks
- efficient: avoid unnecessary allocations and keep runtime overhead low

## Design decisions

- struct templates use []
	- main reason we have struct templates at all are user-defined containers
	- function templates do not use a separate bracketed parameter list; generic type parameters are introduced in the function signature with `$T`, and compile-time value parameters use `comptime`
	- we want a pair of enclosing characters, single character is hard to read when nested: `Array@Array@i32`
	- different begin and end, reads easier, especially when nested, e.g. if we used `"` - `Box"Pair"i32, string""`
	- so our options are `[]`, `<>`, `()`, `{}`
	- there are already languages using `[]` and `<>` for type templates
	- `<>` is harder to parse thana `[]`

- static-sized arrays and slices use prefix notation
	- arrays are `[N]T` and slices are `[]T`, not postfix `T[N]` or `T[]`
	- avoids grammar ambiguity: `Identifier[...]` in type position could be array type, struct template instantiation, or comptime variable indexing; prefix `[` resolves this
	- consistent with prefix nullable `?T`: types read outside-in rather than inside-out
	- consistent direction with slices: `[]T` reads left-to-right like `?T`, unlike C-style postfix where nesting order is confusing (`T[4][8]`)
	- runtime operations (indexing `arr[i]`, slicing `arr[start:end]`) remain postfix on values, creating clear distinction: postfix `[` = runtime value operation, prefix `[` = compile-time type constructor

- `for` uses `in` for both ranges and sequences
	- `in` is familiar from other languages' loop syntax, reducing the learning curve

- raw memory api
	- we want raw memory api so users can implement their own containers, arenas and other features
	- the primitive currency is the byte slice `[]byte`; `alloc` returns one and `free` takes one back (Zig-style allocator interface)
	- `byte` is a distinct type from `u8` (untyped storage vs a numeric type); its bit width is implementation-defined, so `sizeof`/`alignof` are measured in `byte` units
	- `sizeof`/`alignof` produce untyped integer constants rather than a fixed type, so they concretize to context (array size, struct template argument, `comptime` parameter, or `isize` in a size expression)
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
	- `as` yields `?Member` instead of trapping, reusing the forced-null-check machinery instead of adding a runtime abort path
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

- reflection members use `::`
	- `T::kind`, `T::fields`, `T::values` and the rest live in a namespace disjoint from `.`, so they never compete with user-declared names (see [Type members](#type-members))
	- the collision is enum-specific: enum member access is `State.Running`, so on `.` a reflection member and a declared member would fight for one spelling. Structs have no `Type.member` access, and cursors have no user-declarable members, so both keep `.`
	- the deciding argument is extensibility, not disambiguation. Reserving `kind`, `name`, `child`, `length`, `fields`, `values`, `types` as enum member names would work today, but it makes adding a reflection member later a breaking change for every enum already using one of those names. A disjoint namespace costs nothing to grow
	- `::` is otherwise unused in the grammar, and unlike `.` it has no meaning to overload
	- `@` was considered - `T@kind` - and rejected because `@` has two other claimants: explicit field offsets (`x : f32 @ 4`, still open) and a possible attribute syntax
	- a Zig-style `typeinfo(T)` returning a tagged union over per-kind info types was considered and rejected. It is attractive because the kind guard on `T::fields` would collapse into ordinary union promotion, but it needs the scrutinee bound to a name before matching, it requires union membership to admit compile-time-only types, and it needs roughly eight nameable built-in info types - trading seven reserved enum member names for eight reserved global type names, and reversing the decision that cursor types are [not nameable in source](#reflection-sequences)
	- the cost of `::` is that reflection is a second member-access syntax to learn, and the kind guard on `T::fields` stays a bespoke checker rule rather than falling out of the type system

- undefined behavior
	- compared to C or C++, try to define as much behavior as possible
	- defined signed integer overflow
	- **open questions**: should we have any undefined behavior?

## Quick example

```cpp
import "core:vec3"

fn add(a : Vec3, b : Vec3) : Vec3 {
	const x = a.x + b.x;
	const y : f32 = a.y + b.y;
	const z = a.z + b.z;
	return { x, y, z };
}

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

A module contains top-level `import`, `comptime`, `struct`, `enum`, `fn`, and variable declarations.

## Source files

- LumScript files use the `.lum` extension.
- Whitespace is ignored.
- Line comments start with `//`.

```cpp
// comment
var speed : f32 = 12.5;
```

## Declarations

### Imports

Imports load another module by path.

```cpp
import "math"
```

The `.lum` suffix is omitted:

```cpp
import "std:math"
import "core:collections/list" as list
```

Imports map source names to declarations in imported modules. They are lookup sources, not scope injection.

Imported modules also contribute operator declarations to overload resolution.
Alias-qualified names are not used for operator syntax; aliasing only affects ordinary name lookup.

Import forms:

```cpp
import "math"
import "core:vec3"

fn main() : f32 {
	const v : Vec3 = Vec3 { 1, 2, 3 };
	return length(v);
}
```

```cpp
import "core:vec3" as vec

fn main() : f32 {
	const v : vec.Vec3 = vec.Vec3 { 1, 2, 3 };
	return v.x;
}
```

```cpp
import "std:math" as math

fn main() : f32 {
	return math.sin(0.0) + math.cos(0.0) + math.sqrt(4.0);
}
```

Builtin math functions live under `std:`. Use `std:math` for `sin`, `cos`, and `sqrt`.
The `std:` prefix is reserved for builtin modules and should not be used for user-defined imports.

Rules:

- importing the same path with the same alias in the same file is a compile-time error
- alias collisions are compile-time errors
- import cycles are compile-time errors
- imports are not transitive for symbol visibility
- an alias-qualified name resolves only through that alias
- a bare name resolves against the current module and then unaliased imports
- if no match is found and the call has at least one argument, the first argument's type namespace is also searched (see [Argument-dependent lookup](#argument-dependent-lookup))
- if a bare name matches more than one declaration, using it is a compile-time error
- if a bare name matches both a local declaration and an unaliased import, using it is a compile-time error
- unaliased imports are not a separate namespace and do not override local declarations

```cpp
import "core:vec3" as core
import "core:quat" as core // compile-time error: alias collision
```

```cpp
import "a"

// a.lum
import "b"

// b.lum
import "a" // compile-time error: import cycle
```

### Structs

```cpp
struct Transform {
	x : f32;
	y : f32;
	visible : bool;
}
```

Rules:

- field names must be unique within the struct
- field types can be primitive, enum, function type, or previously declared struct
- a trailing semicolon after the closing `}` is a compile-time error

### Enums

```cpp
enum State {
	Idle,
	Running,
	Paused,
	Done
}
```

Explicit values are allowed:

```cpp
enum Priority {
	Low = 0,
	Medium = 5,
	High = 10
}
```

Enums are strongly typed:

- no implicit conversion between enums and integers
- use explicit `as` casts when needed
- a trailing semicolon after the closing `}` is a compile-time error

```cpp
const key_code : i32 = Keycode.W as i32;
```

Shorthand member syntax works when enum type is unambiguous:

```cpp
fn handle_state(state : State) : void {
	if state == .Running {
		// equivalent to state == State.Running
	}
}

var priority : Priority = .High;
```

### Functions

```cpp
fn clamp_min(v : i32, min_value : i32) : i32 {
	if v < min_value {
		return min_value;
	}
	return v;
}
```

Rules:

- parameter names must be unique
- top-level `fn foo() : T { ... }` is syntax sugar for a module-level `comptime foo = fn() : T { ... }` binding
- overloading is not supported
- parameters are immutable
- nested functions are not supported
- this does not include operator declarations; operators are a separate declaration form

### Comptime

`comptime` declares a module-level binding whose initializer is evaluated during compilation. It is used for values that must be known before runtime code is checked or generated.

Primitive values can be bound at compile time:

```cpp
comptime N = 32;
comptime enabled = true;
comptime scale = 1.5;
```

These values can be used where the language requires a compile-time value, such as struct template arguments, static array sizes, `comptime` parameters, and other comptime expressions.

Types are compile-time values. Structs, enums, and functions can therefore be written as expressions and bound to names:

```cpp
comptime Vec2 = struct {
	x : f32;
	y : f32;
}

comptime State = enum {
	Idle,
	Running
}

comptime add = fn(a : i32, b : i32) : i32 {
	return a + b;
}
```

Declaration syntax is sugar over these bindings:

```cpp
struct Vec2 {
	x : f32;
	y : f32;
}

enum State {
	Idle,
	Running
}

fn add(a : i32, b : i32) : i32 {
	return a + b;
}
```

is equivalent to:

```cpp
comptime Vec2 = struct {
	x : f32;
	y : f32;
}

comptime State = enum {
	Idle,
	Running
}

comptime add = fn(a : i32, b : i32) : i32 {
	return a + b;
}
```

Struct template application uses square brackets. Runtime function calls use parentheses:

```cpp
var v : Vec2[f32] = Vec2[f32] { 1.0, 2.0 }; // [f32] applies the struct template
```

Comptime initializers may call functions that are known at compile time. Top-level functions are compile-time bindings to function values, so they can be evaluated during compilation when all arguments and all operations are compile-time-valid:

```cpp
fn double(v : i32) : i32 {
	return v * 2;
}

comptime N = double(16); // N == 32
```

Comptime calls do not create new type declarations. Type construction is intentionally limited to `struct[...]` template expressions, so arbitrary compile-time functions cannot return freshly declared struct or enum types:

```cpp
fn make_vec2(T : type) : type {
	return struct {
		x : T;
		y : T;
	};
}

comptime Vec2 = make_vec2(f32); // compile-time error
```

Use a struct template instead:

```cpp
comptime Vec2 = struct[T] {
	x : T;
	y : T;
}

fn main() : void {
	var v : Vec2[f32] = Vec2[f32] { 1.0, 2.0 };
}
```

Comptime evaluation cannot depend on runtime storage:

```cpp
var runtime_value : i32 = 16;
comptime N = double(runtime_value); // compile-time error
```

Rules:

- `comptime` bindings are immutable
- a `comptime` binding can produce a type, function, integer, float, bool, or other compile-time value
- a value used as a type must resolve to a compile-time type value
- compile-time evaluation happens before concrete runtime code is lowered
- compile-time application uses `[]`; runtime application uses `()`
- primitive types such as `i32`, `f32`, `bool`, and `string` are built-in type values and cannot be shadowed; so are the built-in `type` and `TypeKind`
- using a runtime-only value where a compile-time value is required is a compile-time error
- a comptime call may call only compile-time-known function values
- declared functions cannot return `type`; the compile-time operator [`typeof`](#typeof) is not a function and is not covered by this rule
- functions cannot create new struct or enum types from their body
- new generic types are declared with `struct[...]` not by returning `type` from a function
- native/extern functions are runtime-only unless explicitly marked otherwise by a future extension
- comptime evaluation has an implementation-defined recursion/step limit to prevent non-terminating compilation

### Templates

Templates are compile-time functions or type constructors. Template parameters are written inside square brackets. A bare parameter name is a type parameter; a parameter with an explicit type annotation, such as `N : i32`, is a compile-time value parameter.

**Struct templates:**

```cpp
struct Pair[T] {
	first  : T;
	second : T;
}

struct Optional[T] {
	value   : T;
	present : bool;
}
```

Struct template declaration syntax is sugar for a `comptime` binding to a generic struct expression:

```cpp
comptime Pair = struct[T] {
	first  : T;
	second : T;
}
```

Instantiation supplies concrete types in square brackets:

```cpp
fn main() : void {
	var p : Pair[i32] = Pair[i32] { 1, 2 };
	var s : Pair[f32] = Pair[f32] { 1.0, 2.0 };
}
```

**Function templates:**

```cpp
fn identity(a : $T) : T {
	return a;
}

fn swap(a : ref $T, b : ref T) : void {
	const tmp = a;
	a = b;
	b = tmp;
}
```

`$T` in a parameter type introduces an inferred compile-time type parameter named `T`. Later uses of `T` in the same signature or body refer to that type.

Function template declaration syntax is sugar for a `comptime` binding to a generic function expression:

```cpp
comptime identity = fn(a : $T) : T {
	return a;
}
```

The compiler infers type parameters from argument types only:

```cpp
fn default_value(fallback : $T) : T {
	return fallback;
}

fn main() : void {
	const x = identity(42);                      // T inferred as i32
	const y = identity(3.14);                    // T inferred as f32

	var a : i32 = 1;
	var b : i32 = 2;
	swap(ref a, ref b);                          // T inferred as i32

	const v = default_value(0);                  // T inferred as i32
}
```

Type parameters cannot be inferred from the return type context. Type parameters that cannot be inferred from arguments must be passed as explicit compile-time type parameters:

```cpp
fn make(T : comptime type) : T {
	return undefined;
}

fn main() : void {
	const x : i32 = make(i32);  // T must be explicit
}
```

**Multiple type parameters:**

```cpp
fn first(a : $A, b : $B) : A {
	return a;
}

struct Map[K, V] {
	key   : K;
	value : V;
}
```


**Compile-time value parameters:**

Function parameters can also be marked `comptime` to require compile-time constant values. These are useful for values that must be known at compile time but do not require template bracket syntax:

```cpp
fn repeat(text : string, count : comptime i32) : void {
	for i in 0..count {
		print(text);
	}
}

fn splat(value : f32, n : comptime i32) : [n]f32 {
	var result : [n]f32 = undefined;
	for i in 0..n {
		result[i] = value;
	}
	return result;
}

fn main() : void {
	repeat("hi", 3);           // valid: 3 is a compile-time constant
	repeat("hi", some_var);    // error: some_var is not a compile-time constant
	
	const arr = splat(1.0, 4); // arr has type [4]f32
}
```

Comptime parameters:
- must be initialized with a compile-time constant value at the call site
- allow dependent function signatures where return types or array sizes depend on the parameter value
- are checked at compile time but do not require explicit bracket syntax at the call site
- compose with inferred type parameters in the same function signature, for example `fn foo(a : $T, i : comptime i32) : void`
- struct templates continue to support value parameters via bracket syntax: `struct[T, N : i32]`

Rules:

- a fully instantiated template type such as `Pair[i32]` or `Box[Pair[i32]]` is a concrete type and can be used anywhere a concrete type is valid: variable declarations, function parameters, return types, struct fields, and as type arguments to other struct templates
- `$T` introduces a function type parameter once; later uses must be written as `T`, and repeating `$T` for the same name in a signature is a compile-time error
- type parameter names must be unique within a function signature or struct template parameter list
- template parameters are resolved at compile time; no runtime overhead is incurred
- inferred function type parameters and explicit struct template arguments must satisfy the structural requirements of the template body (field access, arithmetic, etc.); mismatches are compile-time errors
- recursive struct templates are not supported
- function type parameters are inferred from value arguments; a function type parameter that cannot be inferred must be passed as a regular compile-time type parameter, for example `fn make(T : comptime type) : T`
- type arguments in struct instantiations drive the expected type of value arguments, the same way a concrete parameter type does
- the count of explicit struct template arguments must equal the count of struct template parameters
- template functions and structs from imported modules can be instantiated in the importing module; alias-qualified syntax applies as normal: `lib.identity(42)`, `lib.Pair[i32] { 1, 2 }`
- a function template becomes a concrete function value after its type parameters and `comptime` parameters are known; that concrete value can be used anywhere a function value of that signature is valid: assignment, passing as an argument, returning, storing in a variable
- operator overloads cannot be templated; use a concrete instantiation instead

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

### Ref parameters

`ref` passes a writable location by alias instead of by value.

```cpp
fn increment(v : ref i32) : void {
	v += 1;
}

fn main() : void {
	var x : i32 = 10;
	increment(ref x);
}
```

`ref` constraints:

- call-site argument must be prefixed with `ref`
- argument must be writable and have stable storage
- `const` values are not allowed
- `ref` parameter types cannot be nullable -- TODO why?
- `ref` arguments cannot be nullable -- TODO why?

```cpp
struct Stats {
	hp : i32;
};

struct Player {
	stats : Stats;
};

var global_counter : i32 = 0;

fn bump(v : ref i32) : void {
	v += 1;
}

fn main() : void {
	var p = Player { Stats { 10 } };
	bump(ref global_counter);
	bump(ref p.stats.hp);
}
```

## Types

Built-in and user types:

- `void`
- `bool`
- `i8`, `u8`, `i16`, `u16`, `i32`, `u32`, `i64`, `u64`
- `isize`
- `byte`
- `f32`, `f64`
- `string`
- `cstr`
- `cptr`
- `type` (compile-time only)
- `TypeKind` (compile-time only, see [TypeKind](#typekind))
- user-defined `struct` types
- user-defined `enum` types
- function types
- tagged union types (`A | B`)

`isize` is the signed integer type used for memory sizes, slice lengths, and indices. It is signed and a fixed 64 bits on all targets (not platform/pointer-width dependent).

- it is the parameter type of the raw-memory allocator's size/alignment arguments, the return type of `length`, the type of a slice's length, and the type used for indexing
- indexing is bounds-checked against `0 <= i < length`, so a negative index is a runtime error

See [Design decisions](#design-decisions) for why sizes are signed.

`byte` is the smallest addressable unit of raw memory. It is distinct from `u8`: `u8` is a numeric type with a fixed width, while `byte` represents untyped storage. `sizeof` and `alignof` are measured in bytes. The raw-memory allocator works in terms of `[]byte` (a byte slice), and `[]byte` can be reinterpreted as a typed slice (see [Casts](#casts)).

`Vec3`, `DVec3`, and `Quat` are core value types used heavily by engine APIs:

```cpp
import "core:vec3"
import "core:dvec3"
import "core:quat"

const position = Vec3 { 1.0, 2.0, 3.0 };
const world_position = DVec3 { 1000.0, 2000.0, 3000.0 };
const rotation = Quat { 0.0, 0.0, 0.0, 1.0 };
```

### Untyped literals

Numeric literals and expressions composed only from untyped numeric constants remain untyped during checking:

- integer literals start as untyped integer
- decimal literals start as untyped float
- arithmetic on untyped constants produces another untyped constant; for example, `12 + 13` is an untyped integer constant with value `25`

Context (target type, argument type, return type, cast, expression expectation) concretizes them.

```cpp
fn takes_f32(value : f32) : void {}

const a = 12 + 13;       // defaults to i32
const b : f32 = 12 + 13; // the expression is concretized as f32
const c = 12.5;          // defaults to f64
takes_f32(12);           // 12 is concretized as f32
const big = 2147483648;  // infers i64
const huge = 18446744073709551615; // infers u64
```

An untyped constant can be concretized as a numeric type only when its value is representable by that type. This contextual concretization is not an implicit cast between concrete numeric types.

Defaults when context is insufficient:

- integer literals infer `i32` when they fit, otherwise `i64`, otherwise `u64`
- integer literals that do not fit `u64` are compile errors
- decimal literals default to `f64`

There are still no implicit numeric casts between concrete types.

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

The same applies when the `else` branch returns:

```cpp
if e != null {
	use_entity(e);
} else {
	return;
}

use_entity(e); // e is promoted to entity.Entity
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
- writing the same member twice directly (`A | A`, including through aliases) is a compile-time error
- duplicates that arise from flattening composed unions silently collapse into the set: if `comptime AB = A | B` and `comptime BC = B | C`, then `AB | BC` is `A | B | C`

**Members**

Any concrete runtime type can be a member: structs, enums, primitives, and `string`. Excluded:

- `void`
- `null` - `null` as a member is a compile-time error
- nullable types (`?T`); nullable union syntax (`?(A | B)`) is not supported
- function types - use nullable function types (`?fn(...)` ) or wrap in a struct instead
- unions (they flatten, see above)
- slices (`[]T`) - a slice is a view without ownership; the union tag does not distinguish which backing storage a slice view represents
- static arrays (`[N]T`) - use proper container types (structs, function-based interfaces) instead of unioning different array sizes

All members must be pairwise distinct types. Because the member type is the tag, two semantically different variants with the same payload type require wrapper structs.

**Coercion**

- a member value coerces implicitly to any union containing its type: `var e : InputEvent = b;`
- a union value coerces implicitly to any union whose member set is a superset (the tag is remapped at the coercion site): an `A | B` value can be assigned where `A | B | C` is expected - this is what lets error unions propagate across call layers
- no other implicit conversions apply; narrowing (superset to subset, or union to member) is never implicit

**Testing and extraction: `is` and `as`**

- `e is ButtonEvent` evaluates to `bool`: whether the active variant is `ButtonEvent`
- `if e is ButtonEvent { ... }` promotes `e` to `ButtonEvent` inside the branch, like nullable promotion in `if e != null`; the `else` branch and the code after an early return narrow it too (see [Narrowing](#narrowing))
- `e as ButtonEvent` evaluates to `?ButtonEvent`: the payload when the active variant matches, `null` otherwise; the usual forced null check applies before use
- there is no trapping variant cast
- `is` / `as` with a type that is not a member of the union is a compile-time error

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
- a narrowed subject keeps the residual type for member access, `is`, `as`, `match`, and [`typeof`](#typeof)
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

`==` and `!=` are not defined for union values; comparing two unions is a compile-time error. Narrow first (`is`, `as`, or `match`) and compare the payloads.

**Layout**

- storage is a tag followed by payload space sized for the largest member
- the tag is an `i32` holding the member's index in the union's canonical member order; the canonical order is deterministic but implementation-defined (member sets are unordered at the language level)
- `sizeof(A | B)` is `sizeof(i32) + max(sizeof(members))`
- `alignof(A | B)` is `max(alignof(i32), alignof(members))`
- the tag is not directly observable; there is no union-to-integer cast

### Strings

```cpp
fn greet(name : string) : string {
	const prefix = "Hello ";
	return prefix + name;
}
```

- string literals produce `string`
- concatenation uses `+`
- string interpolation is not implemented

`string` is LumScript's normal counted-string type. It carries a byte length, so it can represent arbitrary byte sequences, including embedded null bytes.

`cstr` is a distinct borrowed, NUL-terminated C string type for native interop. It maps to `const char*` in C and is intended for declarations such as:

```cpp
extern fn puts(text : cstr) : i32;

fn main() : void {
	puts("hello");
}
```

`string` converts implicitly to `cstr` without copying, including when passing a normal string to a native function or initializing a `cstr` variable. The generated string storage is NUL-terminated:

```cpp
var text : string = getMessage();
var native_text : cstr = text;
```

The conversion requires NUL-terminated storage and rejects strings containing an embedded null byte; C APIs conventionally stop at the first null and cannot otherwise observe the full LumScript string. The resulting `cstr` is borrowed: it is valid only while the source string remains alive and is not mutated or released. `cstr` has no ownership or freeing behavior.

Conversion in the other direction is also explicit and copies the C string into normal LumScript-owned string storage:

```cpp
var native_text : cstr = getNativeMessage();
var text : string = native_text as string;
```

`cstr as string` scans up to the first null byte and copies those bytes. It is therefore `O(n)` and the resulting `string` remains valid if the native library later mutates or releases the original buffer. Converting a null `cstr` requires a nullable check first.

`cptr` is a separate opaque raw native pointer type. Use it for handles, raw memory, and dynamic-library symbol lookup; do not use it for C text when a `cstr` parameter is available. The `null` literal is valid wherever a `cptr` is expected and represents a null native pointer:

```cpp
extern fn MessageBoxA(window : cptr, text : cstr, caption : cstr, flags : u32) : i32;

MessageBoxA(null, "Hello", "LumScript", 0);
```

### Function types

Function type syntax:

```cpp
fn(i32, i32) : i32
```

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

Declaration syntax for fixed-size arrays uses prefix size:

```cpp
var d : [16]i32 = undefined;
```

Usage syntax:

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
- `length(arr)` on a `[N]T` produces the untyped compile-time integer constant `N`, so it can be used where a compile-time integer is required (`unroll for` bounds, array sizes, `comptime` parameters). When context does not require another type it concretizes to `isize`, matching `length` on a slice
- postfix `[` in type position (after a type constructor like size and element) means indexing or slicing on runtime values; array types always use prefix `[N]T` notation

### Slices

Slices are lightweight views over contiguous storage. A slice does not own its elements; it stores a pointer to the first element plus a length.

```cpp
var arr : [4]i32 = foo();
var slice : []i32 = arr[1:2];
```

Slice syntax uses `[]T`, where `T` is the element type.

Slice creation forms:

- `arr[start:end]` creates a slice from a static array or another slice
- `arr[start:]` uses the remainder of the storage to the end
- `arr[:end]` starts at the beginning
- `arr[:]` creates a slice over the whole range
- slicing uses half-open bounds: `start` is inclusive and `end` is exclusive
- omitted bounds default to the beginning or end of the source range
- slicing never copies elements
- slicing a slice produces another slice over the same backing storage
- an array can be passed to a parameter of slice type implicitly

```cpp
var x : []i32 = arr[1:2];
var y : []i32 = arr[1:];
var z : []i32 = arr[:7];
var z2 : []i32 = z[2:4];
var w : []i32 = arr[:];
var sub : []i32 = slice[1:3];
var q = arr[3:4];

fn foo(slice : []i32) : void {}
var arr : [16]i32 = bar();
foo(arr); // automatic conversion
```

Slice operations:

- slices can be indexed with `slice[i]`
- indexing is bounds-checked at runtime
- `length(slice)` returns the number of elements in the slice
- a slice can be initialized with `null`, which creates an empty slice
- slices can be stored in variables, passed to functions, and returned from functions
- assigning one slice to another copies only the pointer and length
- a slice remains valid only while the backing storage remains alive and stable
- writing an element through a slice mutates the viewed storage, not the slice binding, so element writes are allowed even when the binding itself is immutable (for example a function parameter of slice type)

```cpp
fn sum(values : []i32) : i32 {
	var total : i32 = 0;
	var i : i32 = 0;
	while i < length(values) {
		total += values[i];
		i += 1;
	}
	return total;
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

## Variables

```cpp
var counter : i32 = 0;
const step = 1;
comptime max_entities = 1024;

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

`match` is a non-fallthrough multi-way branch for enum and scalar values. Each `case` can contain any number of statements; execution stops at the end of the selected case and does not fall through to the next case.
An empty `case:` is the fallback arm. `else` is reserved for `if` statements.

Supported patterns:

- enum members
- scalar literals
- ranges (`a..=b`, inclusive on both bounds)
- member types, when matching a tagged union value (see [Tagged unions](#tagged-unions))
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

Enum matches must be exhaustive unless an empty `case:` fallback is present. Duplicate enum cases and multiple fallback cases are compile-time errors.

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

A condition or scrutinee is compile-time known when it is built only from values the compiler already has: literals, `comptime` bindings, `comptime` parameters, template arguments, and calls to compile-time-evaluable functions over such values. Reading a `var`, a `const` initialized from runtime data, or the result of a runtime call makes the expression runtime-valued, and the branch is an ordinary runtime branch.

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

Both bounds must have the same integer type, which becomes the type of the loop variable. An untyped bound adopts the other bound's concrete type (`for i in 0..length(s)` iterates with an `isize` loop variable); if both bounds are untyped they default to `i32`.

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
for i in 0..length(arr) {
	const v = arr[i];
	log.logError(v);
}
```

`for i, v in arr` is the same desugaring, additionally exposing the index as `i`:

```cpp
for i in 0..length(arr) {
	const v = arr[i];
	log.logError(i);
	log.logError(v);
}
```

- `arr` must be a static-sized array or a slice
- `i` has type `isize`; `v` has the element type of `arr`. This holds for static arrays too: `length` on a `[N]T` is an untyped compile-time constant, and the desugared range concretizes it to `isize`
- both `i` and `v` are immutable inside the loop body, like the single-variable `for` loop variable

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
12.5
```

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

`as` on a tagged union value with a member type produces a nullable payload, for example `e as ButtonEvent` has type `?ButtonEvent` (see [Tagged unions](#tagged-unions)).

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
- both produce an untyped integer constant, usable wherever a compile-time integer is required (array sizes, struct template value arguments, `comptime` parameters, other comptime expressions)
- `sizeof(T)` is the size of `T` measured in `byte` units: `byte`, `bool`, `i8`, and `u8` are 1 byte; `i16`/`u16` are 2; `i32`/`u32`/`f32`/enums/function values are 4; `i64`/`u64`/`isize`/`f64`/strings/pointers are 8; a slice is a pointer plus an `i64` length; an array is `size * sizeof(element)`; a struct is the sum of its field sizes; and a tagged union is `sizeof(i32)` for the tag plus the size of its largest member
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
- `string` - content comparison
- `cstr` and `cptr` - address comparison (two `cstr` values with equal content but different storage are not equal)
- function values - same function
- nullable values - only against the `null` literal
- `type` values - type identity, compile-time only (see [Type equality](#type-equality))

Arrays, slices, unions, and two nullable values have no built-in equality; comparing them is a compile-time error. Structs resolve `==` through `operator` declarations (see below).

If an operator is used with non-builtin value types, the compiler may resolve it to a matching `operator` declaration instead of a built-in primitive rule.
Primitive operands keep their built-in semantics and cannot be overridden by `operator` declarations.
`and` and `or` keep their built-in short-circuit semantics, and `not` keeps its built-in `bool`-only semantics; none of the three are candidates for operator declarations.
Compound assignment follows the same rule: a non-primitive left-hand target uses the corresponding binary operator, while a primitive left-hand target stays on the built-in path. In the latter case, the right-hand operand must be implicitly convertible to the left-hand target type; an expression such as `5 *= Vec2 { 1, 2 }` is therefore invalid.

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
| 10 | `-` (prefix), `ref` | right |
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
- if the resolved function takes its first parameter by `ref`, the receiver is passed by `ref`; `x.foo(a, b)` is then equivalent to `foo(ref x, a, b)`
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

This also permits mutable container operations without spelling `ref` at every call site:

```cpp
import "core:array" as array

fn example() : void {
	var values : array.Array[i32] = undefined;
	values.init();
	values.push(42);
}
```

### Field access

```cpp
position.x
position.y = 4;
```

Left side must be a struct value, and field must exist.

### Struct literals

Positional literals:

```cpp
import "core:vec3"

var a : Vec3 = { 1, 2, 3 };
const b = Vec3 { 4, 5, 6 };
```

- `{ ... }` uses expected type from context
- `Type { ... }` sets type explicitly
- field count and field types must match declaration

Named-field struct literals are not implemented.

## Compile-time introspection

Types are compile-time values (see [Comptime](#comptime)). Introspection lets compile-time code ask what a type *is* and enumerate its structure, so a single function template can handle every type in the language.

The motivating example is a generic `print`:

```cpp
import "core:io" as io // extern write_bytes, write_i64, write_u64, write_f64, write_bool

fn print(v : $T) : void {
	match T::kind {
		case .Bool:                          io.write_bool(v);
		case .F32, .F64:                     io.write_f64(v as f64);
		case .I8, .I16, .I32, .I64, .ISize:  io.write_i64(v as i64);
		case .U8, .U16, .U32, .U64:          io.write_u64(v as u64); // no .Byte, no .CStr: see below
		case .String:                        io.write_bytes(v);

		case .Nullable:
			if v != null {
				print(v); // v is promoted; the recursive call instantiates at the inner type
			} else {
				io.write_bytes("null");
			}

		case .Slice, .Array:
			io.write_bytes("[");
			for i in 0..length(v) {
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
			unroll for i, f in v {
				if i > 0 { io.write_bytes(", "); }
				io.write_bytes(f.name);
				io.write_bytes(" = ");
				print(f.value);
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

`.Byte` and `.CStr` also fall through, but for a narrower reason: both *are* ordinary runtime values with printable contents, blocked only by a missing cast. [Casts](#casts) lists no conversion between `byte` and an integer type, nor between `cstr` and `string`, so `v as u64` and `v as string` would not compile in those arms. Printing either one's contents requires a cast the language does not currently have.

### typeof

`typeof` is a compile-time operator that takes an expression and produces its concrete type as a compile-time `type` value:

```cpp
comptime A = typeof(1 + 2);      // i32
comptime B = typeof(v[0]);       // the element type of v
comptime C = typeof(make_vec3()); // the return type of the call, Vec3
```

Rules:

- the operand is an expression, not a type - the mirror of [`sizeof` and `alignof`](#sizeof-and-alignof), which take a type. `sizeof(typeof(e))` is the size of `e`'s type. Reflection that starts from a *type* rather than an expression uses [type members](#type-members) instead
- like `sizeof` and `alignof`, `typeof` is an operator resolved by the compiler, not a function: it cannot be bound to a name, passed as an argument, used as a function value, or reached through [UFCS](#ufcs); only its result is a value. The rule that functions cannot return `type` (see [Comptime](#comptime)) constrains declared functions and does not apply to it
- the operand is type-checked but **not evaluated**, and no code is generated for it. `typeof(v[0])` on an empty slice is valid and yields the element type
- it produces a `type` value, usable wherever a compile-time type is required (template arguments, variable type positions, `comptime` bindings, `==` comparison)
- `typeof` observes flow typing: after `if v != null`, `typeof(v)` inside the branch is the promoted type `U`, not `?U`. The same applies inside an `is` or `match` arm on a tagged union
- the result is compile-time only and never materializes into runtime code (see [Comptime-to-runtime materialization](#comptime-to-runtime-materialization))

### Type members

A `type` value exposes its structure through members accessed with `::` (see [Why `::` and not `.`](#tname) below). Unlike [`typeof`](#typeof), which takes an *expression*, these start from a *type* - typically a `$T` parameter, a `comptime type` parameter, or any `typeof` result:

| member | result | valid for |
| --- | --- | --- |
| `t::kind` | `TypeKind` | every type |
| `t::name` | `string` | every type |
| `t::child` | `type` | `.Nullable`, `.Slice`, `.Array` |
| `t::length` | comptime integer | `.Array` |
| `t::fields` | sequence of field cursors | `.Struct` |
| `t::values` | sequence of enum cursors | `.Enum` |
| `t::types` | sequence of member types | `.Union` |
| `t::params` | sequence of parameter types | `.Fn` |
| `t::ret` | `type` | `.Fn` |

```cpp
comptime k = i32::kind;       // .I32
comptime s = Vec3::kind;      // .Struct
comptime n = Vec3::name;      // "Vec3"
comptime e = ([]i32)::child;  // i32
comptime m = ([4]i32)::length; // 4
comptime ps = (fn(i32, f32) : bool)::params; // []type: [i32, f32]
comptime r = (fn(i32, f32) : bool)::ret;     // bool
```

- `t::kind` classifies the type into one [`TypeKind`](#typekind) discriminant
- `t::name` is the type's source-level name, the same string in the table under [`t::name`](#tname) below
- `t::child` is the single operand of a one-operand type constructor: the `U` of `?U`, the element of `[]U`, or the element of `[N]U`
- `t::length` is the element count `N` of a `[N]T`, an untyped compile-time integer - the same value `length(v)` yields on an instance (see [Static-sized arrays](#static-sized-arrays)), but reachable from the type without one, so `unroll for i in 0..t::length` works on a type alone. It is not defined for `.Slice`, whose length is a runtime property
- `t::fields`, `t::values`, and `t::types` are [reflection sequences](#reflection-sequences)
- `t::params` is the [reflection sequence](#reflection-sequences) of a `.Fn` type's parameter types, in declaration order; `t::ret` is its return type, named `ret` rather than `return` to avoid the keyword. Function type syntax carries no parameter names (`fn(i32, i32) : i32`), so `t::params` is a plain `[]type` - there is no parameter cursor to bundle a name with, the same reasoning that makes [union iteration](#union-iteration)'s `t::types` a plain `[]type` rather than a cursor sequence

All type members are compile-time only. `t` must be a concrete compile-time type; a `$T` that has not been instantiated yet is a compile-time error. Type members are not operators or functions - like any member access they cannot be taken as a value on their own, only applied to a type.

**Kind-specific members are guarded.** `t::child`, `t::length`, `t::fields`, `t::values`, `t::types`, `t::params`, and `t::ret` exist only for some kinds, and each is a compile-time error unless `t`'s kind is statically known to admit it. A manifest type - a type literal such as `[]i32` or a concrete `Vec3` - carries its kind by construction, so `([]i32)::child` needs no branch. The guard matters for a type of *unknown* kind, such as a `$T` parameter: there the kind must first be established, normally by an arm of a `match t::kind` or the taken side of an `if t::kind == ...`:

```cpp
match t::kind {
	case .Struct:   unroll for f in t::fields { ... } // ok: kind proven
	case .Nullable: foo(t::child);                    // ok: kind proven
	case: ...
}

foo(t::child);   // error: t::child is not valid unless t is proven .Nullable/.Slice/.Array
```

This is the same [compile-time branch](#compile-time-branches) pruning used everywhere else: the arm that reads `t::fields` is checked only when it is selected, and it is selected only for a struct. Unlike the value-side `unroll for` forms, the guard here is *enforced* - a bare `t::fields` outside a kind-proving branch is reported, not merely error-prone.

#### `t::name`

`t::name` returns the source-level name of `t`:

| type | `t::name` |
| --- | --- |
| builtins | `"i32"`, `"f64"`, `"string"` |
| structs and enums | the declaration name, `"Vec3"`, `"State"` |
| template instantiations | `"Pair[i32]"`, `"Optional[Vec3]"` |
| nullable | `"?Vec3"` |
| slices and arrays | `"[]i32"`, `"[4]i32"` |
| unions | `"A \| B \| C"` in canonical member order |
| function types | `"fn(i32, i32) : i32"` |

`t::kind` and `t::name` are the only members every type has; a type's constituent parts are reached through the [reflection sequences](#reflection-sequences), which exist only for structs, enums, and unions.

**Why `::` and not `.`.** Reflection members are reached with `::`, which puts them in a namespace disjoint from user-declared names. Enum member access uses `Type.member`, so on `.` a reflection member and an enum member would compete for the same spelling; with `::` they cannot. An `enum` may declare a member named `kind`, `name`, or `values`, and both readings stay available: `State.values` is the declared member, `State::values` is the reflection sequence.

The decisive benefit is extensibility. Reserving `kind`, `name`, `child`, `length`, `fields`, `values`, and `types` as enum member names would work today, but it would make *adding* a reflection member in a future version a breaking change for every enum that already declares one. A disjoint namespace has no such cost.

Cursor members stay on `.` (`f.name`, `f.type`, `f.value`, `e.value`). Cursors are compiler-synthesized values with no user-declarable members, so nothing can collide with them and the `::` namespace buys nothing. Struct fields likewise stay on `.`: a struct type has no `Type.member` access at all, so a field named `fields` is reached as `v.fields` on a value and never competes with `S::fields`.

### TypeKind

`TypeKind` is a built-in enum type, not a declaration in a module. Like `i32` or `string` it is always in scope, needs no import, and cannot be shadowed. Its definition is fixed by the language and shown here only for reference:

```cpp
enum TypeKind {
	Bool,
	I8, I16, I32, I64, ISize,
	U8, U16, U32, U64, Byte,
	F32, F64,
	String,
	CStr,
	CPtr,
	Void,
	Type,      // the `type` type itself
	Nullable,  // ?T
	Slice,     // []T
	Array,     // [N]T
	Enum,
	Struct,
	Union,     // A | B
	Fn         // fn(...) : R
}
```

- the enum is exhaustive: every type has exactly one discriminant.
- an instantiated struct template such as `Pair[i32]` is a `.Struct`
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

`t::fields`, `t::values`, `t::types`, and `t::params` produce a struct's fields, an enum's members, a union's member types, or a `.Fn` type's parameter types as compile-time sequences. Each is a `comptime` slice whose element type is compile-time only:

| member | element |
| --- | --- |
| `t::fields` | a *field cursor*: `.name`, `.type` |
| `t::values` | an *enum cursor*: `.name`, `.value` |
| `t::types` | a `type` |
| `t::params` | a `type` |

As comptime slices they are first-class: they can be bound, measured, indexed, and iterated, all at compile time.

```cpp
comptime fs    = t::fields;    // element type inferred; see below
comptime n     = length(fs);  // the field count
comptime first = fs[0];       // a single field cursor
unroll for f in fs { ... }    // re-iterate a bound sequence
```

- **binding requires inference.** The cursor element types are built in and not nameable in source, so `comptime fs = t::fields` is legal but `comptime fs : []FieldCursor = t::fields` is not - there is no such spelling. `t::types` and `t::params` are the exception: their element type `type` *is* nameable, so `comptime ts : []type = t::types` or `comptime ps : []type = t::params` may carry the annotation
- `length`, indexing, and `unroll for` work as on any comptime slice
- element types are compile-time only, so none of these sequences [materialize](#comptime-to-runtime-materialization), and a runtime `for` over one is a compile-time error
- once bound, the value is an ordinary comptime slice with no residual tie to `t`: it can be carried out of the branch that produced it and used anywhere, because the kind was proven at the point the sequence was obtained, not at the point it is used

**Field cursors carry no `.value`.** A field cursor from `t::fields` has `.name` and `.type` only. It cannot carry `.value`, because a field's value has a different type for every field while a slice has one element type. The mutable, per-field `.value` exists only in the [value form](#field-iteration) of field iteration - `unroll for f in v` over a struct *value* - where each unrolled copy binds one cursor of a single known type. That cursor can never escape into a binding, so value-side field access is loop-only; there is no `comptime fs = v.fields`.

### Unroll for

`unroll for` duplicates its body at compile time, once per iteration, binding the loop variable to a different compile-time *value* in each copy. Each copy is then type-checked separately - which is what allows a loop over a struct's fields to touch a differently typed field every time, even though the loop variable is a field cursor in all of them.

```cpp
unroll for i in 0..N { ... }             // range form, N comptime-known
unroll for x in seq { ... }              // sequence form
unroll for i, x in seq { ... }           // sequence form with index
```

- the range form requires both bounds to be compile-time integer constants; `length(arr)` on a `[N]T` is one, so `unroll for i in 0..length(arr)` unrolls a static array while the same loop over a slice must use a runtime `for`
- the sequence form requires a comptime slice - which includes the [reflection sequences](#reflection-sequences) `t::fields`, `t::values`, `t::types` and any binding of one - or a struct **value** operand for [value-side field iteration](#field-iteration) (see [What an unrolled loop binds](#what-an-unrolled-loop-binds)). A bare struct, enum, or union *type* is not iterable; iterate its reflection sequence instead
- the loop variable itself is a compile-time binding and cannot be reassigned in the body; this constrains the *cursor*, not the storage it denotes, so a field cursor's `f.value = x` still writes through to the field (see [Field iteration](#field-iteration))
- because it is compile-time, expressions derived from it are resolved per copy: `f.value` has that field's type, `v is M` tests that member, and the index in `unroll for i, x in seq` is a constant, so `if i > 0 { ... }` is decided at compile time
- a comptime slice may also be iterated with a runtime `for` when its element type has a runtime representation, in which case the loop variable is an ordinary runtime value and none of the above applies

Control flow inside an unrolled body:

- `return` returns from the enclosing function
- `break` transfers past the last copy; `continue` transfers to the next copy
- labeled `break` / `continue` work normally, including labels on an enclosing `unroll for`

`unroll for` is a compile-time *duplication* construct, not a compile-time *evaluation* construct: the copies are ordinary code. In particular `break` and `continue` emit real runtime branches out of the unrolled sequence, so an unrolled loop is not guaranteed to be free of control flow.

### Field iteration

A struct's fields are iterated in declaration order in two forms:

- **type form** - `unroll for f in S::fields` over the [reflection sequence](#reflection-sequences) of a struct **type**. Cursors carry `.name` and `.type`
- **value form** - `unroll for f in v` over a struct **value**. Cursors additionally carry `.value`

```cpp
struct S {
	i : i32;
	f : f32;
}

var v : S = undefined;

unroll for i, f in v {         // value form
	if f.type == i32 {         // compile-time branch: the f32 copy is pruned
		io.write_bytes(f.name);
		f.value = 42;          // writes through to v.i
	}
}
```

The loop variable is a *field cursor* with at most three members:

- `f.name` - the field's declared name, a compile-time `string`
- `f.type` - the field's type as a compile-time `type` value, equivalent to `typeof(f.value)`
- `f.value` - **value form only** - the field itself. This is not a copy: it is the ordinary field access `v.i` under another spelling, so `f.value = x` is legal exactly when `v.i = x` would be (mutable when `v` is a `var`, rejected on a `const` or a [temporary](#temporaries), usable as a `ref` argument), and it has that field's exact type in each unrolled copy

Rules:

- the type form (`S::fields`) yields cursors with `.name` and `.type`; naming `.value` on one is a compile-time error, because a type has no storage to bind. It is an ordinary [reflection sequence](#reflection-sequences), so it can also be bound, counted with `length`, and indexed
- the value form (`v`) additionally yields `.value`, but is loop-only: it is not a slice and cannot be bound (see [Reflection sequences](#reflection-sequences))
- both forms take the optional index binding, with the same meaning it has over any other sequence:

	```cpp
	unroll for f in v { ... }        // fields of the value, no index
	unroll for f in S::fields { ... } // fields of the type: no f.value
	unroll for i, f in v { ... }     // index available for separators
	```

- the operand is evaluated in the enclosing scope, so a binding that shadows it (`unroll for v in v`) is legal but makes the operand unreachable inside the body
- the cursor type is built in and not nameable in source; cursors exist only as loop bindings and comptime values derived from them
- an ordinary runtime `for` over a struct value is a compile-time error: each field has a different type, so there is no single type for a runtime loop variable to have
- the number of fields is `length(S::fields)`; the `i > 0` idiom covers separators without needing it

The assignment in the example type-checks only because [type equality](#type-equality) is compile-time known, so the copy generated for the `f32` field never checks its body. The same loop without the `if` would be an error on the first field whose type rejects `42`.

Because the cursor binds the field directly, there is no projection operator and no field-descriptor type: a field's type name is `f.type::name`, and its value is `f.value`.

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

The loop variable is an *enum cursor* with two members:

- `e.name` - the member's declared name, a compile-time `string`
- `e.value` - the member itself, typed as the enum. Its discriminant is `e.value as i32`, following the ordinary [enum-to-integer cast](#casts) rules

Rules:

- the operand is an enum type's `::values`; an enum *value* is not iterable, since it denotes one member rather than the set of them
- as a reflection sequence it can be bound, counted with `length`, and indexed, so a name table *can* be built: `State::values[i].name`
- the optional index binding works as it does everywhere else: `unroll for i, e in State::values { ... }`
- the cursor type is built in and not nameable in source
- an ordinary runtime `for` over `State::values` is a compile-time error: the cursor is compile-time only

`e.name` and `e.value as i32` are still ordinary compile-time constants in each copy, so they [materialize](#comptime-to-runtime-materialization) into runtime code exactly like any other comptime string or integer.

### Union iteration

`T::types` is the [reflection sequence](#reflection-sequences) of a tagged union **type**; iterating it visits the union's member types in the union's canonical order (deterministic but implementation-defined, see [Tagged unions](#tagged-unions)).

```cpp
unroll for M in T::types {
	if v is M { print(v); return; }
}
```

The loop variable is a compile-time `type` value, not a cursor. A struct field and an enum member each bundle a name with something else, so they need one; a union member *is* a type, and its name is `M::name`, so there is nothing to bundle. `T::types` is therefore a plain `[]type` - the same reasoning applies to a `.Fn` type's [`t::params`](#type-members), whose elements are unnamed parameter types with nothing to bundle either (see [Function type introspection](#function-type-introspection)).

Rules:

- the operand is a union type's `::types`; a union *value* is not iterable, since it holds one member at a time - use [`match`](#tagged-unions) or `is` on the value
- `M` is usable anywhere a compile-time type is: `v is M`, `v as M`, a variable's declared type, a template argument
- as a reflection sequence, `T::types` can be bound (`comptime ts : []type = T::types`), counted with `length`, and indexed
- the `is` test and the promotion it performs are the ordinary union rules; unrolling emits one test per member
- the optional index binding works as it does everywhere else
- an ordinary runtime `for` over `T::types` is a compile-time error: `type` values have no runtime representation

### Function type introspection

`t::params` and `t::ret` decompose a `.Fn` type into its parameter types and return type, completing the type-member table for the one kind that previously exposed only `t::kind` and `t::name`.

```cpp
comptime Handler = fn(i32, f32) : bool;

comptime ps = Handler::params; // []type: [i32, f32]
comptime r  = Handler::ret;    // bool

unroll for P in ps {
	io.write_bytes(P::name);
	io.write_bytes(" ");
}
```

- `t::params` is the [reflection sequence](#reflection-sequences) of parameter types, in declaration order; `t::ret` is a single `type`, following the same shape as [`t::child`](#type-members) for other one-operand-or-fewer constructors
- function type syntax (`fn(i32, i32) : i32`) never names its parameters, so there is nothing for a parameter cursor to bundle a name with - `t::params` is a plain `[]type`, like [`t::types`](#union-iteration) for unions
- `t::ret` may be `void`; unlike a value's static type, which can never be `void` (see [`TypeKind`](#typekind)), a function type is free to name `void` as its return type, and `t::ret` observes it directly without going through `typeof` on a call
- both are guarded like any [kind-specific member](#type-members): valid only once `t`'s kind is proven `.Fn`
- neither reaches into a parameter's or return type's own structure automatically; a `.Struct` element of `t::params` is introspected by recursing, the same as any other `type` value: `t::params[0]::fields`

Rules:

- `t::params` can be bound (`comptime ps : []type = Handler::params`), counted with `length`, indexed, and unrolled, like any reflection sequence with a nameable element type
- an ordinary runtime `for` over `t::params` is a compile-time error: `type` values have no runtime representation
- `t::ret` is compile-time only, like `t::child` and every other member that produces a `type`

### What an unrolled loop binds

The iteration forms differ in what the loop variable is, decided by the operand:

| operand | binding | members |
| --- | --- | --- |
| comptime slice | its element | whatever the element type has |
| `t::fields` (struct type) | field cursor | `.name`, `.type` |
| `t::values` (enum type) | enum cursor | `.name`, `.value` |
| `t::types` (union type) | a `type` | - |
| `t::params` (function type) | a `type` | - |
| struct value | field cursor | `.name`, `.type`, `.value` |

The reflection sequences are themselves comptime slices, so they are subcases of the first row; they are listed separately because their element types are built in and not otherwise nameable. The struct **value** is the only non-slice operand, and the only one that binds `.value`. Enum and union *values* are not iterable at all - iterate the type's `::values` or `::types` instead; likewise a function *value*'s parameter and return types are reached through its type, `typeof(f)::params`, not through `f` directly.

### Comptime-to-runtime materialization

Introspection results are compile-time values. Some of them can cross into runtime code as constants.

Materializes into a runtime constant:

- comptime `string` values, such as `t::name`, `f.name`, and `e.name`, become string constants
- comptime integers, such as `sizeof`, `alignof`, `t::length`, and `e.value as i32`, become integer constants
- an enum cursor's `e.value` becomes an ordinary enum constant

These may be passed to functions including `extern fn`, assigned to `var` and `const`, and used in any runtime expression.

Stays compile-time only:

- `type` values, including `typeof(...)`, `t::child`, `t::ret`, `f.type`, and a union or function-parameter iteration's binding (`M` or `P`)
- `TypeKind` values, including `t::kind`
- field and enum cursors, because a cursor carries a `type` and, for a field, a binding whose type differs per unrolled copy
- any slice whose element type is compile-time only, which is every [reflection sequence](#reflection-sequences): `t::fields`, `t::values`, `t::types`, and `t::params` (each of the latter two a `[]type`)

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
demo.lum: line 12, column 2: instantiation depth limit exceeded
  in f[[][]i32]
  in f[[]i32]
  in f[i32]
```

## Runtime model

Current runtime executes compiled bytecode through the public `ls_runtime` API.

- calls create call frames
- blocks create nested local scopes
- struct values store fields in declaration order
- function values reference existing script or native functions
- bytecode functions consume arguments from the runtime stack

Example C++ shape:

```cpp
ls_arena compile_arena;
ls_default_arena_create(&compile_arena);
ls_host compile_host = {compile_arena};
ls_module* module = ls_module_create(&compile_host);
ls_module_compile(module, source, source_name, nullptr, nullptr);

ls_bytecode* bytecode = ls_bytecode_compile(module, &compile_host);
ls_arena runtime_arena;
ls_default_arena_create(&runtime_arena);
ls_host runtime_host = {runtime_arena};
ls_runtime* runtime = bytecode ? ls_runtime_create(bytecode, &runtime_host) : nullptr;
if (runtime) {
	ls_string_view main_name = { "main", "main" + 4 };
	ls_call(runtime, main_name);
	if (ls_bytecode_runtime_result_kind(runtime, main_name) != LS_TYPE_VOID) {
		i32 result = ls_to_i32(runtime, -1);
	}
}
```

### Native functions

Declare native functions with `extern fn` in the script. After compiling the
module, find the declaration in its unit and bind its unit-local index to the
runtime callback:

```cpp
static void native_add(ls_runtime* runtime, ls_call_frame frame) {
	LS_ARG(frame, i32, a);
	LS_ARG(frame, i32, b);
	LS_RESULT(frame, a + b);
}

ls_module* module = ls_module_create(&host);
if (ls_module_compile(module, source, source_name, nullptr, nullptr) == LS_RESULT_OK) {
	ls_bytecode* bytecode = ls_bytecode_compile(module, &host);
	ls_runtime* runtime = bytecode ? ls_runtime_create(bytecode, &host) : nullptr;
	ls_unit* unit = ls_module_get_unit(module, 0);
	if (runtime && unit && ls_unit_get_native_function_count(unit) == 1) {
		ls_runtime_set_native_function_callback(runtime, unit, 0, &native_add);
	}
}
ls_runtime_destroy(runtime);
ls_bytecode_destroy(bytecode);
ls_module_destroy(module);
ls_default_arena_destroy(&runtime_arena);
ls_default_arena_destroy(&compile_arena);
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

### Extern declarations

```cpp
extern fn native_add(a : i32, b : i32) : i32;
```

`extern` declarations inform the compiler about a function's name and signature but do not provide an implementation. Each declaration is enumerated by `ls_unit_get_native_function_count` and `ls_unit_get_native_function_name`; bind it with `ls_runtime_set_native_function_callback` using the corresponding unit-local index. Use `ls_unit_get_path` to identify declarations from imported units.


## Diagnostic

- compilation currently stops after first reported error
- parser/checker/runtime diagnostics include source, line, and column when the source name is known
- imported source files report the import path, for example `core:vec3`

Examples:

```txt
maps/demo/demo.lum: line 50, column 4: Unexpected token near '_'
core:vec3: line 28, column 14: Arithmetic operands must have the same type
```

## Known underspecified areas

- `IndexingRequiresArrayTypeFails`
- `BytecodeGlobalInitializationOrder`
- `DeferCanNotWrapReturn`
- `NestedFunctionCanNotCaptureOuterLocal`
- `DuplicateDeclarationsFail`
- `ConstCanNotBeUndefined`
- `DuplicateUnaliasedImportFails`
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
- `StringIsReservedKeyword`
- `match` needs tighter rules for what counts as a valid pattern expression, how string subjects behave, and the exact duplicate/exhaustiveness policy for non-enum subjects.
- `for` ranges should define whether bounds must match exactly, what type the loop variable has, and what happens for descending or overflowing ranges.
- Static-sized arrays still need complete rules for copy semantics, passing/returning by value, and comparison behavior. Nesting reads left-to-right: `[4][8]i32` is an array of 4 arrays of 8 ints; `[][4]i32` is a slice of arrays of 4 ints.
- Nullable promotion should define `else if`, compound conditions, and scope boundaries in more detail.
- `defer` should define behavior on `break`, `continue`, runtime errors, and nested scopes, not only normal exit and `return`.
- Imports and `extern` bindings still need explicit collision policy for same-path/same-alias cases, builtin module boundaries, and imported declaration conflicts.
- Function values need clearer rules for equality/identity interactions with function declarations and literals.
- `unroll for` accepts a struct **value** as an operand, which a runtime `for` rejects, and that operand alone binds a cursor with `.value`. The loop variable's shape (element, field cursor, enum cursor, or `type`) depends on the operand rather than on the loop syntax; see [What an unrolled loop binds](#what-an-unrolled-loop-binds).
- Type member and cursor names (`t::name`, `f.name`, `e.name`) are unqualified declaration names. Two modules that both declare `Vec3` produce the same `t::name`, and a generic `print` cannot distinguish them; whether these should be module-qualified is unresolved, and interacts with the import collision policy noted above.
- `length` is both a builtin over arrays, slices, and [reflection sequences](#reflection-sequences), and an ordinary function name that core modules define on structs, such as `length(v)` for a `Vec3` magnitude in [Imports](#imports). Since [overloading is not supported](#functions), the rule for which one a call selects, and whether a user declaration may take the name at all, is unspecified.
