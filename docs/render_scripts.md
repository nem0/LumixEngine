# Render scripts

The 3D frame of the renderer and its postprocesses are written in [Evox](evox.md) scripts. A project's `render.evox` renders the frame (`data/scripts/core/render/`), calling the engine's own effects (atmosphere, SSAO, depth of field, bloom with autoexposure, TAA, film grain), which are scripts too and live in `data/scripts/core/postprocess/`. They are good examples to copy from.

The API is declared in `data/scripts/core/render.evox`, import it with `import "core:render"`. The native side is in `src/renderer/evox_render.cpp`.

## Project script

Every project has a `render.evox` in its root. It is compiled as a second root of `main.evox` and shares its runtime. Studio creates it when you open a project that has none. It defines

```
fn main(ctx : Context) : RenderBuffer
fn debugUI(ctx : Context) : void       // optional
```

`main` is called for every pipeline (scene view, game view, ...) each frame. It renders the whole 3D frame and returns the final image the pipeline presents, calling the postprocesses directly. `debugUI` is called by Studio's Debug popup of a view and draws ImGui widgets of the effects' settings. A function that fails is disabled until the script is reloaded.

```
import "core:render"
import "core:render/frame" as frame

fn main(ctx : Context) : RenderBuffer {
	return frame.renderFrame(ctx);
}

fn debugUI(ctx : Context) : void {
	frame.debugUI(ctx);
}
```

`frame.renderFrame` (`core/render/frame.evox`) is the engine's default frame: it runs the stage scripts (shadows, geometry, lighting, transparent, debug views, tonemap), the C++ plugins (DLSS, FSR3, ...) and the default effects, in order. To change what runs, copy `frame.evox` into your project, edit it (remove an effect, add your own call between two stages, ...) and call your copy from `main`. Keep the order, e.g. DOF has to run before bloom.

## Writing an effect

An effect is a plain function that `renderFrame` (or your copy of it) calls at the stage it belongs to.

| Stage | Signature | When it runs |
|-------|-----------|--------------|
| before lighting | `fn(Context) : void` | G-buffer is filled, e.g. SSAO. |
| before transparent | `fn(Context, RenderBuffer) : RenderBuffer` | Lit opaque scene, before transparent geometry. |
| antialias | `fn(Context, RenderBuffer) : RenderBuffer` | Return a valid buffer if antialiasing was done; no other antialiasing runs then. |
| before tonemap | `fn(Context, RenderBuffer) : RenderBuffer` | HDR color before tonemapping. |
| tonemap | `fn(Context, RenderBuffer) : RenderBuffer` | Return a valid buffer if tonemapping was done, it replaces the default tonemap. |
| after tonemap | `fn(Context, RenderBuffer) : RenderBuffer` | Final color, before UI. |
| `debugUI` | `fn(Context) : void` | Studio's Debug popup of a view. |
| debug output | `fn(Context, RenderBuffer) : bool` | While the debug view shows this effect. |

Effects run for every pipeline. Use `ctx.pipelineType()` to skip some, e.g. `if ctx.pipelineType() != .GAME_VIEW { return input; }`. Global variables are shared by all effects and all pipelines.

### Returning buffers

Effects taking a buffer return the buffer the next stage should use:

* `input` (or an invalid buffer) if the image was modified in place or left as is,
* a different buffer if the effect produced a new one.

Ownership: if an effect returns a new buffer, it also has to release `input` with `releaseBuffer`, except for `tonemap`, where the frame releases `input` itself. The frame owns and releases the returned buffer.

## Types

* `Context`, `Shader` - handles that are valid only during the call they were passed to. Do not store them.
* `RenderBuffer` - a GPU image (`handle`, zero is invalid). `invalidBuffer()` and `buffer.isValid()` help with that.
* `Storage` - a GPU buffer (e.g. a histogram), zero `id` is invalid.

## Context API

