# Postprocess scripts

Renderer postprocesses are written in [Evox](evox.md) scripts. The engine's own effects (atmosphere, SSAO, depth of field, bloom with autoexposure, TAA, film grain) are scripts too, they live in `data/scripts/core/postprocess/` and are good examples to copy from.

The API is declared in `data/scripts/core/postprocess.evox`, import it with `import "core:postprocess"`. The native side is in `src/evox/evox_postprocess.cpp`.

## Project script

Every project has a `postprocess.evox` in its root. It is compiled as a second root of `main.evox` and shares its runtime. Studio creates it when you open a project that has none. It defines

```
fn main(registry : Registry) : void
```

which is called whenever the script is (re)loaded and registers effects with `registry.add(effect)`.

```
import "core:postprocess"
import "core:postprocess/defaults" as defaults

fn main(registry : Registry) : void {
	defaults.register(registry);

	var vignette = postprocess();
	vignette.afterTonemap = myVignette;
	registry.add(vignette);
}
```

`defaults.register` adds all engine effects. To change what runs, remove it and call the individual functions of `core:postprocess/defaults` instead (`registerAtmosphere`, `registerAmbientOcclusion`, `registerDepthOfField`, `registerBloom`, `registerAntialiasing`, `registerFilmGrain`). Keep their order, e.g. DOF has to run before bloom.

## Effects and hooks

An effect is a `Postprocess` struct with optional hooks. `postprocess()` returns an effect with no hooks, assign the ones you need. `registry.add` registers a copy of the effect.

| Hook | Signature | When it runs |
|------|-----------|--------------|
| `beforeLightPass` | `fn(Context) : void` | Before lighting, G-buffer is filled. |
| `beforeTransparent` | `fn(Context, RenderBuffer) : RenderBuffer` | Lit opaque scene, before transparent geometry. |
| `antialias` | `fn(Context, RenderBuffer) : RenderBuffer` | Return a valid buffer if antialiasing was done; no other antialiasing runs then. |
| `beforeTonemap` | `fn(Context, RenderBuffer) : RenderBuffer` | HDR color before tonemapping. |
| `tonemap` | `fn(Context, RenderBuffer) : RenderBuffer` | Return a valid buffer if tonemapping was done, it replaces the built-in tonemap. |
| `afterTonemap` | `fn(Context, RenderBuffer) : RenderBuffer` | Final color, before UI. |
| `debugUI` | `fn(Context) : void` | Studio's Debug popup of a view. |
| `debugOutput` | `fn(Context, RenderBuffer) : bool` | While the debug view shows this effect. |

Hooks of the same stage run in registration order, each receiving the output of the previous one. Built-in effects run before scripts, so when built-in TAA is enabled, `antialias` hooks of scripts don't run.

Hooks run for every pipeline (scene view, game view, ...). Use `ctx.pipelineType()` to skip some, e.g. `if ctx.pipelineType() != .GAME_VIEW { return input; }`.

A hook that fails is disabled until the script is reloaded. Global variables are shared by all hooks of all effects and all pipelines.

### Returning buffers

Hooks taking a buffer return the buffer the next stage should use:

* `input` (or an invalid buffer) if the image was modified in place or left as is,
* a different buffer if the effect produced a new one.

Ownership: if a hook returns a new buffer, it also has to release `input` with `releaseBuffer`, except for `tonemap`, where the renderer releases `input` itself. The renderer owns and releases the returned buffer.

## Types

* `Registry`, `Context`, `Shader` - handles that are valid only during the call they were passed to. Do not store them.
* `RenderBuffer` - a GPU image (`handle`, zero is invalid). `invalidBuffer()` and `buffer.isValid()` help with that.
* `Storage` - a GPU buffer (e.g. a histogram), zero `id` is invalid.

## Context API

Functions are called with the context as the first argument, e.g. `ctx.width()`.

### Pipeline info

