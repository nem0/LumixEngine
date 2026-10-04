# Evox procedural mesh recipes (.lpg)

`.lpg` compiles an ordinary Evox program to a regular **Model** resource, the same way `.ltct` compiles to a texture. The result can be used anywhere a model can (model instances, prefabs, LODs, culling), unlike geometry generated at runtime into a procedural geometry component.

```evox
import "core:procedural_geom"
import "core:vec3"

fn main() : Mesh {
    var body = cube(1.0, 1.0, 1.0);
    var top = translate(sphere(0.5, 32, 16), Vec3 { 0.0, 0.75, 0.0 });
    return merge(body, top);
}
```

The API is `data/scripts/core/procedural_geom.evox`, the same module used for runtime generation (`setMesh`). `main` must return a triangle `Mesh` (topology 0); paths and point sources (`line`, `circle`, `spiral`, ...) are only inputs for `extrude` and `placeInstancesAtPoints`.

A larger example is `data/scripts/tests/pagoda.lpg` (with `pagoda.ltct` as its texture atlas and `pagoda.mat`): a two tier pavilion with columns, curved tiled roofs generated as height-field grids, fascia and ridge trim, and per-part UV regions of one material. Open it in Studio and edit the constants and dimensions in `main` to explore variations.

## Build

Recipes run in an isolated Evox runtime with no world and no engine bindings, so `setMesh` and `core:entity` functions panic if called. Imported scripts are registered as dependencies, so the model is rebuilt when they change. The output must have 1-1,000,000 vertices and 3-3,000,000 indices (a multiple of 3), all indices must be in range, and all vertex data must be finite.

The mesh is written as one mesh with position, normal, UV and tangent attributes. A material `<recipe name>.mat` using the standard shader is created next to the recipe if it does not exist yet; edit it to change the look.

## Studio

Create a *Procedural mesh* asset in the asset browser. Opening it shows the code editor beside a model preview. Saving writes the recipe source and recompiles the model; compile errors go to the log.

Tests: `src/tests/procedural_mesh_tests.cpp`.