Functions are called with the context as the first argument, e.g. `ctx.width()`.

### Pipeline info

| Function | Description |
|----------|-------------|
| `depth(ctx)` | Scene depth set by `setGBuffer`, available during tonemapping too. Unavailable in `debugUI`. |
| `gbufferA/B/C/D(ctx)` | G-buffer textures set by `setGBuffer`, unavailable in `debugUI`. Built-in effects use B for normals (and ambient occlusion), C for screen space shadowing (in `.w`) and D for motion vectors. Write to them using `rwBindless`. |
| `renderModule(ctx)` | The `RenderModule` of the scene. |
| `pipelineType(ctx)` | Scene view, game view, ... |
| `width(ctx)`, `height(ctx)` | Viewport (render) size in pixels. |
| `displayWidth(ctx)`, `displayHeight(ctx)` | Size of the image the pipeline presents, what upscalers and TAA work with. |
| `enablePixelJitter(ctx, bool)` | Shifts the camera by a sub-pixel offset each frame, as TAA needs. Applies from the next frame, so an antialiasing effect that isn't running should switch it off. |
| `builtinTaaEnabled(ctx)` | False while TAA is off, e.g. because an upscaler (DLSS, FSR3) does the antialiasing. |

### Buffers

| Function | Description |
|----------|-------------|
| `createBuffer(ctx, w, h, format)` | Buffer that compute shaders can read and write. Depth, compressed, sRGB and BGRA formats are not supported. |
| `createRenderTarget(ctx, w, h, format)` | Like `createBuffer`, but can also be a render target (for `clear`, `setRenderTarget`, `draw`). Use it for buffers the frame draws into after your effect, e.g. what tonemapping returns. Some formats (R32F) don't work as render targets. |
| `releaseBuffer(ctx, buffer)` | Releases a buffer. |
| `clear(ctx, buffer, r, g, b, a)` | Clears a render target. |
| `blit(ctx, src, dst, w, h)` | Copies (and converts the format of) a region. |
| `setRenderTargetWithDepth(ctx, buffer, depth, flags)` | Like `setRenderTarget` with a depth buffer (e.g. `ctx.depth()`) and `FramebufferFlags` (`.READONLY_DEPTH`, ...). |
| `clearTargets(ctx, flags, r, g, b, a, depth)` | Clears the current targets, `flags` is `ClearFlags`. |
| `clearColor(ctx)` | Color the pipeline clears its HDR image with. |
| `mainCamera(ctx)`, `pass(ctx, camera)` | A handle to the pipeline camera (valid during the script call) and the call that sets its matrices for following draws. |
| `viewport(ctx, x, y, w, h)` | Sets the viewport. |
| `setRenderTargets(ctx, colors, depth, flags)` | Up to 4 color targets and an optional depth buffer. |
| `renderBucket(ctx, view, bucket)` | Draws a bucket of the view returned by `cull`. |
| `addBucket(ctx, layer, define, sort, state)`, `cull(ctx, camera)` | Add buckets (render object layers with a state and shader define), then cull the scene for the camera into them, `cull` returns the view for `renderBucket`. |
| `shadowCamera(ctx, slice)`, `shadowsEnabled(ctx)` | Camera of a shadow cascade (0..3), and whether the active environment casts shadows. |
| `renderGrass(ctx, camera, state, defines)`, `renderTerrains(ctx, camera, state, define)` | Draw grass and terrains. |
| `renderEntities(ctx, state, define, entities)` | Draw the meshes of only the given `Entity` values (model instances; others are skipped) into the current targets, e.g. a depth-only mask with `"DEPTH"` for an outline. The rest of the scene is not drawn. |
| `createDepthBuffer(ctx, w, h)`, `setDepthTarget(ctx, buffer)`, `barrierRead(ctx, buffer)` | D32 depth buffer, rendering to it only, and a read barrier. |
| `createRenderbuffer(ctx, w, h, format, flags)` | Buffer of any format, `flags` are `TextureFlags` (add them with `textureFlags`). |
| `blendState(src_rgb, dst_rgb, src_a, dst_a)` | State bits that set up blending, for buckets and draws. |
| `renderOpaquePlugins(ctx)` | Lets C++ plugins draw their opaque geometry. |
| `debugShow(ctx)`, `copyChannels(ctx, source, output, w, h, r, g, b)` | Selected built-in debug view, and a channel-mask copy between buffers. |
| `beginFrame(ctx)`, `bindGlobalState(ctx, shadowmap)`, `endFrame(ctx)` | The frame setup and teardown that `main` of `render.evox` has to call (sort keys, light clusters, global uniforms). |
| `setGBuffer(ctx, gbuffer)` | Sets the G-buffer that effects called from the frame script see as `gbuffer()` and `depth()`. |
| `runPlugins(ctx, stage, input)`, `runPluginsDebugOutput(ctx, output)` | The C++ plugins of a stage (`PluginStage`). |
| `renderDebugShapes(ctx, output, depth)`, `render2D(ctx, output)` | Debug shapes and UI of the frame. |
| `beginStatsBlock(ctx, name)` | `beginBlock` that also collects draw statistics. |
| `setUniform(ctx, slot, data)` | Binds data to `.DRAWCALL` (b4) or `.DRAWCALL2` (b5). |
| `renderTransparentPlugins(ctx)` | Lets C++ plugins draw their transparent geometry. |
| `shadowAtlasBindless(ctx)`, `reflectionProbesBindless(ctx)` | Bindless handles of the shadow atlas and reflection probes. |
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
* `drawState(ctx, shader, state, uniform)` - like `draw` with render state, e.g. `stencilState(write_mask, func, ref, mask, sfail, dpfail, dppass)`.
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

