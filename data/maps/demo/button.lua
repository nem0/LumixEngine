local lumix_math = require "scripts/math"
local interactive = false

label = {}
player = {}
sound = -1
local ik_target_input = -1
local ik_alpha_input = -1
local ik_co = nil

Editor.setPropertyType(this, "label", Editor.ENTITY_PROPERTY)
Editor.setPropertyType(this, "player", Editor.ENTITY_PROPERTY)
Editor.setPropertyType(this, "sound", Editor.RESOURCE_PROPERTY, "clip")

function playSound(sound)
	local path = this.world.lua_script:getResourcePath(sound)
	this.world:getModule("audio"):play(this, path, false)
end

function update(time_delta)
	-- check if player is close
	local dist_squared = lumix_math.distXZSquared(this.position, player.position)	
	interactive = dist_squared < 2
	-- animate the label if player is close
	label.property_animator.enabled = interactive

	if ik_co then
		local status, res = coroutine.resume(ik_co, time_delta)
		if not status then
			LumixAPI.logError("coroutine error: " .. res)
			ik_co = nil
		end
		if not res then
			ik_co = nil
		end
	end
end

function easeOutBack(x: number): number
	local c1 = 1.70158;
	local c3 = c1 + 1;
	
	return 1 + c3 * math.pow(x - 1, 3) + c1 * math.pow(x - 1, 2);
end

function calcIKTarget()
	local button_pos = this.position
	local player_pos = player.position
	button_pos = lumix_math.subVec3(button_pos, player_pos) -- relative to player_pos
	local player_rot = player.rotation
	player_rot[4] = -player_rot[4] --invert rotation
	button_pos = lumix_math.transformVec3(player_rot, button_pos) --transform to player space
	return button_pos
end

function onInputEvent(event : InputEvent)
	-- check if player pressed "F" and is close
	if interactive and event.type == "button" and event.device.type == "keyboard" then
		if event.key_id == string.byte("F") then
			if event.down then
				-- start by moving the hand to button
				ik_target_input = player.animator:getInputIndex("left_hand_ik_target")
				ik_alpha_input = player.animator:getInputIndex("left_hand_ik_alpha")
				local ik_time = 0
				
				ik_co = coroutine.create(function()
					local td = 0
					while ik_time < 1 do
						player.animator:setVec3Input(ik_target_input, calcIKTarget())
						if ik_time < 0.2 then
							if ik_time + td >= 0.2 then
								-- play the sound
								playSound(sound)
								-- press (move) the button
								this.property_animator.enabled = false
								this.property_animator.enabled = true
							end
							player.animator:setFloatInput(ik_alpha_input, easeOutBack(ik_time / 0.2))
						else 
							player.animator:setFloatInput(ik_alpha_input, 1 - easeOutBack((ik_time - 0.2) / 0.8))
						end
						td = coroutine.yield(true)
						ik_time = ik_time + td
					end
					return false
				end)

			end
		end
	end
end
