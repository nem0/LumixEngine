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

The API is `data/scripts/core/procedural_geom.evox`, the same module used for runtime generation (`setMesh`). `main` returns a triangle `Mesh` (topology 0). Paths and point sources (`line`, `circle`, `spiral`, ...) are only inputs for `extrude` and `placeInstancesAtPoints`.

A larger example is `data/scripts/tests/pagoda.lpg` (with `pagoda.ltct` as its texture atlas and `pagoda.mat`): a two tier pavilion with columns, curved tiled roofs generated as height-field grids, fascia and ridge trim, and per-part UV regions of one material. Open it in Studio and edit the constants and dimensions in `main` to explore variations.

## Build

Recipes run in an isolated Evox runtime with no world and no engine bindings, so `setMesh` and `core:entity` functions panic if called. Imported scripts are registered as dependencies, so the model is rebuilt when they change. The output must have 1-1,000,000 vertices and 3-3,000,000 indices (a multiple of 3), all indices must be in range, and all vertex data must be finite.

The mesh is written with position, normal, UV and tangent attributes. Triangles are grouped by material and by level of detail, see below. `withMaterial(mesh, name)` assigns every triangle of a mesh to a material with that name. Names are 1-64 characters without `/`, a backslash, `:` or `.`. Triangles without a material use the default material.

```evox
import "core:procedural_geom"
import "core:vec3"

fn main() : Mesh {
    var body = withMaterial(cube(1.0, 1.0, 1.0), "body");
    var roof = withMaterial(translate(cone(0.8, 0.6, 16), Vec3 { 0.0, 0.5, 0.0 }), "roof");
    return merge(body, roof);
}
```

A complete example with five materials is `data/scripts/tests/house.lpg` (with `house_stone.mat`, `house_walls.mat`, `house_roof.mat`, `house_wood.mat` and `house_glass.mat`).

Each material of each level becomes a model mesh with its own material file. The default material uses `<recipe name>.mat`, a named material uses `<recipe name>_<name>.mat`, for example `house_roof.mat`. These materials use the standard shader and are created next to the recipe if they do not exist yet; edit them to change the look. Parts are not used by `setMesh`, which uploads a single material.

## Studio

Create a *Procedural mesh* asset in the asset browser. Opening it shows the code editor beside a model preview. Saving writes the recipe source and recompiles the model; compile errors go to the log.

Tests: `src/tests/procedural_mesh_tests.cpp`.

## Levels of detail

Each part has a level of detail. `withLod(mesh, lod)` assigns every triangle of a mesh to a level (0 is the most detailed), and keeps the material of each part. Merge meshes of different levels to build several levels in one recipe:

```evox
import "core:procedural_geom"

fn main() : Mesh {
    var high = withLod(cube(1.0, 1.0, 1.0), 0);
    var low = withLod(withMaterial(sphere(0.6, 16, 8), "roof"), 1);
    return merge(high, low);
}
```

LOD levels are set in the meta file `<recipe>.lpg.meta` next to the recipe, a plain text file with the same keys as the model meta:

```
lod_count = 3
lod1_distance = 20
lod2_distance = 50
autolod2 = 0.25
```

- `lod_count` (1-4) is the number of levels. Parts of the recipe must use a level below `lod_count`, and level 0 must have triangles.
- `lodN_distance` is the distance at which the model switches to level N, as for models.
- A level N comes from one of two sources:
  - The recipe has parts with `withLod(..., N)`. It can differ in shape, materials and detail.
  - Otherwise `autolodN` (a simplification ratio, 0-1) generates it from level 0 with mesh simplification. Each material is simplified separately.

A level with neither is a compile error. If the recipe has parts for a level and `autolodN` is also set, the recipe wins.
