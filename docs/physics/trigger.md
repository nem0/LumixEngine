# Triggers

Triggers are special collision shapes that detect when other objects enter or exit their area without physically blocking movement. Any rigid actor can be made a trigger by setting its `Trigger` property in the editor's property grid or programmatically. They're commonly used for:

- Detecting when a player enters a specific zone
- Activating events when objects pass through an area
- Creating invisible boundaries or checkpoints
- Implementing pickup detection for items

When an object enters or exits a trigger, the `onTrigger` function is called with the entity that triggered the event and a boolean indicating whether contact was lost.

```lua
function onTrigger(entity, touch_lost)
	black.hAPI.logError("trigger")
	black.hAPI.logError(entity.name)
end
```