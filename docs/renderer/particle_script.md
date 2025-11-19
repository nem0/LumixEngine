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

### Control Flow
- `if` / `else` - Conditional statements
- `return` - Return from function

### Other
- `import` - Import other scripts

## Control Flow

Particle scripts support conditional execution using `if`/`else` statements. `else`
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
- `emit_on_move` - Emit particles when emitter moves
- `max_ribbons` - Maximum number of ribbons
- `max_ribbon_length` - Maximum ribbon length
- `init_ribbons_count` - Initial ribbon count

## Input Variables

Input variables (`in`) declare parameters that can be passed when instantiating particles from another emitter. They allow emitters to communicate data between each other.

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

Input variables are only accessible in the `emit()` function and are used to initialize particle state when spawned from another emitter.

## Entry points

### emit()
Called when emitting new particles. Initialize particle state here.

### update()
Called every frame for each particle. Update particle state, check lifetime, etc.

### output()
Called before rendering. Set output channels for rendering.

## User-Defined Functions

Particle scripts support user-defined functions to organize and reuse code. Functions must be defined globally and can take parameters.

### Function Definition

```hlsl
fn function_name(param1, param2, ...) {
    // function body
    return value;
}
```

Examples:

```hlsl
fn abs(x) {
	return max(x, -x);
}

fn saturate(x) {
	return max(0, min(1, x));
}

fn sphere(r) {
	let lat = random(0, 2 * PI);
	let lon = random(0, 2 * PI);
	let res : float3;
	let tmp = sin(lon) * r;
	res.x = cos(lat) * tmp;
	res.y = sin(lat) * tmp;
	res.z = cos(lon) * r;
	return res;
}
```

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
- `emit(emitter)` - Emit new particle in specified `emitter` (update function only)
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

Vectors support swizzling and component-wise operations:

```hlsl
pos = {1, 2, 3};
x = pos.x;
xy = pos.xy;
pos.yz = {4, 5};
```

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
	
	// par-particle output data, read in shaders
    out i_rot_lod : float4
	out i_pos_scale : float4

	// per-particle runtime data, only accessed by the particle system
    var pos : float3
	var vel : float3
	var t : float

	// called every frame
    fn update() {
		t = t + time_delta;
		pos = pos + vel * time_delta;
		vel.y = vel.y - G * time_delta;
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
	}

	// called before rendering to output data to rendering system
    fn output() {
		i_pos_scale.xyz = pos;
		i_pos_scale.w = min(pos.y * 0.1, 0.1);
		i_rot_lod = {0.707, 0, 0.707, 0};
	}
}
```

## Editor Features

The particle script editor provides:
- Syntax highlighting
- Real-time preview
- Debug information showing register usage
- Play/pause/step controls
- Global variable editing
- Performance statistics