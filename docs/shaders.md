# Shaders

Shaders can be found in the [data/shaders](../data/shaders) directory and are written in [HLSL](https://learn.microsoft.com/en-us/windows/win32/direct3dhlsl/dx-graphics-hlsl). We support pixel, vertex and compute shaders. Shaders are compile with **FXC compiler** distributed with OS. Shader are compiled with **Shader model 5.1.**. Shader files must have `.hlsl` extension. Shaders can be edited by integrated shader editor or any text editor.

## Vertex and pixel shader

Vertex shader entry point must be named `mainVS` and pixel shader entry point must be named `mainPS`. Both can be in a single HLSL file. To distinguist vertex + pixel from compute shaders, shader compiler looks for `//@surface` directive.

```hlsl
//@surface

float4 mainVS() : SV_Position {
    ...
}

float4 mainPS() :SV_Target {
    ...
}
```

## Compute shader

If there's **no** `//@surface` directive, the HLSL file is considered as a compute shader. Compute shader entry point must be named `main`.

```hlsl
#include "shaders/common.hlsli"

[numthreads(16, 16, 1)]
void main(uint3 thread_id : SV_DispatchThreadID) {
    ...
}
```

## Includes

HLSL files can include another files with standard include directive. Included files must have `.hlsli` extension to be recognized by the editor - i.e. so they can be visible in asset browser and editable.

```hlsl
#include "shaders/common.hlsli"
```

## Directives

We support specific directives within HLSL source code. These directives begin with `//@` and must be the only content on the line, aside from any leading whitespace. For details on how these directives are parsed, refer to `ShaderPlugin::compile`. Directives serve as an interface for materials. They also inform the shader compiler whether the HLSL files contain a compute shader or a vertex + pixel shader.

```hlsl
// this is just a comment
// this is also just a comment //@surface
void foo() {} //@surface <- this is also not considered a directive

// but the following lines are valid directives
//@surface
    //@define SOME_DEFINE
```

## Texture slots

Texture slots can be defined using the `//@texture_slot` directive. Textures can be assigned to these slots in the material editor UI. The directive has three arguments, with the last one being optional:
* **name** - a readable name used in the material editor UI. It's also automatically converted to an HLSL variable name by using the prefix `t_`, replacing invalid characters with `_`, and converting to lower case.
* **default_value** - a texture that is used if the material does not assign any texture to the slot.
* **define** - an optional argument. If provided, the define is activated when a texture is assigned to the slot.

```hlsl
//@texture_slot "Albedo", "textures/common/white.tga"
//@texture_slot "Metallic", "", "HAS_METALLICMAP"

float4 mainPS(VSOutput input) : SV_Target {
    ...
    float3 albedo = sampleBindless(LinearSampler, t_albedo, input.uv).rgb;
    ...
    #ifdef HAS_METALLICMAP
        float metallic = sampleBindless(LinearSampler, t_metallic, input.uv).rgb;
    #else
        float metallic = 0;
    #endif
}
```

## Uniforms

Multiple uniforms can be defined in shaders using the `//@uniform name, type, default_value` directive. All uniforms defined this way are placed in a single constant buffer (see `Material::updateRenderData`). Values of these uniforms can be set in materials. The `//@uniform` directive has three arguments:

* **name** - readable name, it's used in material editor UI. It's also automatically converted to HLSL variable by using `u_` prefix, replacing invalid characters (e.g. spaces) with `_` and using lower case.
* **type** - type of the uniform, can be one of `normalized_float`, `float`, `int`, `color`, `float2`, `float3` or `float4`.
* **default_value** - default value is used when material does not set any value for a uniform.

```hlsl
//@uniform "Material color", "color", {1, 1, 1, 1}

float4 mainPS() : SV_Target {
    return u_material_color;
}
```

## Defines

`//@define` directive allow enabling or disabling parts of a shader from the material editor UI, where they appear as checkboxes. Additionally, `#define` directives can be used directly within the shader code.

```hlsl
//@define "ALPHA_CUTOUT"

#define ALPHA_CUTOUT_VALUE 0.5 // this is fixed, not exposed in UI in any way

float4 mainPS() : SV_Target {
    ...
    // whether ALPHA_CUTOUT is defined or not depends on a material
    #ifdef ALPHA_CUTOUT
        if (surface.alpha < ALPHA_CUTOUT_VALUE) discard;
    #endif
    ...
}
```

## Related functions

* `Material::updateRenderData` - Updates material constant buffer.
* `DrawStream::createProgram` - Adds some preamble to shaders.
* `ShaderPlugin::compile` - Handle includes and directives. Outputs HLSL source code.
* `ShaderCompiler::compileStage` - Invokes **FXC** to compile HLSL to bytecode.