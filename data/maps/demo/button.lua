local co = require "scripts/coroutine"
local lumix_math = require "scripts/math"
local interactive = false

label = {}
player = {}
cubeA = {}
cubeB = {}
sphereA = {}
sphereB = {}
sound = -1
local ik_target_input = -1
local ik_alpha_input = -1
local ik_co = nil
local cubeA_pos = {}
local cubeB_pos = {}
local sphereA_pos = {}
local sphereB_pos = {}

Editor.setPropertyType(this, "label", Editor.ENTITY_PROPERTY)
Editor.setPropertyType(this, "player", Editor.ENTITY_PROPERTY)
Editor.setPropertyType(this, "cubeA", Editor.ENTITY_PROPERTY)
Editor.setPropertyType(this, "cubeB", Editor.ENTITY_PROPERTY)
Editor.setPropertyType(this, "sphereA", Editor.ENTITY_PROPERTY)
Editor.setPropertyType(this, "sphereB", Editor.ENTITY_PROPERTY)
Editor.setPropertyType(this, "sound", Editor.RESOURCE_PROPERTY, "clip")

function start()
	cubeA_pos = cubeA.position
	cubeB_pos = cubeB.position
	sphereA_pos = sphereA.position
	sphereB_pos = sphereB.position
end

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
				ik_target_input = player.animator:getInputIndex("left_hand_ik_target")
				ik_alpha_input = player.animator:getInputIndex("left_hand_ik_alpha")
				
				player.animator:setVec3Input(ik_target_input, calcIKTarget())
				co.run(function()
					co.parallel(
						-- move hand to the button
						function() co.lerpAnimatorFloat(player, ik_alpha_input, 0, 1, 0.3) end,
						-- wait a bit and then press the button
						function() 
							co.wait(0.2)
							co.lerpVec3(this, "local_position", {0, 0, 0}, {0, 0, 0.1}, 0.1)
						end
					)
					-- reset objects' positions
					cubeA.position = cubeA_pos
					cubeB.position = cubeB_pos
					sphereA.position = sphereA_pos
					sphereB.position = sphereB_pos
					-- play a sound and wait a bit
					playSound(sound)
					co.wait(0.1)
					co.parallel(	
						-- move hand back
						function() co.lerpAnimatorFloat(player, ik_alpha_input, 1, 0, 0.3) end,
						-- release the button
						function() co.lerpVec3(this, "local_position", {0, 0, 0.1}, {0, 0, 0}, 0.1) end
					)
					return false
				end)
			end
		end
	end
end
