# Particle Scripts

Particle scripts in Lumix Engine define particle system behavior using a custom scripting language. Scripts are compiled to bytecode and executed on the CPU for high-performance particle simulation.

## File Extension

Particle script files use the `.pat` extension. Particle script files can import other files with `.pai` extension.

## Basic Structure

A particle script consists of one or more emitters. Each emitter defines the behavior of a particle system.

```hlsl
emitter EmitterName {
    material "/path/to/material.mat"
    init_emit_count 10
    emit_per_second 5

    // Output channels (required)
    out position : float3
    out scale : float
    out color : float4

    // Variables
    var velocity : float3
    var lifetime : float

    // Functions
    fn emit() {
        // Initialize new particles
    }

    fn update() {
        // Update existing particles
    }

    fn output() {
        // Set output values for rendering
    }
}
```

## Data Types

- `float` - Single floating-point value
- `float2` - 2-component vector (x, y)
- `float3` - 3-component vector (x, y, z)
- `float4` - 4-component vector (x, y, z, w)

## Keywords

### Declaration Keywords
- `emitter` - Defines a particle emitter. Single file can contain multiple emitters.
- `out` - Declares output channels for rendering. This can be accessed from shaders.
- `in` - Declares input parameters for emitter instantiation.
- `var` - Declares per-particle data.
- `let` - Declares block-local variables.
- `const` - Declares global constants.
- `global` - Declares global variables. This can be set from outside the particle system, e.g., by a lua script.
- `fn` - Defines a function.

## Global Constants

Global constants are compile-time evaluated values that are accessible throughout the particle script. They are declared using the `const` keyword and must be initialized with a constant expression.

### Declaration

```hlsl
const PI = 3.14159;
const GRAVITY = 9.8;
const MAX_SPEED = 100.0;
```

Constants can use expressions, including function calls, as long as they can be evaluated at compile time:

```hlsl
const HALF_PI = PI / 2;
const GRAVITY_SQUARED = GRAVITY * GRAVITY;
const MAX_DISTANCE = sqrt(MAX_SPEED * MAX_SPEED + GRAVITY_SQUARED);
```

### Usage

Constants can be used anywhere in the script where a literal value is expected:

```hlsl
fn update() {
    velocity.y = velocity.y - GRAVITY * dt;
    if speed > MAX_SPEED {
        // clamp speed
        speed = MAX_SPEED;
    }
}
```

Constants are evaluated once during compilation and cannot be modified at runtime.

### Control Flow
- `if` / `else` - Conditional statements

### Other
- `import` - Import other scripts

## Control Flow

Particle scripts support conditional execution using `if`/`else` statements.
### If/Else Statements

```hlsl
if condition {
    // Code executed if condition is true
} else {
    // Code executed if condition is false (optional)
}
```

Conditions can use comparison operators (`<`, `>`) and boolean expressions:

```hlsl
fn update() {
    if life > 1.0 {
        kill(); // Kill particle if lifetime exceeded
    } else {
        scale = scale * 1.1; // Grow particle
    }
}
```

Note: If/else statements are not SIMD-friendly, as branching is not vectorized in particle simulations.



## Emitter Properties

- `material` - Path to the material file (for sprite/ribbon particles)
- `mesh` - Path to the mesh file (for mesh particles)
- `init_emit_count` - Initial number of particles to emit
- `emit_per_second` - Particles emitted per second
- `emit_move_distance` - Emit particles when emitter moves by this distance (negative value disables)
- `max_ribbons` - Maximum number of ribbons
- `max_ribbon_length` - Maximum ribbon length
- `init_ribbons_count` - Initial ribbon count

## Input Variables

Input variables (`in`) declare parameters that can be passed when instantiating particles from another emitter. They allow emitters to communicate data between each other.

Input variables are **only accessible in the `emit()` function** and are used to initialize particle state when spawned from another emitter.

```hlsl
emitter ChildEmitter {
    // Input parameters
    in spawn_position : float3
    in particle_color : float3

    // ... other declarations ...

    fn emit() {
        // Use input values to initialize particle
        pos = spawn_position;
        color = particle_color;
    }
}

emitter ParentEmitter {
    // ... declarations ...

    fn update() {
        if t > 1.5 {
            // Emit particles from ChildEmitter with input values
            emit(ChildEmitter) {
                spawn_position = current_position;
                particle_color = {1, 0, 0}; // red
            };
        }
    }
}
```

