# 3D UI

UI supports rendering UI documents as world-space 3D surfaces.

## Overview

There are two UI rendering paths:

- World-level UI document (existing behavior): rendered in `GAME_VIEW`.
- 3D UI component (`UI3D`): rendered in both `GAME_VIEW` and `SCENE_VIEW`.

3D UI is currently **render-only** (no pointer/raycast interaction).

## Adding 3D UI to an entity

1. Select an entity in the world.
2. Add the `UI / UI3D` component.
3. Set these properties:
   - `Source`: path to a `.ui` document.
   - `Virtual size`: logical canvas size (for example `1000, 1000`).
   - `Orient to camera`: if enabled, the surface faces the active camera.

If `Source` is empty, the component is inactive and renders nothing.

## Property behavior

- `Source`
  - Loads a `.ui` document resource for this entity.
  - Changing the value reloads the document for that component.
- `Virtual size`
  - Controls the document layout space before projection into world space.
  - Larger values give more UI pixels; world transform still controls final world-space scale.
- `Orient to camera`
  - `true`: billboard-like orientation toward the camera while keeping entity position/scale.
  - `false`: uses the entity transform orientation directly.

## Notes and limitations

- 3D UI does not consume UI input events yet.
- The world-level `<world>.ui` flow remains unchanged and works alongside `UI3D` components.
- Save/load persists `UI3D` properties (`Source`, `Virtual size`, `Orient to camera`).
