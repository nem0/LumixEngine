# This is in early prototype/exploration stage, everything can change

# TODO

* engine API
* arrays
* first-class functions

---

* for cycle
* break/continue/named label
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

LumScript is a small statically typed scripting language being built for Lumix Engine. The current implementation has a parser, checker, tree-walk runtime, `.lum` asset registration, and a basic Studio code editor. Bytecode is intentionally not part of the first version.

This page describes the language as implemented today.

## Table of Contents

- Basics
	- [A Small Program](#a-small-program)
	- [Source Files](#source-files)

- Language
	- [Declarations](#declarations)
		- [Imports](#imports)
		- [Structs](#structs)
		- [Enums](#enums)
		- [Functions](#functions)
		- [Ref Parameters](#ref-parameters)
	- [Types](#types)
		- [Untyped Values](#untyped-values)
		- [Nullable Values](#nullable-values)
		- [Strings](#strings)
	- [Variables](#variables)
	- [Statements](#statements)
		- [Blocks](#blocks)
		- [Assignment](#assignment)
		- [If And Else](#if-and-else)
		- [Match](#match)
		- [While](#while)
		- [Defer](#defer)
		- [Return](#return)
	- [Expressions](#expressions)
		- [Literals](#literals)
		- [Arithmetic](#arithmetic)
		- [Integer Overflow](#integer-overflow)
		- [Casts](#casts)
		- [Comparison And Boolean Operators](#comparison-and-boolean-operators)
		- [Calls](#calls)
		- [Namespace Resolution By First Parameter](#namespace-resolution-by-first-parameter)
		- [Field Access](#field-access)
		- [Struct Literals](#struct-literals)

- Runtime And API
	- [Runtime Model](#runtime-model)
		- [Native Functions](#native-functions)
	- [Built-Ins](#built-ins)

- Engine
	- [Engine Integration](#engine-integration)
		- [Engine API Design](#engine-api-design)
		- [World Script Lifecycle](#world-script-lifecycle)
		- [Asset Compilation](#asset-compilation)

- Tooling And Notes
	- [Editor](#editor)
	- [Diagnostics](#diagnostics)
	- [Current Limitations](#current-limitations)

## A Small Program

```cpp
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

At the top level, a LumScript module contains `import`, `struct`, `enum`, and `fn` declarations. Functions have explicit parameter and return types. Struct fields are stored in declaration order, which is also the order used by positional struct literals.

## Source Files

LumScript source files use the `.lum` extension.

Whitespace is ignored. Line comments start with `//` and continue to the end of the line.

```cpp
// comment
var speed : f32 = 12.5;
```

## Declarations

### Imports

Imports load declarations from another LumScript source file:

```cpp
import "math"
```

Imported declarations can also be placed under an alias:

```cpp
import "math" as math
```

The quoted path is an import specifier, not a general string value.

Import specifiers that start with `engine:` are reserved for built-in engine modules rather than project files:

```cpp
import "engine:entity" as entity
import "engine:animator" as animator
import "engine:world" as world
```

Declarations from an import without an alias are added to the current module and are accessed by their declared names:

```cpp
import "math"

fn main() : f32 {
	const v : Vec3 = Vec3 { 1, 2, 3 };
	return length(v);
}
```

Declarations from an aliased import are accessed through the alias:

```cpp
import "math" as math

fn main() : f32 {
	const v : math.Vec3 = math.Vec3 { 1, 2, 3 };
	return math.length(v);
}
```

This applies to imported structs, enums, and functions. For example, an enum imported as `game.State` is referenced as `game.State.Running`.

### Structs

Structs declare named fields:

```cpp
struct Transform {
	x : f32;
	y : f32;
	visible : bool;
};
```

Field names must be unique inside a struct. Field types can be primitive types or previously declared user structs.

### Enums

Enums define a set of named integer constants:

```cpp
enum State {
	Idle,
	Running,
	Paused,
	Done
};
```

Enum members are automatically assigned values starting from 0, incrementing by 1. You can also explicitly assign values:

```cpp
enum Priority {
	Low = 0,
	Medium = 5,
	High = 10
};
```

Enums can be used as types in function parameters, struct fields, and local variables:

```cpp
fn handle_state(state : State) : void {
	if state == State.Running {
		// handle running
	}
}

struct Task {
	name : i32;
	priority : Priority;
};
```

Enum values are internally `i32` and can be compared with `==`, `!=`, and relational operators. Conversion to and from `i32` is implicit when assigning to an enum-typed variable or passing as an argument expecting an `i32`.

**TODO no implicit conversion**

#### Shorthand Syntax

When the enum type is known from context, you can use a dot prefix (`.Member`) instead of the full qualified name (`Enum.Member`):

```cpp
fn handle_state(state : State) : void {
	if state == .Running {
		// equivalent to state == State.Running
	}
}

var priority : Priority = .High;  // equivalent to Priority.High
```

This shorthand works in comparisons, assignments, function arguments, and anywhere the target type is unambiguous.

### Functions

Functions are declared with `fn`:

```cpp
fn clamp_min(v : i32, min_value : i32) : i32 {
	if v < min_value {
		return min_value;
	}
	return v;
}
```

Parameter names must be unique. Functions are globally visible and are called by name. Overloading is not supported, so function names must be unique in a module. Parameters are immutable.

### Ref Parameters

`ref` parameters pass an assignable local or field by alias instead of by value.

```cpp
fn increment(v : ref i32) : void {
	v += 1;
}

fn main() : void {
	var x : i32 = 10;
	increment(ref x);
}
```

A `ref` argument must be written using `ref` at the call site and must be assignable (for example a local variable or struct field). `const` values can not be passed as `ref` arguments.

## Types

LumScript currently supports:

- `void`
- `bool`
- `i8`
- `u8`
- `i16`
- `u16`
- `i32`
- `u32`
- `i64`
- `u64`
- `f32`
- `f64`
- `string`
- built-in `Vec3`
- built-in `Quat`
- user-defined `struct` types
- user-defined `enum` types

There are no arrays, pointers, references, optionals, generics, or user-declared methods yet.

`Vec3` and `Quat` are built-in value types used by engine APIs:

```cpp
const position = Vec3 { 1.0, 2.0, 3.0 };
const rotation = Quat { 0.0, 0.0, 0.0, 1.0 };
```

`Vec3` exposes `x`, `y`, and `z` fields. `Quat` exposes `x`, `y`, `z`, and `w` fields.

### Untyped Values

LumScript uses untyped literal values at type-check time.

- Integer literals start as untyped integers.
- Decimal literals start as untyped floating-point values.
- Untyped values are compile-time only and are never runtime value kinds.

An untyped literal is concretized by context (initializer target type, argument type, return type, explicit cast, or expected type in an expression).

When there is no stronger context:

- Integer literals default to `i32`.
- Decimal literals default to `f32`.

LumScript still does not do implicit numeric casts between concrete numeric types. For example, an `i32` or `u32` value is not assigned to `f32` unless the expression uses an explicit cast such as `value as f32`.

### Nullable Values

Nullable values are written with a leading `?` before the base type. Using a nullable value without a null check is a compile-time error.

```cpp
fn find_entity() : ?entity.Entity {
	return null;
}

fn main() : void {
	const e = find_entity();
	if e != null {
		// use e, it's promoted to entity.Entity
	}
}
```

Inside `if value != null`, `value` is promoted to the non-nullable type in that branch.

### Strings

String literals produce `string` values. Strings can be stored in locals, passed to functions, returned from functions, and concatenated with `+`.

```cpp
fn greet(name : string) : string {
	const prefix = "Hello ";
	return prefix + name;
}
```

String interpolation is not implemented yet.

## Variables

Variables are declared with `var` or `const`:

```cpp
var counter : i32 = 0;
const step = 1;

fn tick() : i32 {
	counter += step;
	return counter;
}
```

Top-level variables are module globals. They are initialized once when the runtime first runs the module and keep their values across function calls.

Local variables use the same syntax inside functions and blocks:

```cpp
var count : i32 = 4;
var inferred = count + 1;
const gravity : f32 = 9.8;
```

The type annotation is optional when an initializer is present and the checker can infer the type. `const` variables cannot be assigned after initialization.

Variables are block scoped. A nested block can declare a local with the same name as an outer block:

```cpp
fn scoped() : i32 {
	var value = 1;
	{
		var value = 5;
		value += 1;
	}
	return value; // returns 1
}
```

Local variables can shadow globals inside their scope.

## Statements

### Blocks

Blocks are surrounded by braces and introduce a local scope:

```cpp
{
	var local = 1;
}
```

### Assignment

Assignments update a local variable or a struct field:

```cpp
value = 10;
position.x = position.x + 1;
```

Compound assignments are supported:

```cpp
value += 1;
value -= 1;
value *= 2;
value /= 2;
```

Postfix increment and decrement are accepted as statements:

```cpp
i++;
i--;
```

They are equivalent to `i += 1` and `i -= 1`.

### If And Else

Conditions must be `bool`:

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

`match` is a multi-way branch construct for enums and scalar values. It replaces C-style `switch`; arms do not fall through.

`match` supports enum values, simple scalar literals, inclusive scalar ranges, comma-separated alternatives, and `_` fallback arms:

```cpp
match state {
	case .Idle:
		logError("idle");
	case .Running:
		logError("running");
	case .Paused:
		logError("paused");
	case _:
		logError("unknown");
}
```

Range cases use `..` and include both endpoints:

```cpp
match score {
	case 0:
		logError("none");
	case 1..9:
		logError("low");
	case 10..99:
		logError("high");
	case _:
		logError("overflow");
}
```

Range endpoints must be scalar constants compatible with the matched value type.

Multiple patterns can share one arm by separating them with commas:

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

Literal, enum, and range patterns can be mixed in one arm when they are compatible with the matched value type:

```cpp
match count {
	case 0, 10..19, 99:
		logError("special");
	case _:
		logError("ordinary");
}
```

For enum matches, arms must be exhaustive unless a `_` fallback arm is present. Duplicate enum cases are compile-time errors.

### While

`while` loops reevaluate their condition each iteration. Conditions must be `bool`.

```cpp
var i = 10;
while i > 0 {
	i -= 1;
}
```

The runtime has a configurable step budget to stop accidental infinite loops.

### Defer

`defer` schedules a statement to run when leaving the current scope. Deferred statements run in reverse order (LIFO).

```cpp
fn main() : void {
	defer cleanup();
	// work
}
```

Deferred statements run when leaving the scope normally and also on early `return`.

### Return

Use `return` to leave the current function:

```cpp
fn answer() : i32 {
	return 42;
}

fn done() : void {
	return;
}
```

The returned expression must match the function return type. Use an explicit cast when returning a different scalar type. A bare `return;` is only valid for `void`.

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

Arithmetic on `f32` produces `f32`. Integer arithmetic produces `i32`. `%` is currently integer modulo.

### Integer Overflow

Integer overflow follows wraparound semantics.

- Integer arithmetic is performed in the destination integer type width.
- Results wrap modulo $2^N$, where $N$ is the number of bits of the integer type.
- Overflow does not trap and is not undefined behavior.

Examples:

```cpp
const a : u8 = 255 as u8;
const b : u8 = (a + 1 as u8) as u8; // b == 0

const c : i8 = 127 as i8;
const d : i8 = (c + 1 as i8) as i8; // d == -128
```

Explicit casts between integer widths also wrap to the destination width using the same modulo rule.

### Casts

Use `as` to explicitly convert a value to another type:

```cpp
const whole : i32 = 10;
const decimal = whole as f32;
```

The initial cast support is intended for primitive scalar conversions:

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

Struct casts are not supported.

Assignments, function arguments, return values, and struct literal fields do not cast implicitly. Write the conversion at the expression site:

```cpp
fn takes_f32(v : f32) : void {
	return;
}

fn main() : void {
	const x : i32 = 10;
	takes_f32(x as f32);
}
```

### Comparison And Boolean Operators

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

`and` and `or` short-circuit at runtime.

### Calls

```cpp
const c = add(a, b);
```

Calls are statically checked for function existence, argument count, and argument types.

### Namespace Resolution By First Parameter

Namespaced functions can be called through their first argument when that argument has a namespaced type. The compiler uses the namespace of the first argument type to resolve the function name.

This:

```cpp
import "engine:world" as world
import "engine:entity" as entity

fn move_up(w : world.World, e : entity.Entity) : void {
	const p = w.getPosition(e);
	w.setPosition(e, { p.x, p.y + 1.0, p.z });
}
```

is equivalent to:

```cpp
fn move_up(w : world.World, e : entity.Entity) : void {
	const p = world.getPosition(w, e);
	world.setPosition(w, e, { p.x, p.y + 1.0, p.z });
}
```

In general, `value.func(arg1, arg2)` is resolved as `namespace.func(value, arg1, arg2)`, where `namespace` comes from the type of `value`. For example, `world.World` resolves to the `world` namespace, so `w.findByName("Player")` resolves to `world.findByName(w, "Player")`.

### Field Access

```cpp
position.x
position.y = 4;
```

The left side must be a struct value, and the field must exist on that struct.

### Struct Literals

Struct values are created positionally:

```cpp
var a : Vec3 = { 1, 2, 3 };
const b = Vec3 { 4, 5, 6 };
```

`{ 1, 2, 3 }` uses the expected type from context, including variable declarations, returns, function arguments, and method-call arguments. `Vec3 { 1, 2, 3 }` names the target type directly and can be used when there is no expected type or when the explicit type is clearer. Field count and field types must match the struct declaration.

```cpp
entity.setPosition({ 0.0, 1.0, 0.0 });
entity.setRotation({ 0.0, 0.0, 0.0, 1.0 });
```

Named-field literals are not supported yet.

## Runtime Model

The current runtime is a tree-walk interpreter over the checked AST.

- Functions are called by name.
- Each call creates a frame for parameters and locals.
- Blocks create nested local scopes.
- Struct values store their fields as runtime values in declaration order.
- Runtime calls can pass arguments and optionally receive a result.
- A `RuntimeOptions::max_steps` budget limits execution.
- Native functions can be registered from C++ and called with normal function-call syntax.

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

### Native Functions

Native functions are C++ callbacks with a LumScript signature. Register them on the module after parsing and before type checking:

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

Script code calls the native function like any other function:

```cpp
fn main() : i32 {
	return native_add(20, 22);
}
```

Native calls are checked for function existence, argument count, argument types, and return type.

## Built-Ins

**TODO - normal import**

LumScript modules compiled through the engine builtin path can call:

```cpp
logError(value : string) : void
```

Example:

```cpp
fn update(dt : f32) : void {
	logError("Hello " + "Lumix");
}
```

## Engine Integration

LumScript integrates into Lumix worlds as a **world-level script**. Each world can load one `.lum` script file that runs throughout the world's lifetime.

### Engine API Design

Engine APIs are exposed through virtual `engine:` imports. These imports behave like modules supplied by the engine instead of files on disk:

```cpp
import "engine:entity" as entity
import "engine:animator" as animator
```

The alias is used as a namespace for the imported engine declarations:

```cpp
fn update(e : entity.Entity, input_idx : i32, value : f32) : void {
	const anim = e.animator();
	if anim != null {
		anim.setFloatInput(input_idx, value);
	}
}
```

Engine object types are opaque script types. For example, `entity.Entity` carries the entity id and the owning world context internally, but it remains a distinct LumScript type so it can not be accidentally mixed with an `i32`, animation input index, or another handle type.

Component accessors are generated on `entity.Entity` for reflected engine components. They return nullable opaque component handles, because an entity may not have the requested component:

```cpp
const anim = e.animator(); // ?animator.Animator
if anim != null {
	const speed = anim.getInputIndex("speed_y");
	anim.setFloatInput(speed, 1.0);
}
```

The component handle also carries the entity id and owning world context internally, but its type is specific to the component module. This keeps component APIs from accepting unrelated entities or handles by accident.

Engine systems are exposed as namespaced functions. When the first argument has a namespaced engine type, the same function can also be called through that value:

**TODO getPosition and other are not on world, but enitity**

```cpp
import "engine:entity" as entity
import "engine:world" as world

fn move_up(w : world.World, e : entity.Entity) : void {
	const p = w.getPosition(e);
	w.setPosition(e, { p.x, p.y + 1.0, p.z });
}
```

Here `w.getPosition(e)` resolves to `world.getPosition(w, e)` because `w` has type `world.World`. Likewise, `anim.setFloatInput(speed, 1.0)` resolves to `animator.setFloatInput(anim, speed, 1.0)` because `anim` has type `animator.Animator`. This keeps the scripting model explicit while still allowing short call syntax: handles are values, imported aliases are namespaces, and engine behavior is reached through native functions registered by the imported engine module. Component functions generated by Meta are available when all parameter and return types can be represented in LumScript. Currently this covers `void`, `bool`, `i32`, reflected `u32` values as `i32`, `f32`, `string`, `Vec3`, `Quat`, `entity.Entity`, `world.World`, and generated component handle types.

### World Script Lifecycle

When a `.lum` script file is assigned to a world:

1. **Compilation**: The source code is parsed and type-checked
2. **Initialization**: The `init(world, input_system)` function is called (if it exists)
3. **Update**: The `update(dt)` function is called every frame with frame time in seconds

```cpp
import "engine:world" as world
import "engine:input" as input

fn init(w : world.World, inputs : input.InputSystem) : void {
	// Called once when the world script loads
	// Use for world initialization and setup
}

fn update(dt : f32) : void {
	// Called every frame while the world is running
	// Update world state and logic
}
```

The `world.World` value is an opaque handle to the current Lumix world. The `input.InputSystem` value is an opaque handle to the current input system and exposes the input events collected for the current frame. Both are passed by the engine and should be used with engine API modules rather than constructed by script code.

The `engine:world` module currently exposes basic entity lifecycle functions:

```cpp
import "engine:world" as world
import "engine:entity" as entity
import "engine:input" as input

fn init(w : world.World, inputs : input.InputSystem) : void {
	var e : entity.Entity = world.createEntity(w);
	const player = w.findByName("Player");
	e.setPosition({ 1.0, 2.0, 3.0 });
	e.setRotation({ 0.0, 0.0, 0.0, 1.0 });
	e.setScale({ 2.0, 2.0, 2.0 });

	const p = e.getPosition();
	const r = e.getRotation();
	const s = e.getScale();

	if player != null {
		player.setPosition({ 0.0, 1.0, 0.0 });
	}

	if e.isValid() {
		e.destroy();
	}
}
```

**TODO get rid of getPosition and others on world**

Available functions:

- `world.createEntity(w : world.World) : entity.Entity`
- `world.destroyEntity(w : world.World, e : entity.Entity) : void`
- `world.hasEntity(w : world.World, e : entity.Entity) : bool`
- `world.findByName(w : world.World, name : string) : ?entity.Entity`
- `world.setPosition(w : world.World, e : entity.Entity, position : Vec3) : void`
- `world.getPosition(w : world.World, e : entity.Entity) : Vec3`
- `world.setRotation(w : world.World, e : entity.Entity, rotation : Quat) : void`
- `world.getRotation(w : world.World, e : entity.Entity) : Quat`
- `world.setScale(w : world.World, e : entity.Entity, scale : Vec3) : void`
- `world.getScale(w : world.World, e : entity.Entity) : Vec3`

The `engine:entity` module exposes the same common entity operations with the entity as the first parameter:

- `entity.destroy(e : entity.Entity) : void`
- `entity.isValid(e : entity.Entity) : bool`
- `entity.setPosition(e : entity.Entity, position : Vec3) : void`
- `entity.getPosition(e : entity.Entity) : Vec3`
- `entity.setRotation(e : entity.Entity, rotation : Quat) : void`
- `entity.getRotation(e : entity.Entity) : Quat`
- `entity.setScale(e : entity.Entity, scale : Vec3) : void`
- `entity.getScale(e : entity.Entity) : Vec3`

It also exposes generated component accessors for reflected components:

- `entity.animator(e : entity.Entity) : ?animator.Animator`
- `entity.<component_id>(e : entity.Entity) : ?<component_module>.<ComponentType>`

These are usually called through first-parameter namespace resolution, for example `e.animator()`.

The `engine:input` module exposes raw input event iteration. It does not currently expose action polling helpers such as `isDown`, `wasPressed`, or axis lookup by name.

**TODO enum instead of input.BUTTON()**
**TODO input.Keycode.W -> Keycode.W**

```cpp
import "engine:world" as world
import "engine:input" as input

fn init(w : world.World, inputs : input.InputSystem) : void {
	var i : i32 = 0;
	const count = inputs.getEventCount();
	while (i < count) {
		const e = inputs.getEvent(i);
		const t = e.getType();

		if t == input.BUTTON() {
			const key = e.getKeyId();
			const down = e.isDown();
			if key == input.Keycode.W {
				logError("W");
			}
		}

		if t == input.AXIS() {
			const axis = e.getAxis();
			const value = e.getValue();
		}

		i = i + 1;
	}
}
```

Input event functions:

- `input.getEventCount(inputs : input.InputSystem) : i32`
- `input.getEvent(inputs : input.InputSystem, index : i32) : input.InputEvent`
- `input.getType(e : input.InputEvent) : i32`
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

Input event type helpers return `i32` values: `input.BUTTON()`, `input.AXIS()`, `input.MOUSE_WHEEL()`, `input.TEXT_INPUT()`, `input.DEVICE_ADDED()`, and `input.DEVICE_REMOVED()`.

Meta enums are exposed under the engine import alias. For example, `import "engine:input" as input` exposes `input.Keycode.W`, `input.Keycode.SHIFT`, and the other reflected `Keycode` members as numeric enum constants.

## Editor

Studio can create and open `.lum` files from the asset browser. The LumScript editor uses the built-in code editor with keyword highlighting, save/open/locate actions, and a `Check` action that runs the standalone parser and type checker on the current text.

## Diagnostics

Compilation stops after the first reported error. Parser, checker, and many runtime diagnostics include a line and column:

```txt
line 2, column 9: Unknown variable 'missing'
```

The checker currently reports errors such as:

- duplicate structs, functions, fields, parameters, or locals
- unknown types, variables, functions, or fields
- assignment to `const`
- type mismatch in initializers, assignments, arguments, returns, or struct literals
- non-`bool` conditions for `if` and `while`

## Current Limitations

LumScript is intentionally small right now. These features are not implemented yet:

- only a subset of native `engine:` function signatures are exposed right now
- bytecode (currently interprets AST directly)
- string interpolation
- arrays or maps
- named struct fields in literals
- return-path analysis for every branch