## Entry Points and Context Restrictions

Particle scripts have three main entry points, each with specific restrictions on what can be accessed:

### emit()
Called when emitting new particles. Initialize particle state here.
- Can access `in` variables
- Cannot access `out` variables
- Can call `emit()` to spawn particles from other emitters

### update()
Called every frame for each particle. Update particle state, check lifetime, etc.
- Can access `var` variables
- Cannot access `in` or `out` variables
- Can call `kill()` to remove particles
- Can call `emit()` to spawn new particles

### output()
Called before rendering. Set output channels for rendering.
- Can access `out` variables
- Can access `var` variables
- Cannot access `in` variables
- Cannot call `kill()` or `emit()`

## Variable Scope and Accessibility

- `in` variables: Only accessible in `emit()` function
- `out` variables: Only accessible in `output()` function  
- `var` variables: Accessible in `emit()`, `update()`, and `output()` functions
- `global` variables: Accessible everywhere
- `const` variables: Accessible everywhere (compile-time constants)

## User-Defined Functions

Particle scripts support user-defined functions to organize and reuse code. Functions must be defined globally and can take parameters.

### Function Definition

```hlsl
fn function_name(param1, param2, ...) {
    // function body
	result = value;
}
```

Functions do not use the `return` keyword. Instead, assign the function output to the implicit `result` local. The name `result` is reserved and cannot be used with `let`.

The type of `result` is inferred from assignments in the function body: assigning a scalar makes it `float`, and assigning a compound literal like `{1, 2, 3}` makes it `float3` (similarly 2/4 elements infer `float2`/`float4`). Once inferred, subsequent assignments to `result` must match the inferred vector width, and accessing components like `result.z` is only valid if `result` has been inferred wide enough.

If `result` is assigned in `if`/`else` blocks, the compiler still infers a single type for the whole function: all assignments to `result` across all branches must agree on the same vector width. If different branches assign different widths (e.g. `float3` in the `if` branch and `float` in the `else` branch), compilation fails with a type mismatch. If only some branches assign to `result`, the function still has the inferred type, but on the paths where `result` is not assigned it will keep its default value.

Valid example (infers `float3`):

```hlsl
fn make_vec3() {
    result = {1, 2, 3};
    result.z = 4;
}
```

Invalid examples:

```hlsl
fn bad_component() {
    result = {1, 2};
    result.z = 3; // error: result inferred as float2
}

fn bad_mismatch() {
    result = 1;
    result = {1, 2}; // error: type mismatch (float vs float2)
}
```

Examples:

```hlsl
fn abs(x) {
	result = max(x, -x);
}

fn saturate(x) {
	result = max(0, min(1, x));
}

fn sphere(r) {
	let lat = random(0, 2 * PI);
	let lon = random(0, 2 * PI);
	let res : float3;
	let tmp = sin(lon) * r;
    res.x = cos(lat) * tmp;
    res.y = sin(lat) * tmp;
    res.z = cos(lon) * r;
    result = res;
}
```

### Generic Functions

Functions in particle scripts can be used generically: a single user-defined function may accept and return different concrete vector widths depending on how it's called. The compiler infers parameter and `result` types from call sites and assignments, allowing one function body to be specialized for `float`, `float2`, `float3`, or `float4` as needed.

Example (from tests):

```hlsl
fn identity(v) {
    result = v;
}

emitter test {
    out o3 : float3
    out o4 : float4

    var v3 : float3
    var v4 : float4

    fn emit() {
        v3 = {1, 2, 3};
        v4 = {4, 5, 6, 7};
    }

    fn output() {
        o3 = identity(v3); // identity used as float3 -> returns float3
        o4 = identity(v4); // identity used as float4 -> returns float4
    }
}
```

Notes:
- The compiler creates an appropriate specialization based on argument types: `identity(float3)` and `identity(float4)` are both valid uses of the same function body.
- Within a single specialization (a given call signature), all assignments to `result` must be compatible (same vector width). Mismatched widths across branches still produce a compile error.
- Generic usage relies on type inference from call sites; if the compiler cannot determine types, make usages explicit so the intended types are clear.

## Built-in Functions

