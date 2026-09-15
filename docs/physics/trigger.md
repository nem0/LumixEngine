# Triggers

Triggers are special collision shapes that detect when other objects enter or exit their area without physically blocking movement. Any rigid actor can be made a trigger by setting its `Trigger` property in the editor's property grid or programmatically. They're commonly used for:

- Detecting when a player enters a specific zone
- Activating events when objects pass through an area
- Creating invisible boundaries or checkpoints
- Implementing pickup detection for items

## Evox API

Import `core:physics` to poll trigger events produced by the latest physics simulation step. `PhysicsModule.getTriggerHits` returns an iterator over `TriggerHitData` values:

```evox
import "core:physics"
import "core:world"

fn processTriggers(world : World) : void {
	const physics = world.physics() else return;

	for hit in physics.getTriggerHits() {
		if hit.touch_lost {
			// hit.e1 stopped overlapping hit.e2
		}
		else {
			// hit.e1 started overlapping hit.e2
		}
	}
}
```

`hit.e1` is the entity configured as a trigger, `hit.e2` is the other entity, and `hit.touch_lost` distinguishes exit events from enter events. Consume the events each frame; do not retain the iterator.

To enable or disable trigger behavior programmatically, use the rigid actor component:

```evox
import "core:rigid_actor"

fn setTrigger(entity : Entity, enabled : bool) : void {
	const actor = entity.rigid_actor() else return;
	actor.setIsTrigger(enabled);
}
```
