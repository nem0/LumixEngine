local math = require "scripts/math"
local interactive = false

label = {}
player = {}
sound = -1

Editor.setPropertyType(this, "label", Editor.ENTITY_PROPERTY)
Editor.setPropertyType(this, "player", Editor.ENTITY_PROPERTY)
Editor.setPropertyType(this, "sound", Editor.RESOURCE_PROPERTY, "clip")

function playSound(sound)
	local path = this.world.lua_script:getResourcePath(sound)
	this.world:getModule("audio"):play(this, path, false)
end

function update(time_delta)
	-- check if player is close
	local dist_squared = math.distXZSquared(this.position, player.position)	
	interactive = dist_squared < 2
	-- animate the label if player is close
	label.property_animator.enabled = interactive
end

function onInputEvent(event : InputEvent)
    -- check if player pressed "F" and is close
	if interactive and event.type == "button" and event.device.type == "keyboard" then
		if event.key_id == string.byte("F") then
	        if event.down then
     	       -- play the sound
				playSound(sound)
				-- press (move) the button
				this.property_animator.enabled = false
				this.property_animator.enabled = true
            end
		end
	end
end