fn myEffect(ctx : Context, input : RenderBuffer) : RenderBuffer {
	var state = ctx.state(MyState);
	state.counter = state.counter + 1;
	...
}
```

`ctx.state(T)` returns a pointer to memory of type `T` that belongs to the pipeline the script runs in. It is zero-initialized the first time and keeps its value between frames, so it's the place for history buffers and other per-pipeline data (globals are shared by all pipelines). `RenderBuffer`s and `Storage`s in it (directly or nested in structs/arrays) are released automatically when the pipeline is destroyed or the script reloaded. Do not keep the pointer after the script call returns.

## Debug view

Studio's Debug popup of a view calls `debugUI` of `render.evox`, where effects draw their settings and offer their buffers with `debugRadio`; the user can show them instead of the usual output (see `bloom.debugOutput`).

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

Evox (`invert.evox` in the project):

```
import "core:render"

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
```

Call it from your copy of `frame.evox`, after the tonemap stage:

```
result = apply(ctx, invert.invert, result);
```

The HLSL above is a sketch; look at the shaders in `data/engine/shaders` (e.g. `film_grain.hlsl`) for the exact includes and helpers to use.

## Engine effects

| Effect | Stage | File |
|--------|-------|------|
| Atmosphere | `beforeTransparent` | `core/postprocess/atmo.evox` |
| Frame | `main` of `render.evox` | `core/render/frame.evox` (stages in `shadow`, `geom`, `lighting`, `transparent`, `debug_view`, `tonemap` next to it) |
| SSAO | `beforeLightPass`, `debugUI` | `core/postprocess/ssao.evox` |
| Depth of field | `beforeTonemap` | `core/postprocess/dof.evox` |
| Bloom + autoexposure | `beforeTonemap`, `tonemap`, `debugUI`, `debugOutput` | `core/postprocess/bloom.evox` |
| TAA | `antialias`, `debugUI` | `core/postprocess/taa.evox` |
| Film grain | `afterTonemap` | `core/postprocess/film_grain.evox` |

Bloom, film grain and others are driven by properties of the active camera and run in the game view only.
