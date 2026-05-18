# This is in early prototype/exploration stage, everything can change

# TODO

* engine API

---

* operators
* for cycle
* debugger
* string interpolation
* bytecode/vm
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
- [Engine modules](#engine-modules)
	- [Engine log](#engine-log)
	- [Engine integration model](#engine-integration-model)
	- [Entity and world API shape](#entity-and-world-api-shape)
	- [Input API shape](#input-api-shape)
- [Editor and diagnostics](#editor-and-diagnostics)
- [Known limitations and pending spec decisions](#known-limitations-and-pending-spec-decisions)

Current implementation includes:

- parser
- type checker
- AST interpreter runtime
- `.lum` asset registration
- basic Studio editor integration

Bytecode and JIT are intentionally out of scope for the first version.

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

Basic import:

```cpp
import "math"
```

Import with alias:

```cpp
import "math" as math
```

`core:` imports resolve under `data/scripts/core/`. The `.lum` suffix is optional:

```cpp
import "core:math"
import "core:collections/list" as list
```

`engine:` imports resolve to built-in engine modules, not project files:

```cpp
import "engine:entity" as entity
import "engine:animator" as animator
import "engine:world" as world
```

Without alias, imported declarations are added to the current module scope:

```cpp
import "math"
import "core:vec3"

fn main() : f32 {
	const v : Vec3 = Vec3 { 1, 2, 3 };
	return length(v);
}
```

With alias, declarations are accessed via namespace:

```cpp
import "core:vec3" as vec

fn main() : f32 {
	const v : vec.Vec3 = vec.Vec3 { 1, 2, 3 };
	return v.x;
}
```

Import rules:

- resolution is deterministic and follows import order
- duplicate import of the same path and alias is a no-op
- `core:` duplicate checks normalize `.lum` suffix
- alias collisions are compile-time errors
- import cycles are compile-time errors

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
};
```

Rules:

- field names must be unique within the struct
- field types can be primitive, enum, function type, or previously declared struct

### Enums

```cpp
enum State {
	Idle,
	Running,
	Paused,
	Done
};
```

Explicit values are allowed:

```cpp
enum Priority {
	Low = 0,
	Medium = 5,
	High = 10
};
```

Enums are strongly typed:

- no implicit conversion between enums and integers
- use explicit `as` casts when needed

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
- top-level functions are globally visible in the module
- overloading is not supported
- parameters are immutable

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

`Vec3` and `Quat` are core value types used heavily by engine APIs:

```cpp
import "core:vec3"
import "core:quat"

const position = Vec3 { 1.0, 2.0, 3.0 };
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

Nested functions are supported and scoped to their containing block. They do not capture outer locals or parameters.

```cpp
fn main() : i32 {
	fn add(a : i32, b : i32) : i32 {
		return a + b;
	}

	return add(20, 22);
}
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

Status: syntax and behavior are specified, but not implemented in parser/checker/runtime yet.

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
- constant zero divisor is compile-time error
- runtime zero divisor is runtime error

Floating-point division follows IEEE-754 and may produce `Inf` or `NaN`.

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

Current runtime is a tree-walk interpreter over checked AST.

- calls create call frames
- blocks create nested local scopes
- struct values store fields in declaration order
- function values reference existing script or native functions
- execution is limited by `RuntimeOptions::max_steps`

Example C++ shape:

```cpp
LumScript::Module module(allocator);
LumScript::Diagnostics diagnostics(allocator);

if (LumScript::compile(module, source, diagnostics)) {
	LumScript::Runtime runtime(module, allocator);
	LumScript::Value result;
	runtime.call("main", Span<const LumScript::Value>(), &result, diagnostics);
}
```

### Native functions

Register native functions after parsing and before type checking:

```cpp
static bool native_add(Span<const LumScript::Value> args, LumScript::Value* result, void*) {
	*result = LumScript::Runtime::makeI32(args[0].i + args[1].i);
	return true;
}

LumScript::Module module(allocator);
LumScript::Diagnostics diagnostics(allocator);

if (LumScript::parse(module, source, diagnostics)) {
	LumScript::TypeRef params[] = {
		LumScript::TypeRef(LumScript::TypeRef::I32),
		LumScript::TypeRef(LumScript::TypeRef::I32)
	};

	LumScript::addNativeFunction(
		module,
		"native_add",
		LumScript::TypeRef(LumScript::TypeRef::I32),
		Span<const LumScript::TypeRef>(params),
		&native_add
	);

	if (LumScript::typecheck(module, diagnostics)) {
		LumScript::Runtime runtime(module, allocator);
		LumScript::Value result;
		runtime.call("main", Span<const LumScript::Value>(), &result, diagnostics);
	}
}
```

Script usage:

```cpp
fn main() : i32 {
	return native_add(20, 22);
}
```

## Engine modules

### Engine log

```cpp
import "engine:log" as log

fn update(dt : f32) : void {
	log.logError("Hello " + "Lumix");
}
```

`log.logError(value : string) : void`

### Engine integration model

LumScript integrates as a world-level script. A world loads one `.lum` file that runs for that world's lifetime.

Lifecycle:

1. parse and type-check
2. call `init(world, input_system)` if present
3. call `update(dt)` each frame

```cpp
import "engine:world" as world
import "engine:input" as input

fn init(w : world.World, inputs : input.InputSystem) : void {
	// called once
}

fn update(dt : f32) : void {
	// called every frame
}
```

`world.World` and `input.InputSystem` are opaque handles provided by the engine.

### Entity and world API shape

```cpp
import "core:vec3"
import "core:quat"
import "engine:world" as world
import "engine:entity" as entity
import "engine:input" as input

fn init(w : world.World, inputs : input.InputSystem) : void {
	var e : entity.Entity = world.createEntity(w);
	const player = w.findByName("Player");
	e.setPosition({ 1.0, 2.0, 3.0 });
	e.setRotation({ 0.0, 0.0, 0.0, 1.0 });
	e.setScale({ 2.0, 2.0, 2.0 });

	if player != null {
		player.setPosition({ 0.0, 1.0, 0.0 });
	}

	if e.isValid() {
		e.destroy();
	}
}
```

Current world functions:

- `world.createEntity(w : world.World) : entity.Entity`
- `world.destroyEntity(w : world.World, e : entity.Entity) : void`
- `world.hasEntity(w : world.World, e : entity.Entity) : bool`
- `world.findByName(w : world.World, name : string) : ?entity.Entity`

Current entity functions:

- `entity.destroy(e : entity.Entity) : void`
- `entity.isValid(e : entity.Entity) : bool`
- `entity.setPosition(e : entity.Entity, position : Vec3) : void`
- `entity.getPosition(e : entity.Entity) : Vec3`
- `entity.setRotation(e : entity.Entity, rotation : Quat) : void`
- `entity.getRotation(e : entity.Entity) : Quat`
- `entity.setScale(e : entity.Entity, scale : Vec3) : void`
- `entity.getScale(e : entity.Entity) : Vec3`

Generated component accessors:

- `entity.animator(e : entity.Entity) : ?animator.Animator`
- `entity.<component_id>(e : entity.Entity) : ?<component_module>.<ComponentType>`

### Input API shape

The `engine:input` module currently exposes raw event iteration.

```cpp
import "engine:world" as world
import "engine:input" as input
import "engine:InputEventType"
import "engine:Keycode"
import "engine:log" as log

fn init(w : world.World, inputs : input.InputSystem) : void {
	var i : i32 = 0;
	const count = inputs.getEventCount();
	while i < count {
		const e = inputs.getEvent(i);
		const t = e.getType();

		if t == InputEventType.BUTTON {
			const key = e.getKeyId();
			if key == Keycode.W as i32 {
				log.logError("W");
			}
		}

		i += 1;
	}
}
```

Input functions:

- `input.getEventCount(inputs : input.InputSystem) : i32`
- `input.getEvent(inputs : input.InputSystem, index : i32) : input.InputEvent`
- `input.getType(e : input.InputEvent) : InputEventType`
- `input.getDeviceType(e : input.InputEvent) : i32`
- `input.getDeviceIndex(e : input.InputEvent) : i32`
- `input.getKeyId(e : input.InputEvent) : i32`
- `input.isDown(e : input.InputEvent) : bool`
- `input.isRepeat(e : input.InputEvent) : bool`
- `input.getX(e : input.InputEvent) : f32`
- `input.getY(e : input.InputEvent) : f32`
- `input.getValue(e : input.InputEvent) : f32`
- `input.getAxis(e : input.InputEvent) : i32`
- `input.getText(e : input.InputEvent) : i32`

Input event types are exposed through `InputEventType`:

- `InputEventType.BUTTON`
- `InputEventType.AXIS`
- `InputEventType.MOUSE_WHEEL`
- `InputEventType.TEXT_INPUT`
- `InputEventType.DEVICE_ADDED`
- `InputEventType.DEVICE_REMOVED`

Meta enums are imported by enum name, for example `import "engine:Keycode"`.

## Editor and diagnostics

Studio support:

- create/open `.lum` assets in asset browser
- syntax highlighting
- save/open/locate actions
- `Check` action that runs parser + checker on current text

Diagnostic behavior:

- compilation currently stops after first reported error
- parser/checker/runtime diagnostics include source, line, and column when the source name is known
- top-level world/editor scripts report the asset path, for example `maps/demo/demo.lum`
- imported source files report the import path, for example `core:vec3`
- engine imports do not have source text, so errors in unresolved `engine:` imports are reported at the import statement in the importing file

Examples:

```txt
maps/demo/demo.lum: line 50, column 4: Unexpected token near '_'
core:vec3: line 28, column 14: Arithmetic operands must have the same type
```

Common checker errors include duplicate declarations, unknown symbols, invalid assignment to `const`, type mismatches, and non-`bool` conditions.

## Known limitations and pending spec decisions

Not implemented yet:

- full `engine:` API coverage
- bytecode VM
- string interpolation
- maps
- break/continue and named loop labels
- lambdas and closures
- named struct fields in literals
- complete return-path analysis for all branches

Still being finalized:

- nullable flow typing details after reassignment
- `match` overlap rules for range/literal combinations
- global initialization order across imports
- string semantic guarantees (encoding/equality guarantees)
- migration from numeric input event helpers to enum-based event types
