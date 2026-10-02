# Evox texture recipes (.ltct)

`.ltct` builds a texture by running an ordinary Evox program. Operations are **eager**: `load` and `resize` finish before returning an image; in-place operations such as `map` finish before returning. Recipes support 2D textures, texture arrays, and cubemaps.

```evox
import "core:texture_recipe"
import "core:vec4"

fn main() : Texture2D {
    var image = resize(load("engine/textures/white.tga"), 256, 256);
    map(image,
        fn(x : u32, y : u32, value : Vec4) : Vec4 {
            return Vec4 { x as f32 / 255, y as f32 / 255, value.z, 1 };
        });
    return texture2D(image);
}
```

The API is `data/scripts/core/texture_recipe.evox`. `Image` is a concrete build-local handle with `width`, `height`, and `channels`. Copies of an image handle alias its storage; do not modify the handle or its dimensions/channel fields. The host frees all image storage after the build. No context argument or manual destruction is needed.

For a more elaborate example, open `data/scripts/tests/ion_marble.ltct` in Studio. It produces luminous turquoise/amber mineral veins using cellular and gradient noise, warp, twirl, a color gradient, and normal-map lighting. It is self-contained; edit `SIZE`, the vein frequency/power, and the palette to explore variations.

`data/scripts/tests/orbital_alloy.ltct` shows a different approach: beveled sci-fi panels with vents, screw heads, cyan conduits, and amber hazard stripes. It combines coordinate-based masks, checkerboard variation, gradient coloring, blurred seam glow, noise wear, and normal-map lighting. Adjust `SIZE`, the palette, and the lighting/glow values to customize it.

`data/scripts/tests/tree_bark.ltct` generates rough brown bark with elongated fissures, chipped ridges, short cross-cracks, and three small knots. It uses stretched gradient noise, cellular noise, a brown color gradient, and baked normal-map lighting. Adjust `RIDGES` for coarser/finer bark, `SIZE` for resolution, and the palette for different wood colors.

## Pixel values and colors

The current API uses `Vec4` for pixel values. Its components correspond to RGBA when working with colors, but can also represent normals, masks, or other numeric texture data. `getPixel`, `setPixel`, and mapping callbacks therefore use a general numeric value rather than assuming every texture contains colors.

A pixel value's four components are separate from the image's stored channel count. For example, `getPixel` on a one-channel image returns `Vec4 { value, 0, 0, 1 }`; `setPixel` writes only the components stored by that image. `texture2D(image)`, `map`, and `mapWith` preserve the input channel count. `textureArray(images)` also preserves it and requires all layers to agree. `color` creates four-channel images.

A dedicated `Color { r, g, b, a }` type is a proposed API improvement for color-specific operations such as fills and blending. General pixel operations would retain `Vec4` so non-color texture data keeps neutral component names. `Color` is not implemented in the recipe API yet; the examples below use the existing `Vec4` signatures.

## Operations

