# Extending Property Grid

Properties exposed to [reflection](../../src/engine/reflection.h) are automatically visible in the property grid. Users can extend the property grid to show data not known to the reflection system by implementing `PropertyGrid::IPlugin`.

There are two ways to add functionality to the property grid:

## onGUI

```cpp
void IPlugin::onGUI(PropertyGrid& grid, Span<const EntityRef> entities, ComponentType cmp_type, const TextFilter& filter, WorldEditor& editor);
```

`IPlugin::onGUI` is called at the end of every component in the property grid.

## Blob Properties

While the reflection system does recognize blob properties, it treats them as an opaque block of data. The system doesn't attempt to understand or display their internal structure, leaving it entirely up to the plugin to interpret and render the content meaningfully. This is used in the Lua plugin to display [properties](../lua/properties.md) defined in Lua scripts.

```cpp
void IPlugin::blobGUI(PropertyGrid& grid, Span<const EntityRef> entities, ComponentType cmp_type, u32 array_index, const TextFilter& filter, WorldEditor& editor);
```

`IPlugin::blobGUI` is called for every blob property in every component in the property grid.