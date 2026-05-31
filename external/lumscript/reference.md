# This is in early prototype/exploration stage, everything can change

# TODO

* dynamic arrays/memory
* debugger
* string interpolation
* jit/llvm

---

* getter/setter?
* traits/interfaces?
* tagged unions?
* with/when/where?
* context object?
* multiple returns?
* compil-time stuff?
* reflection?
* first-class types at compile time?
* gc?
* attributes?
* fibers/coroutines?
* closures?
* generics?

# Goals
 * **simple** - string concatenation: `"Hello " + "World!"`. Avoid verbose low level code.
 * **safe**	- nullable values with forced null check to access
 * **efficient** - no unnecessary allocations, fast

# LumScript

LumScript is a small, statically typed scripting language for Lumix Engine.

## Table of contents

- [Design goals](#design-goals)
- [Quick example](#quick-example)
- [Source files](#source-files)
- [Declarations](#declarations)
	- [Imports](#imports)
	- [Structs](#structs)
	- [Enums](#enums)
	- [Functions](#functions)
	- [Operators](#operators)
	- [Ref parameters](#ref-parameters)
- [Types](#types)
	- [Untyped literals](#untyped-literals)
	- [Nullable values](#nullable-values)
	- [Strings](#strings)
	- [Function types](#function-types)
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
	- [Comparison and boolean operators](#comparison-and-boolean-operators)
	- [Calls](#calls)
	- [Namespace resolution by first parameter](#namespace-resolution-by-first-parameter)
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

A module contains top-level `import`, `struct`, `enum`, `fn`, and variable declarations.

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
- top-level `fn foo() : T { ... }` is syntax sugar for a module-level `const foo = fn() : T { ... }` declaration
- overloading is not supported
- parameters are immutable
- nested functions are not supported
- this does not include operator declarations; operators are a separate declaration form

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
- overload resolution uses exact type matching
- no implicit casts are performed to make an operator applicable
- imported modules participate in operator lookup
- if multiple declarations match equally well, the expression is ambiguous and is a compile-time error
- primitive built-in operator behavior still applies when no overload is involved
- compound assignment on a non-primitive type uses the corresponding binary operator, for example `x += y` behaves like `x = x + y`
- compound assignment evaluates the left-hand side once // TODO test
- primitive compound assignment keeps its built-in behavior and cannot be overridden

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
- `ref` parameter types cannot be nullable
- `ref` arguments cannot be nullable

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
- `f32`, `f64`
- `string`
- user-defined `struct` types
- user-defined `enum` types
- function types

Not implemented yet:

- maps, pointers, references as first-class types
- generics
- closures
- user-declared methods

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

Literals start untyped during checking:

- integer literals start as untyped integer
- decimal literals start as untyped float

Context (target type, argument type, return type, cast, expression expectation) concretizes them.

Defaults when context is insufficient:

- integer literals default to `i32`
- decimal literals default to `f32`

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

Declaration syntax for fixed-size arrays uses postfix size:

```cpp
var d : i32[16] = undefined;
```

Usage syntax:

```cpp
var d : i32[16] = undefined;
d[0] = 42;
const first : i32 = d[0];
```

Type rules:

- size must be a compile-time positive integer literal
- element type is fixed for all entries
- assignment requires exact same element type and size
- index expression must have an integer type

Indexing behavior:

- constant index out of range is a compile-time error
- variable index is allowed
- variable index is bounds-checked at runtime
- out-of-bounds access is a runtime error

Indexing examples:

```cpp
var d : i32[16] = undefined;

const idx = 3;
d[idx] = 11; // allowed

var i : i32 = 3;
d[i] = 12; // allowed, runtime bounds check

const bad = 99;
d[bad] = 1; // compile-time error (constant index out of range)
```

## Variables

```cpp
var counter : i32 = 0;
const step = 1;

fn tick() : i32 {
	counter += step;
	return counter;
}
```

Rules:

- top-level variables are module globals
- globals initialize once when runtime first runs the module
- locals use same declaration syntax
- all `var` and `const` declarations must have an initializer
- to intentionally skip initialization, use explicit `undefined` (for example `var x : i32 = undefined`)
- explicit type is optional if inference can resolve from initializer
- `const` cannot be reassigned
- variables are block scoped
- locals can shadow globals

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

Supported patterns:

- enum members
- scalar literals
- inclusive ranges (`a..b`)
- comma-separated alternatives
- `_` fallback

```cpp
match state {
	case .Idle:
		log.logError("idle");
	case .Running:
		log.logError("running");
		update_running_state();
	case .Paused:
		log.logError("paused");
	case _:
		log.logError("unknown");
}
```

```cpp
match score {
	case 0:
		log.logError("none");
	case 1..9:
		log.logError("low");
	case 10..99:
		log.logError("high");
	case _:
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

Enum matches must be exhaustive unless `_` is present. Duplicate enum cases are compile-time errors.

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
for i = 0..9 {
	log.logError(i);
}
```

The `a..b` range is inclusive. The range expressions are evaluated once before the loop starts. The loop variable is introduced by the `for` statement and is immutable inside the loop body.

If the lower bound is greater than the upper bound, the loop body does not execute.

This is equivalent to evaluating the bounds once, then iterating upward with an internal counter:

```cpp
const from = 0;
const to = 9;
var current = from;
while current <= to {
	const i = from;
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
x as bool
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

Enums can cast to integers, and integers can cast to enums:

```cpp
const numeric : i32 = State.Running as i32;
const state : State = numeric as State;
```

Integer-to-enum cast does not validate membership.

Struct casts are not supported.

No implicit casts occur in assignments, arguments, returns, struct fields, or binary arithmetic.

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

If an operator is used with non-builtin value types, the compiler may resolve it to a matching `operator` declaration instead of a built-in primitive rule.
Primitive operands keep their built-in semantics and cannot be overridden by `operator` declarations.
`and` and `or` keep their built-in short-circuit semantics and are not candidates for operator declarations.
Compound assignment follows the same rule: custom types use the corresponding binary operator, while primitive compound assignment stays built in.

### Calls

```cpp
const c = add(a, b);
```

Calls are statically checked for:

- function existence
- argument count
- argument types

If callee expression is a function value, call is indirect.

### Namespace resolution by first parameter

Method-style syntax is syntactic sugar for namespaced function calls.

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

Resolution order:

- first try written callee directly
- if unresolved and callee is field-call syntax, try first-parameter namespace rewrite

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
ls_module* module = ls_module_create(&host);
ls_module_compile(module, source, source_name, &host, nullptr, nullptr);

ls_bytecode* bytecode = ls_bytecode_compile(module, &host);
ls_runtime* runtime = bytecode ? ls_runtime_create(bytecode) : nullptr;
if (runtime) {
	ls_string_view main_name = { "main", "main" + 4 };
	ls_call(runtime, main_name);
	if (ls_bytecode_runtime_result_kind(runtime, main_name) != LS_TYPE_VOID) {
		i32 result = ls_to_i32(runtime, -1);
	}
}
```

### Native functions

Register native functions after parsing and before type checking:

```cpp
static bool native_add(ls_runtime* runtime, size_t arg_count, size_t result_count, void*) {
	if (arg_count < 2 || result_count < 1) return false;
	ls_push_i32(runtime, ls_to_i32(runtime, -2) + ls_to_i32(runtime, -1));
	return true;
}

ls_module* module = ls_module_create(&host);
if (ls_module_parse(module, source, source_name, &host)) {
	ls_type params[] = {
		ls_type_make(LS_TYPE_I32),
		ls_type_make(LS_TYPE_I32)
	};

	const int native_add_index = ls_module_add_native_function(
		module,
		"native_add",
		ls_type_make(LS_TYPE_I32),
		params,
		2
	);

	ls_bytecode* bytecode = ls_bytecode_compile(module, &host);
	ls_runtime* runtime = ls_runtime_create(bytecode);
	ls_runtime_set_native_function_callback(runtime, native_add_index, &native_add, nullptr);
}
```

Script usage:

```cpp
fn main() : i32 {
	return native_add(20, 22);
}
```

### Extern declarations

`extern fn foo() : T;` is syntax sugar for a module-level `var foo = fn() : T = undefined;` declaration without a body.

```cpp
extern fn native_add(a : i32, b : i32) : i32;
```

`extern` declarations inform the compiler about a function's name and signature but do not provide an implementation. The host must register and bind a native function with the same qualified name (using `ls_runtime_set_native_function_callback`) before calling into script.


## Diagnostic

- compilation currently stops after first reported error
- parser/checker/runtime diagnostics include source, line, and column when the source name is known
- imported source files report the import path, for example `core:vec3`

Examples:

```txt
maps/demo/demo.lum: line 50, column 4: Unexpected token near '_'
core:vec3: line 28, column 14: Arithmetic operands must have the same type
```