| Function | Behavior |
| --- | --- |
| `load(path)` | Immediately decodes a project-relative source image and records its dependency. |
| `color(width, height, value)` | Creates a filled RGBA image. `value` is a `Vec4`. |
| `resize(image, width, height)` | Returns a new bilinearly resized image, preserving channel count. |
| `map(image, callback)` | Modifies the image in place and returns `void`. Calls `fn(u32, u32, Vec4) : Vec4` for each pixel, storing only the image's channels. |
| `mapWith(image, state, callback)` | The same, with a typed fourth argument containing explicit state. |
| `create(width, height, channels)` | Creates zero-filled editable storage with 1–4 channels. |
| `getPixel(image, x, y)` | Reads an in-bounds pixel; absent G/B are zero and absent alpha is one. |
| `setPixel(image, x, y, value)` | Writes the image's stored components. |
| `texture2D(image)` | Selects the output image, preserving its channel count. `main` returns `Texture2D`. |
| `textureArray(images)` | Selects ordered array layers from `[]const Image`, preserving their channel count. `main` returns `TextureArray`. |
| `textureCube(faces)` | Takes `[6]Image` in +X, -X, +Y, -Y, +Z, -Z order. Faces must be square with matching dimensions/channels. `main` returns `TextureCube`. |
| `samplePixel(image, x, y, wrap)` | Integer pixel coordinates (`i32`); wraps when true, otherwise clamps. |
| `sample(image, x, y, wrap)` | Bilinear sampling in pixel coordinates (`f32`), with the same boundary modes. Integer coordinates are pixel centers. |
| `copy(image)` | Copies pixels into independent storage. |
| `checkerboard(width, height, size)` | One-channel alternating cells; positive cell size in pixels. |
| `circle(width, height, power)`, `square(...)`, `triangle(...)` | One-channel center-distance fields raised to positive `power`; center is zero, boundary distance is one. Distances can exceed one beyond the circle/triangle boundary. |
| `gradient(size)` | One-channel horizontal ramp, width `size`, height 1. Endpoints are 0 and 1; a single pixel is 0.5. |
| `valueNoise(width, height, seed)` | One-channel random pixels using the LTC Marsaglia generator and explicit `u32` seed. |
| `gradientNoise(width, height, scale)` | One-channel gradient noise with LTC hashes and quintic interpolation. Positive `f32` scale, at most 16384; integer scales tile. |
| `cellNoise(width, height, scale, offset)` | One-channel distance to the nearest animated cell point. Scale and phase offset are `f32`. |
| `waveNoise(width, height, scale, offset)` | One-channel interpolated sine noise; `f32` scale and coordinate offset. |
| `crop(image, x, y, width, height)` | In-bounds rectangle; unsigned pixel coordinates/dimensions. |
| `flip(image, horizontal, vertical)` | Reflects pixels along the selected axes. |
| `translate(image, dx, dy, wrap)` | Reads from output coordinates plus signed integer offsets, with wrap/clamp boundaries. |
| `warp(image, pattern, intensity)` | Bilinear displacement using central differences of a matching scalar pattern; clamps boundaries. |
| `twirl(image, intensity)` | Rotates sampling coordinates by radial distance times intensity, using bilinear wrapped sampling. |
| `blur(image, iterations)` | Repeated separable horizontal/vertical three-tap box blur; clamps boundaries. |
| `vBlur(image, iterations)` | Repeated vertical three-tap box blur; clamps boundaries. |
| `sharpen(image)` | Clamped 3x3 convolution: center 17/9, other taps -1/9. |
| `normalmap(image, intensity)` | Scalar height field to two-channel encoded XY slopes, using wrapped central differences. |
| `shade(image, height, intensity, light)` | Bakes relief lighting into RGB (or the stored scalar channels), preserving alpha and channel count. Height must be scalar with matching dimensions. `light` is a `ReliefLight`. |
| `split(image, channel)` | Extracts one stored component into a scalar image; channel is `u32`, zero-based. |
| `merge(channels)` | Combines `[]const Image` containing 1–4 matching scalar images into ordered channels. |
| `setAlpha(image, alpha)` | RGB/RGBA plus a matching scalar alpha image to RGBA. |
| `splat(image)` | Replicates a scalar image to all four channels, including alpha. |
| `multiply(a, b)`, `divide(a, b)`, `minimum(a, b)`, `maximum(a, b)` | Componentwise operations on matching dimensions/channels. Zero division fails the build. |
| `mix(a, b, alpha)` | Componentwise linear interpolation, including alpha, using a scalar `f32` weight. |
| `invert(image)` | One minus every stored component, including alpha. |
| `step(image, edge)` | Every stored component becomes zero below `edge`, one otherwise. |
| `curve(image, points)` | Catmull–Rom remap of every stored component. Takes `[]const Vec2` with strictly increasing X coordinates; clamps outside endpoints. |
| `brightness(image, amount)` | Adds `f32` amount to RGB, preserving alpha. |
| `contrast(image, amount)` | LTC contrast formula on every stored component; amount in [-255, 255]. |
| `gamma(image, amount)` | Raises RGB to 1/amount; positive amount, alpha preserved. |
| `grayscale(image)` | RGB luminance weights 0.299/0.587/0.114, replicated to RGB; alpha preserved. Requires RGB/RGBA. |
| `gradientMap(image, stops)` | Scalar input to RGBA using `[]const ColorStop`, each `{ position:f32; value:Vec4; }`. At least two strictly ordered stops; linear interpolation with clamped endpoints. |
| `gridSplatter(background, pattern, x_count, y_count, x_spread, y_spread, seed)` | Places patterns at grid cell origins with clipping and alpha blending. Counts/spreads/seed are `u32`; spreads are pixel jitter radii. Inputs must have matching channels. |
| `circularSplatter(background, pattern, count, radius, radius_step, radius_spread, angle_step, angle_spread, seed)` | Places centered patterns around the background center; each copy advances radius and angle. Radius/jitter in pixels, angles/jitter in radians, count/seed are `u32`. Uses clipping and alpha blending. |

Operations that preserve dimensions and channel count modify their input in place and return `void`: `map`, `mapWith`, `flip`, `translate`, `warp`, `twirl`, `blur`, `vBlur`, `sharpen`, `shade`, `invert`, `step`, `curve`, `brightness`, `contrast`, `gamma`, and `grayscale`. Binary operations (`multiply`, `divide`, `minimum`, `maximum`, `mix`) modify their first image; splatter operations modify their background. Image aliases observe these changes. Use `copy(image)` before modifying an image whose original pixels are still needed. Generators and layout-changing operations (`resize`, `crop`, `split`, `merge`, `setAlpha`, `splat`, `gradientMap`, `normalmap`) return independent storage. `map` and `mapWith` visit pixels in row order; callbacks reading neighboring pixels should read an explicit copy of the input. All listed LTC image nodes have Evox equivalents. Constant values, coordinates, arithmetic, and static switches inside LTC functions map to ordinary Evox expressions and `if` statements inside `map`/`mapWith`.