| Function | Description |
|----------|-------------|
| `input(ctx)` | Color buffer entering this stage. It does not change while the effects of a stage run, pass results of previous effects explicitly. |
| `depth(ctx)` | Scene depth. Not available in `tonemap`. |
| `gbufferA/B/C/D(ctx)` | G-buffer textures. Not available in `tonemap`. Built-in effects use B for normals (and ambient occlusion), C for screen space shadowing (in `.w`) and D for motion vectors. Write to them using `rwBindless`. |
| `renderModule(ctx)` | The `RenderModule` of the scene. |
| `pipelineType(ctx)` | Scene view, game view, ... |
| `width(ctx)`, `height(ctx)` | Viewport (render) size in pixels. |
| `displayWidth(ctx)`, `displayHeight(ctx)` | Size of the image the pipeline presents, what upscalers and TAA work with. |
| `enablePixelJitter(ctx, bool)` | Shifts the camera by a sub-pixel offset each frame, as TAA needs. Applies from the next frame, so an `antialias` hook that isn't running should switch it off. |
| `builtinTaaEnabled(ctx)` | False while TAA is off, e.g. because an upscaler (DLSS, FSR3) does the antialiasing. |

### Buffers

| Function | Description |
|----------|-------------|
| `createBuffer(ctx, w, h, format)` | Buffer that compute shaders can read and write. Depth, compressed, sRGB and BGRA formats are not supported. |
| `createRenderTarget(ctx, w, h, format)` | Like `createBuffer`, but can also be a render target (for `clear`, `setRenderTarget`, `draw`). Use it for buffers the renderer draws into after your hook, e.g. what `tonemap` returns. Some formats (R32F) don't work as render targets. |
| `releaseBuffer(ctx, buffer)` | Releases a buffer. |
| `clear(ctx, buffer, r, g, b, a)` | Clears a render target. |
| `blit(ctx, src, dst, w, h)` | Copies (and converts the format of) a region. |
| `createStorage(ctx, size)` | GPU buffer of `size` bytes, contents are undefined until written. |
| `releaseStorage(ctx, storage)` | Releases a storage. |

Formats are values of `TextureFormat` from `core:renderer/gpu/textureformat`, e.g. `.RGBA16F`, `.RGBA8`.

### Shaders and drawing

```
const shader = ctx.loadShader("engine/shaders/film_grain.hlsl");
if not shader.isReady() {
	return input;
}
ctx.dispatch(shader, MyUniform {intensity, ctx.rwBindless(input)}, (ctx.width() + 15) / 16, (ctx.height() + 15) / 16, 1);
```

* `loadShader(ctx, path)` - loaded once and cached, `path` is relative to the project. `isReady(shader)` is false until it's compiled; dispatch and draw do nothing until then, so check it and return `input` meanwhile.
* `dispatch(ctx, shader, uniform, x, y, z)` - dispatches a compute shader. `uniform` is any struct, it's copied to the shader's `Drawcall` constant buffer (b4), so its layout must match the `cbuffer` in HLSL.
* `dispatchDefine(ctx, shader, define, uniform, x, y, z)` - same, but `define` selects a variant of the shader (`""` for none).
* `setRenderTarget(ctx, buffer)` - sets the target for following `draw` calls, `buffer` must come from `createRenderTarget`.
* `draw(ctx, shader, uniform)` / `drawDefine(ctx, shader, define, uniform)` - draws a fullscreen triangle. The shader has to be a surface shader (`//@surface`) with `mainVS` and `mainPS`.
* `beginBlock(ctx, name)` / `endBlock(ctx)` - profiler / graphics debugger scope.

### Bindless handles

Shaders access buffers through bindless handles that you put into the uniform struct (as `u32`):

| Function | Use |
|----------|-----|
| `bindless(ctx, buffer)` | Read a render buffer. |
| `rwBindless(ctx, buffer)` | Write a render buffer. |
| `bindlessStorage(ctx, storage)` | Read a storage. |
| `rwBindlessStorage(ctx, storage)` | Write a storage. |

