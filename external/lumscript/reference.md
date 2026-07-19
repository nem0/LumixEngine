# This is in early prototype/exploration stage, everything can change

# TODO

* how can we push unions if we don't know the tag value of variants, i.e. U = A | B - we don't know if A's tag is 0 or 1
* use case - comptime string hash
* how should user implement print-s?
	fn print(v : varargs) : void {
		for x in v {
			match typeof(x) {
				case string: { print_string(x); }
				case i32: { print_i32(x); }
			}
		}
	}
* debugger
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
* comptime reflection/introspection?
* reflection?
* first-class types at compile time?
* gc?
* attributes?
* fibers/coroutines?
* closures?
* iterators/yield 
	fn each(a : arr) : yield i32 { ...
	for x in each(a) { ... }

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
	- [Strings](#strings)
	- [Function types](#function-types)
	- [Static-sized arrays](#static-sized-arrays)
	- [Slices](#slices)
- [Memory](#memory)
- [Variables](#variables)
- [Statements](#statements)
	- [Blocks](#blocks)
	- [Assignment](#assignment)
	- [If / else](#if--else)
	- [Match](#match)
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
	- [Ternary operator](#ternary-operator)
	- [Calls](#calls)
	- [Argument-dependent lookup](#argument-dependent-lookup)
	- [UFCS](#ufcs)
	- [Field access](#field-access)
	- [Struct literals](#struct-literals)
- [Runtime model](#runtime-model)
	- [Native functions](#native-functions)
- [Editor and diagnostics](#editor-and-diagnostics)
- [Known limitations and pending spec decisions](#known-limitations-and-pending-spec-decisions)

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
	- the member type is the tag; no named variants (Rust-style `enum` payloads) keeps the feature small — two variants with the same payload type use wrapper structs
		- var a : SomeUnion = SomeMember { 1, "foo" }; is possible and uses only existing language features
	- structural set semantics (order-insensitive, flattening) so anonymous unions like `Error | ASTNode` compose across modules and call layers
	- subset → superset widening is implicit so error unions propagate without manual re-wrapping; this leaves room for future propagation sugar (`try`-style)
	- `as` yields `?Member` instead of trapping, reusing the forced-null-check machinery instead of adding a runtime abort path
	- promotion in `match`/`is` is flow-typing with the same accepted unsoundness as nullable promotion — keeping the checker simple was preferred over a borrow-like aliasing rule
	- excluded from ADL/UFCS because a structural type has no declaring namespace
	- **open questions**: `if` statement lowering details, propagation sugar, canonical member order exposure

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
- primitive types such as `i32`, `f32`, `bool`, and `string` are built-in type values and cannot be shadowed
- using a runtime-only value where a compile-time value is required is a compile-time error
- a comptime call may call only compile-time-known function values
- functions cannot return `type`
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
	for i = 0..count {
		print(text);
	}
}

fn splat(value : f32, n : comptime i32) : [n]f32 {
	var result : [n]f32 = undefined;
	for i = 0..n {
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
- `and` and `or` remain built-in short-circuit operators and are not overloaded
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

A union type is written as members separated by `|`. A union value holds exactly one of its member types at a time; a tag records which one (the active variant). The member type itself is the tag — there are no named variants.

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
- `null` — `null` as a member is a compile-time error
- nullable types (`?T`); nullable union syntax (`?(A | B)`) is not supported
- function types — use nullable function types (`?fn(...)` ) or wrap in a struct instead
- unions (they flatten, see above)
- slices (`[]T`) — a slice is a view without ownership; the union tag does not distinguish which backing storage a slice view represents
- static arrays (`[N]T`) — use proper container types (structs, function-based interfaces) instead of unioning different array sizes

All members must be pairwise distinct types. Because the member type is the tag, two semantically different variants with the same payload type require wrapper structs.

**Coercion**

- a member value coerces implicitly to any union containing its type: `var e : InputEvent = b;`
- a union value coerces implicitly to any union whose member set is a superset (the tag is remapped at the coercion site): an `A | B` value can be assigned where `A | B | C` is expected — this is what lets error unions propagate across call layers
- no other implicit conversions apply; narrowing (superset to subset, or union to member) is never implicit

**Testing and extraction: `is` and `as`**

- `e is ButtonEvent` evaluates to `bool`: whether the active variant is `ButtonEvent`
- `if e is ButtonEvent { ... }` promotes `e` to `ButtonEvent` inside the branch, like nullable promotion in `if e != null`
- `e as ButtonEvent` evaluates to `?ButtonEvent`: the payload when the active variant matches, `null` otherwise; the usual forced null check applies before use
- there is no trapping variant cast
- `is` / `as` with a type that is not a member of the union is a compile-time error

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
- comma-separated alternatives (`case A, B:`) are allowed; the subject is not promoted in such a case since no single member type applies
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

Type rules:

- size must be a compile-time positive integer literal
- element type is fixed for all entries
- assignment requires exact same element type and size
- index expression must have an integer type
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
for i = 0..10 {
	log.logError(i);
}
```

The `a..b` range is exclusive on the upper bound. The range expressions are evaluated once before the loop starts. The loop variable is introduced by the `for` statement and is immutable inside the loop body.

Both bounds must have the same integer type, which becomes the type of the loop variable. An untyped bound adopts the other bound's concrete type (`for i = 0..length(s)` iterates with an `isize` loop variable); if both bounds are untyped they default to `i32`.

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

Returned expression must match function return type. Use explicit cast when needed.

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

Ordering comparisons (`<`, `<=`, `>`, `>=`) require numeric operands of the same type. Equality (`==`, `!=`) is defined for:

- numeric types, `bool`, `byte`, and enums — value comparison
- `string` — content comparison
- `cstr` and `cptr` — address comparison (two `cstr` values with equal content but different storage are not equal)
- function values — same function
- nullable values — only against the `null` literal

Arrays, slices, unions, and two nullable values have no built-in equality; comparing them is a compile-time error. Structs resolve `==` through `operator` declarations (see below).

If an operator is used with non-builtin value types, the compiler may resolve it to a matching `operator` declaration instead of a built-in primitive rule.
Primitive operands keep their built-in semantics and cannot be overridden by `operator` declarations.
`and` and `or` keep their built-in short-circuit semantics and are not candidates for operator declarations.
Compound assignment follows the same rule: a non-primitive left-hand target uses the corresponding binary operator, while a primitive left-hand target stays on the built-in path. In the latter case, the right-hand operand must be implicitly convertible to the left-hand target type; an expression such as `5 *= Vec2 { 1, 2 }` is therefore invalid.

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
- the operator has lower precedence than arithmetic and comparison operators, so `a > b ? 1 : 2` evaluates the comparison first
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
	e.destroy();       // calls entity.destroy — method syntax prefers the receiver type's unit
	destroy(e);        // calls local destroy — plain calls stay lexical, local is preferred over ADL
	entity.destroy(e); // calls entity.destroy — explicit namespace, always unambiguous
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
- Static-sized arrays are missing rules for literal syntax, copy semantics, passing/returning by value, and comparison behavior. Nesting now reads left-to-right: `[4][8]i32` is an array of 4 arrays of 8 ints; `[][4]i32` is a slice of arrays of 4 ints.
- Nullable promotion is only described for `if value != null`; the spec should say how `== null`, `else if`, compound conditions, and scope boundaries behave.
- `defer` should define behavior on `break`, `continue`, runtime errors, and nested scopes, not only normal exit and `return`.
- Boolean operator coverage is incomplete because `not` appears in examples but is not specified alongside `and` and `or`.
- Imports and `extern` bindings still need explicit collision policy for same-path/same-alias cases, builtin module boundaries, and imported declaration conflicts.
- Function values need clearer rules for equality/identity interactions with function declarations and literals.
