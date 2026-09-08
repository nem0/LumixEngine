# Evox in Lumix Engine

Evox is Lumix Engine's statically typed scripting language. This page documents the **Lumix integration**: where scripts live, how the engine starts them, how engine APIs are exposed, and how Evox data components and Studio tooling work.

For the language itself—types, declarations, control flow, pointers, slices, templates, reflection, and native ABI—see the [Evox language reference](../external/evox/reference.md). Evox is still experimental and its API can change.

## Getting started

The Evox plugin loads one root script from the project:

```text
scripts/main.evox
```

A minimal root script is:

```evox
import "core:input"
import "core:world"

var game_world : ?World = null;

fn addWorld(world : World) : void {
	game_world = world;
}

fn start(input : InputSystem) : void {
	// Called after the worlds have been supplied to addWorld.
}

fn update(dt : f32) : void {
	// Called once per game frame.
}
```

All three functions are optional. There is no special `main` function.

The repository's working example is [`demo/scripts/main.evox`](../demo/scripts/main.evox).

### Lifecycle

When game mode starts, the Evox system:

1. calls `addWorld(world)` once for every currently registered world;
2. calls `start(input)` once;
3. calls `update(dt)` on each game frame.

Use these exact signatures when the functions are present:

```evox
fn addWorld(world : World) : void
fn start(input : InputSystem) : void
fn update(dt : f32) : void
```

A world created while the game is already running is passed to `addWorld` immediately. `update` is not called while execution is suspended in the debugger.

There is one Evox runtime shared by the Evox system, not one runtime per world. Consequently, script globals are shared by all worlds. A single `g_world` global is sufficient for projects that run one world, but a multi-world project must decide how to retain and identify every `World` passed to `addWorld`.

Stopping game mode destroys and recreates the runtime, resetting script globals and suspended execution state. Compiling the root again also creates a fresh runtime. If this happens during game mode, the engine repeats the `addWorld` calls and then `start`.

### Compilation and errors

`.evox` assets are copied into the compiled resource tree as source text. The root script and all of its imports are compiled to Evox bytecode at runtime. Compilation and runtime-call failures are written to the Studio log.

Saving a changed script causes the asset compiler to process it. Imports are registered as asset dependencies, so changes can propagate to dependent script resources. The active root resource is rebuilt into a new Evox module/runtime when its resource-change notification arrives.

The system is not ready until `scripts/main.evox` has loaded and both source and bytecode compilation have succeeded. If compilation fails, no lifecycle function runs and no script data types are available to worlds.

## Imports and source paths

The filename suffix is normally omitted:

```evox
import "core:entity"
import "core:world"
import "scripts/player" as player
import "maps/demo/demo" as demo
import "std:math"
```

Lumix resolves imports as follows:

| Import | File/source |
|---|---|
| `core:name` | `engine/scripts/core/name.evox` (source files are in `data/scripts/core/`) |
| `some/path` | `some/path.evox` in the project filesystem |
| `std:name` | Built into Evox; no project file |

Non-core paths are project-root paths; they are **not relative to the importing file**. For example, code in `scripts/main.evox` imports `scripts/player`, not just `player`.

An alias creates a namespace:

```evox
import "scripts/player" as player

fn start(input : InputSystem) : void {
	player.init(input);
}
```

Unaliased imports make declarations available to normal lookup. Evox also supports argument-dependent lookup and UFCS, which is why engine calls often read naturally as methods:

```evox
import "core:entity"
import "core:world"

fn removeNamed(world : World) : void {
	const entity = world.findByName("temporary") else return;
	entity.destroy();
}
```

