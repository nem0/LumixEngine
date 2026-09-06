# Triggers

Triggers are special collision shapes that detect when other objects enter or exit their area without physically blocking movement. Any rigid actor can be made a trigger by setting its `Trigger` property in the editor's property grid or programmatically. They're commonly used for:

- Detecting when a player enters a specific zone
- Activating events when objects pass through an area
- Creating invisible boundaries or checkpoints
- Implementing pickup detection for items

Trigger events can be polled each frame using the physics module's polling functions. Call `getNumTriggerHits()` to get the number of trigger events for the current frame, then use `getTriggerHit()` to retrieve each event:

```lua
import "core:physics" as physics

function update()
	let num_hits = physics.getNumTriggerHits()
	for i = 0..num_hits {
		let hit = physics.getTriggerHit(i)
		let entity1 = hit.e1
		let entity2 = hit.e2
		let touch_lost = hit.touch_lost
		
		if touch_lost {
			-- Entity exited the trigger
		} else {
			-- Entity entered the trigger
		}
	}
end
```