The renderer inserts memory barriers for buffers whose `rwBindless` handle was requested before a dispatch, so request it again for every dispatch that writes the buffer.

## Per pipeline state

```
struct MyState {
	history : RenderBuffer;
	counter : u32;
}

fn myHook(ctx : Context, input : RenderBuffer) : RenderBuffer {
	var state = ctx.state(MyState);
	state.counter = state.counter + 1;
	...
}
```

`ctx.state(T)` returns a pointer to memory of type `T` that belongs to the pipeline the hook runs in. It is zero-initialized the first time and keeps its value between frames, so it's the place for history buffers and other per-pipeline data (globals are shared by all pipelines). `RenderBuffer`s and `Storage`s in it (directly or nested in structs/arrays) are released automatically when the pipeline is destroyed or the script reloaded. Do not keep the pointer after the hook returns.

## Debug view

Studio's Debug popup of a view lists the effects with a `debugUI` hook and lets the user show their buffers instead of the usual output.

* `debugUI` - draw ImGui widgets (`core:imgui`) editing the effect's settings, and call `ctx.debugRadio(name)` for each buffer that can be shown. The context has no input and no G-buffer here. Do not create or release buffers.
* `debugOutput` - called while the debug view shows this effect. `output` is the image of the pipeline. If `ctx.isDebugShown(name)` is true for one of your buffers, `blit` it to `output` and return `true`, otherwise return `false`.

`showDebug(ctx, name)` and `isDebugShown(ctx, name)` are the lower level functions behind `debugRadio`. The debug selection is per pipeline, names only need to be unique among effects. See `bloom.evox` for a complete example, where the buffer is kept in `state` while shown.

## Example: a custom effect

HLSL (`shaders/invert.hlsl`):

```hlsl
//@compute
#include "shaders/common.hlsli"   // adjust to the includes your shader needs

cbuffer Drawcall : register(b4) {
	float amount;
	uint image;
};

[numthreads(16, 16, 1)]
void main(uint3 thread_id : SV_DispatchThreadID) {
	RWTexture2D<float4> rw = rwtex2D<float4>(image);
	float4 c = rw[thread_id.xy];
	rw[thread_id.xy] = float4(lerp(c.rgb, 1 - c.rgb, amount), c.a);
}
```

Evox (`postprocess.evox`):

```
import "core:postprocess"
import "core:postprocess/defaults" as defaults

struct InvertUniform {
	amount : f32;
	image : u32;
}

fn invert(ctx : Context, input : RenderBuffer) : RenderBuffer {
	if ctx.pipelineType() != .GAME_VIEW {
		return input;
	}
	const shader = ctx.loadShader("shaders/invert.hlsl");
	if not shader.isReady() {
		return input;
	}
	ctx.beginBlock("invert");
	ctx.dispatch(shader, InvertUniform {1.0, ctx.rwBindless(input)}, (ctx.width() + 15) / 16, (ctx.height() + 15) / 16, 1);
	ctx.endBlock();
	return input;
}

fn main(registry : Registry) : void {
	defaults.register(registry);

	var effect = postprocess();
	effect.afterTonemap = invert;
	registry.add(effect);
}
```

The HLSL above is a sketch; look at the shaders in `data/engine/shaders` (e.g. `film_grain.hlsl`) for the exact includes and helpers to use.

## Engine effects

| Effect | Hooks | File |
|--------|-------|------|
| Atmosphere | `beforeTransparent` | `core/postprocess/atmo.evox` |
| SSAO | `beforeLightPass`, `debugUI` | `core/postprocess/ssao.evox` |
| Depth of field | `beforeTonemap` | `core/postprocess/dof.evox` |
| Bloom + autoexposure | `beforeTonemap`, `tonemap`, `debugUI`, `debugOutput` | `core/postprocess/bloom.evox` |
| TAA | `antialias`, `debugUI` | `core/postprocess/taa.evox` |
| Film grain | `afterTonemap` | `core/postprocess/film_grain.evox` |

Bloom, film grain and others are driven by properties of the active camera and run in the game view only.