See [Imports](../external/evox/reference.md#imports), [argument-dependent lookup](../external/evox/reference.md#argument-dependent-lookup), and [UFCS](../external/evox/reference.md#ufcs) for the precise lookup rules.

## Using the engine API

Engine-facing declarations are in:

```text
data/scripts/core/
```

These files are the best available API index. Open the unit for the feature you need and import its `core:` name. For example:

```evox
import "core:entity"
import "core:physical_controller"

fn doubleRadius(entity : Entity) : void {
	const controller = entity.physical_controller() else return;
	controller.setRadius(controller.getRadius() * 2.0);
}
```

Common forms are:

- **Entities:** `Entity` stores an entity index and its `World`; `core:entity` provides operations such as `destroy`, transforms, and `findChildByName`.
- **Components:** `entity.model_instance()` returns `?ModelInstance`; `createModelInstance(entity)` creates the component and returns it when successful.
- **Modules:** `world.physics()` returns `?PhysicsModule`. Module functions take that handle as their first argument.
- **Objects:** engine object wrappers hold a native pointer and expose their functions through the object's unit.
- **Collections returned by modules:** native `Span<T>` returns are generated as Evox custom iterators, allowing `for value in module.someFunction(...)` without exposing the native span's lifetime directly.

Component and module lookups are nullable because an entity may not have the component and a world may not have the module:

```evox
const actor = entity.rigid_actor() else return;
if const physics = world.physics() {
	// use physics
}
```

Generated component property names are `get<Name>` and `set<Name>`. Array properties additionally generate a count function, indexed item lookup, and child accessors. Consult the generated unit rather than guessing names.

### Generated versus hand-written core units

Most `data/scripts/core/*.evox` files and `src/evox/evox_capi.gen.h` are generated from engine Meta declarations by `src/meta/evox_meta.cpp`. Some integration APIs—input events, logging, ImGui, basic entity/world operations, and Evox data access—are registered manually in `src/evox/evox_engine_api.cpp`.

Do not make lasting edits to a generated core file. Change the C++ Meta declaration or the Evox Meta generator and regenerate the outputs. Hand-written helper code in a core unit, such as `World.getEvoxData`, must remain consistent with its native registration.

### Binding limitations

The generator exposes only C++ signatures it knows how to marshal. Supported categories include the common scalar types, engine math structs, entities, compatible reflected structs, strings/paths, object handles, and supported spans. Unsupported functions are skipped; Meta prints a diagnostic naming the rejected argument.

Notable mapping details:

- C++ `int` and `i32` appear as Evox `i32`; C++ `u32` appears as Evox `u32`.
- `float` appears as `f32`.
- `StringView`, paths, and supported C strings appear as `[]const u8`.
- object pointers become nullable object handles;
- `EntityRef` and `EntityPtr` become `Entity` values associated with the module's world;
- native enums are strongly typed Evox enums.

The declaration's unit and function name must exactly match the native registration. A declaration alone does not implement a native function.

## Script-defined entity data

Evox can declare data types that Studio attaches to entities. This is data storage, not an automatically executed script component: attaching a type does not call methods on it. Your lifecycle code queries and processes the data explicitly.

Import the marker attributes and annotate a struct:

```evox
import "core:entity"
import "core:evox_data" as evox_data
import "core:world"

#[evox_data.Data {}]
struct Spinner {
	#[evox_data.Owner {}]
	entity : Entity;
	speed : f32;
	angle : f32;
}

fn updateSpinners(world : World, dt : f32) : void {
	for &spinner in world.getEvoxData(Spinner) {
		spinner.angle += spinner.speed * dt;
		// Use spinner.entity to update the owner.
	}
}
```

`#[Data {}]` is recognized by attribute type name and makes the struct available as an Evox data type after successful root compilation. The declaration may be in the root or any compiled import.

In Studio, add an **Evox / Data** component and select the marked type. The property grid can add or remove data types and edit supported fields. Each entity can contain at most one value of a given data type. Adding data zero-initializes it.

`world.getEvoxData(T)` returns a writable `[]T` over all instances of `T` in that world. The slice aliases module-owned storage: changes to its elements change component data directly. Do not retain the slice across component additions/removals, entity destruction, script recompilation, world destruction, or any operation that can relocate the storage.

Use `for &value in ...` when fields must be changed. Plain `for value in ...` iterates over immutable copies.

### Owner injection

A direct field marked `#[Owner {}]` is filled with the entity that owns the data:

```evox
#[evox_data.Data {}]
struct Health {
	#[evox_data.Owner {}]
	owner : Entity;
	current : i32;
	maximum : i32;
}
```

Owner injection currently requires the marked field to be a struct named `Entity`. The engine writes both its entity index and world pointer whenever data is added or restored. The field need not be named `owner`.

### Persistence and supported fields

Evox data is serialized with the world. The serializer retains these kinds:

- `bool`;
- signed and unsigned integers from 8 through 64 bits;
- `f32` and `f64`;
- enums;
- `cptr`;
- structs, recursively.

Other kinds—including arrays, slices, nullable values, unions, pointers, and functions—are currently omitted from persistence. A struct remains a valid data type even if some or all of its fields are omitted. Omitted fields are zero after load or hot reload. `cptr` fields are included in the schema but deliberately restored as null; native addresses are never persisted.

Schema migration is intentionally conservative:

- a data type is matched by its type name and kind;
- struct fields are matched recursively by field name and kind, not position;
- reordered fields survive;
- removed fields are discarded;
- new fields are zeroed;
- a field whose kind changed is zeroed;
- serialized `Entity` indices are remapped when entities are remapped, and their world pointers are restored.

Renaming a data type or field therefore loses the old value. Do not depend on unsupported fields surviving save/load or script recompilation.

## Studio tools

Double-click an `.evox` asset to open the Evox editor. It provides:

- syntax highlighting and save/reload handling;
- **Check**, which compiles the editor buffer and underlines a diagnostic in the current file;
- go to definition;
- autocomplete from symbols in the last successfully compiled runtime module;
- a symbol search palette (`Ctrl+Q` by default);
- breakpoint markers (`F8` by default).

Autocomplete does not parse the current unsaved buffer. This lets it keep working while that buffer is incomplete, but newly typed declarations do not appear until a successful runtime compilation.

The **Evox Debugger** supports continue (`F1`), step over (`F2`), step into (`F3`), and step out (`Shift+F11`). The call-stack view opens source locations, and the Variables window displays locals and relevant project globals while execution is suspended. Breakpoints are reapplied when a runtime becomes available.

Debug source names use import names. Studio maps `core:name` to `engine/scripts/core/name.evox` and ordinary source names to project paths.

## Troubleshooting

### Nothing runs

- Confirm the Evox plugin is built and loaded.
- Confirm the root is exactly `scripts/main.evox`.
- Check the Studio log for source or bytecode compilation errors.
- Verify lifecycle names and signatures exactly.
- Enter game mode; `start` and `update` do not run in edit mode.

### An import cannot be found

- Omit `.evox` or spell it consistently.
- Use a project-root path for project scripts (`scripts/player`, not a path relative to the current file).
- Use `core:name` for files under `data/scripts/core/`.
- `std:` is reserved for Evox built-ins.

### An engine function is missing

- Import the unit that declares it.
- Search `data/scripts/core/` for the generated declaration.
- Check whether its C++ signature is supported by `src/meta/evox_meta.cpp`.
- Check Meta's output for a “skipped” diagnostic.
- If hand-written, verify the unit/name registration in `src/evox/evox_engine_api.cpp`.

### A data type does not appear in Studio

- Import `core:evox_data` and apply `#[evox_data.Data {}]` to a struct.
- Ensure the file containing the declaration is reachable from the root's import graph.
- Fix all root compilation errors; types are discovered from successful bytecode only.
- Re-enter or reload after the compiled root resource changes.

### Data reset after a script change

Check the persistence rules above. Renamed types/fields, changed field kinds, and unsupported field types do not migrate. Native `cptr` values always reset to null.