`ReliefLight { direction:Vec2; ambient:f32; minimum:f32; maximum:f32; }` controls the lightweight relief shading used by the examples. `shade` generates normals with `normalmap(height, intensity)` and computes `ambient + (normal.x - 0.5) * direction.x + (normal.y - 0.5) * direction.y`, clamped to the supplied minimum/maximum. Direction components are XY lighting weights; they do not need normalization. The result multiplies the input's stored color channels and leaves alpha unchanged. For example:

```evox
shade(albedo, height, 2.2,
    ReliefLight { Vec2 { 1.1, -0.45 }, 0.9, 0.35, 1.675 });
```

Import `core:vec2` to construct the direction. Apply grain, weathering, or emissive details afterward, as the example recipes do.

Shape/noise generators return scalar images; use `splat`, `merge`, or `gradientMap` for more channels. Operations do not clamp numeric output to [0, 1] except encoded normal slopes. Blur uses one scratch image regardless of iteration count; zero iterations leaves the image unchanged. Curve/gradient-map endpoints clamp, and negative sampling coordinates wrap correctly, avoiding LTC's edge-case bugs.

Noise follows the LTC formulas, though floating-point results can vary slightly across implementations. Evox currently lacks bitwise operators, so the gradient-noise hash computes XOR with integer arithmetic; large procedural recipes may build slowly.

A cubemap recipe can return:

```evox
import "core:texture_recipe"
import "core:vec4"
fn main() : TextureCube {
    var image = color(16, 16, Vec4 { 0.25, 0.5, 0.75, 1 });
    return textureCube([image, image, image, image, image, image]);
}
```

See `data/scripts/tests/texture_recipe_cube.ltct` for distinct face colors. Studio provides the existing **Side** selector for previewing faces. Cubemaps have a 32M output-pixel limit, including repeated faces.

For a texture array, return `TextureArray` from `main`:

```evox
import "core:texture_recipe"
import "core:vec4"

fn main() : TextureArray {
    var layers : [2]Image = [
        color(64, 64, Vec4 { 1, 0, 0, 1 }),
        color(64, 64, Vec4 { 0, 1, 0, 1 })
    ];
    return textureArray(layers);
}
```

Layers must have matching width, height, and channel count. Reusing an image in multiple layers is supported. `textureArray` is an Evox function that returns a `TextureArray` containing a `layers : []const Image` slice. The slice aliases the supplied array, so changes to its entries are reflected in the output. Keep the backing array in `main` or global storage until `main` returns, when Studio validates and copies the layers. Studio's texture editor offers a layer selector alongside the code editor and preview. See `data/scripts/tests/texture_recipe_array.ltct` for a three-layer example.

`map` and `mapWith` are short Evox loops, not C++ callback bridges. Callbacks execute immediately. Use explicit state instead of capturing recipe locals:

```evox
import "core:texture_recipe"
import "core:vec4"
struct Settings { gain : f32; }
fn main() : Texture2D {
    var image = color(16, 16, Vec4 { 0.25, 0.5, 0.75, 1 });
    mapWith(image, Settings { 2 },
        fn(x : u32, y : u32, value : Vec4, settings : Settings) : Vec4 {
            return Vec4 { value.x * settings.gain, value.y, value.z, value.w };
        });
    return texture2D(image);
}
```

Ordinary `if` statements select which operations execute; unselected branches do no image work. Comptime can specialize callback state types but cannot load or process images.

Loading, resizing, and filling share implementation with `.ltc`. Output uses the existing texture compression/mipmap pipeline and metadata. `.ltct` appears as a texture asset with compiled preview and thumbnail; its texture editor includes a code editor beside the settings and preview. Saving writes the recipe source and recompiles the texture. The external-editor action is also available. The asset browser still creates `.ltc` by default in this review-sized prototype.

## Limits and testing

LTCT owns its compiler and isolated runtime directly through the standalone Evox C API. It does not depend on EvoxSystem, evox_module, or loading the Evox game plugin; Studio can build recipes with --no-evox. The Evox library sources belong to the engine project; the Evox game plugin contains only the engine integration. Builds expose only recipe natives and language builtins, not game-world bindings. Invalid paths/handles, out-of-bounds pixel access, non-finite stored values, and invalid output channels produce diagnostics. Limits are 16384 per dimension, 16M pixels per image, 32M cumulatively allocated pixels, and 256 images. Texture arrays require at least one layer, allow 256 layer references per build, and have a 32M output-pixel limit, including repeated layers. Scripts remain trusted code: arbitrary loops have no instruction timeout.

`src/tests/texture_recipe_tests.cpp` covers eager execution, in-place operations, independent copies and layout-changing results, typed state, aliasing, image decoding/resizing, channels, texture-array ordering, sampling, transforms, math, filters, generators, deterministic noise, splatter, cubemap validation, and failure recovery. These tests use a standalone filesystem without creating an engine or loading plugins, and compile all three procedural examples. Run the regular engine test suite from `data`. The 2D example is `data/scripts/tests/texture_recipe.ltct`. The ImGui tests `ltct_array_open_close` and `ltct_cube_open_close` verify compiled output, layer/face selectors, and code-editor lifecycle.
