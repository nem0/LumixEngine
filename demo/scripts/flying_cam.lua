local lmath = require "scripts/math"
-- add to entity with a camera component
-- behaves similar to camera in scene view

local yaw = 0
local pitch = 0
local forward = 0
local dyaw = 0
local dpitch = 0
local rmb_down = 0


function update(dt)
    yaw = yaw + dyaw * dt
    pitch = pitch + dpitch * dt
    dyaw = 0
    dpitch = 0

    if pitch > 1 then pitch = 1 end
    if pitch < -1 then pitch = -1 end

    local dir = { math.sin(yaw), 0, math.cos(yaw) }
    local pos = lmath.addVec3(this.position, lmath.mulVec3(dir, -forward * dt))
    this.position = pos
    local yaw_quat = { 0, math.sin(yaw * 0.5), 0, math.cos(yaw * 0.5) }
    local pitch_quat = { math.sin(pitch * 0.5), 0, 0, math.cos(pitch * 0.5) }
    this.rotation = lmath.mulQuat(yaw_quat, pitch_quat)
end

function onInputEvent(event : InputEvent)
	if event.type == "button" then
        if event.device.type == "mouse" and event.key_id > 0 then
            rmb_down = event.down
            --Gui.enableCursor(not event.down)

        elseif event.device.type == "keyboard" then
			if event.key_id == string.byte("W") then
                if event.down then
                    forward = 1
                else 
                    forward = 0
                end
			end
			if event.key_id == string.byte("S") then
                if event.down then
                    forward = -1
                else 
                    forward = 0
                end
			end
		end		
	elseif event.type == "axis" then
		if event.device.type == "mouse" and rmb_down then
			dyaw = dyaw + event.x * -0.1;
			dpitch = dpitch + event.y * -0.1;
		end
	end
end