### Math Functions
- `sin(float)` - Sine
- `cos(float)` - Cosine
- `sqrt(float)` - Square root
- `min(float, float)` - Minimum
- `max(float, float)` - Maximum

### Random Functions
- `random(float min, float max)` - Random float between min and max
- `noise(float)` - Perlin noise

### Particle Functions
- `emit()` - Emit new particle from current emitter (update function only)
- `emit(emitter)` - Emit new particle from specified emitter with input parameters (update function only)
- `kill()` - Kill current particle (update function only)

## System Values

- `time_delta` - Time since last frame
- `total_time` - Total elapsed time
- `emit_index` - Index of current particle being emitted
- `ribbon_index` - Index for ribbon particles
- `entity_position` - World space position of emitting entity - useful to make world space particles

## Operators

- Arithmetic: `+`, `-`, `*`, `/`, `%`
- Comparison: `<`, `>`
- Assignment: `=`

## Vector Operations

Vectors support swizzling for reading and writing individual components or combinations of components:

### Reading Components

```hlsl
let pos = {1, 2, 3, 4};
let x = pos.x;        // 1
let y = pos.y;        // 2
let z = pos.z;        // 3
let w = pos.w;        // 4

// Read multiple components
let xy = pos.xy;      // {1, 2}
let xyz = pos.xyz;    // {1, 2, 3}
let rgb = pos.rgb;    // {1, 2, 3} (same as xyz)

// Read with repetition
let xx = pos.xx;      // {1, 1}
let yyy = pos.yyy;    // {2, 2, 2}
let zz = pos.zz;      // {3, 3}

// Read mixed components
let xyx = pos.xyx;    // {1, 2, 1}
let zwz = pos.zwz;    // {3, 4, 3}
```

### Writing Components

```hlsl
let mut pos = {1, 2, 3, 4};

// Write single component
pos.x = 10;           // pos = {10, 2, 3, 4}

// Write multiple components
pos.xy = {20, 30};    // pos = {20, 30, 3, 4}
pos.xyz = {1, 2, 3};  // pos = {1, 2, 3, 4}

// Write with swizzle
pos.yz = {40, 50};    // pos = {1, 40, 50, 4}
```

### Component Names

Vectors use the following component names:
- `x`, `y`, `z`, `w` for general use
- `r`, `g`, `b`, `a` for color operations (equivalent to x, y, z, w)

All component names can be mixed and matched in swizzles.

## Example

```cpp
const horizontal_spread_from = 0.8;
const horizontal_spread_to = 1.2;
const start_vertical_velocity = 5;
const G = 9.8;

emitter Emitter0 {
	mesh "/engine/models/sphere.fbx"
	init_emit_count 0
	emit_per_second 300
	
	// per-particle output data, read in shaders
    out i_rot_lod : float4
	out i_pos_scale : float4

	// per-particle runtime data, only accessed by the particle system
    var pos : float3
	var vel : float3
	var t : float
	var uv_offset : float2  // Example of float2 usage

	// called every frame
    fn update() {
		t = t + time_delta;
		pos = pos + vel * time_delta;
		vel.y = vel.y - G * time_delta;
		
		// Update UV animation
		uv_offset.x = uv_offset.x + time_delta * 0.1;
		uv_offset.y = uv_offset.y + time_delta * 0.05;
		
		kill(pos.y < -0.01);
	}

	// called when emitting a particle
    fn emit() {
		pos = {0, 0, 0};
		let angle = random(0, 2 * 3.14159265);
		let h = random(horizontal_spread_from, horizontal_spread_to);
		vel.x = cos(angle) * h;
		vel.y = start_vertical_velocity;
		vel.z = sin(angle) * h;
		t = 0;
		uv_offset = {0, 0};
	}

	// called before rendering to output data to rendering system
    fn output() {
		i_pos_scale.xyz = pos;
		i_pos_scale.w = min(pos.y * 0.1, 0.1);
		i_rot_lod = {0.707, 0, 0.707, 0};
		
		// Output UV offset as part of rotation/lod data
		i_rot_lod.zw = uv_offset;
	}
}
```

## Editor Features

The particle script editor provides:
- Syntax highlighting
- Autocomplete
- Real-time preview
- Debug information showing register usage and instruction count
- Play/pause/step controls
- Global variable editing