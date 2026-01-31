local math = require "scripts/math"
 
local forward = 0
local backward = 0
local left = 0
local right = 0
local yaw = 0
local pitch = 0
local sprint = 0
local speed_input_idx = -1
local falling_input_idx = -1
local aiming_input_idx = -1
local dir_input_idx = -1
local stance_input_index = -1
local crouched = false
local aiming = false
camera_pivot = black.h.Entity.NULL
handle_input = true

function onInputEvent(event : InputEvent)
	if not handle_input then return end
	
	if event.type == "axis" and event.device.type == "mouse" then
		yaw = yaw + event.x * -0.01
		pitch = pitch + event.y * -0.01
	end
	if event.type == "button" then
		if event.device.type == "mouse" then
			if event.down and event.key_id == 1 then
				aiming = true
			else 
				aiming = false
			end
		end
		if event.device.type == "keyboard" then
			if event.key_id == black.hAPI.Keycode.W then
				if event.down then
					forward = 1
				else
					forward = 0
				end
			end
			if event.key_id == black.hAPI.Keycode.S then
				if event.down then
					backward = 1
				else
					backward = 0
				end
			end
			if event.key_id == black.hAPI.Keycode.C then
				if event.down then
					crouched = not crouched
				end
			end
			if event.key_id == black.hAPI.Keycode.A then
				if event.down then
					left = 1
				else
					left = 0
				end
			end
			if event.key_id == black.hAPI.Keycode.D then
				if event.down then
					right = 1
				else
					right = 0
				end
			end
			if event.key_id == black.hAPI.Keycode.SHIFT then
				if event.down then
					sprint = 1
				else
					sprint = 0
				end
			end
		end		
	end
end

function onControllerHit(obj)
	local a = obj.rigid_actor
	local force = math.mulVec3Num(math.yawToDir(yaw), 50)
	a:applyForce(force)
end

function start()
	speed_input_idx = this.animator:getInputIndex("speed_y")
	dir_input_idx = this.animator:getInputIndex("speed_x")
	stance_input_idx = this.animator:getInputIndex("stance")
	falling_input_idx = this.animator:getInputIndex("is_falling")
	aiming_input_idx = this.animator:getInputIndex("is_aiming")
end

function update(td)
	if speed_input_idx == -1 then 
		speed_input_idx = this.animator:getInputIndex("speed_y")
		dir_input_idx = this.animator:getInputIndex("speed_x")
		stance_input_idx = this.animator:getInputIndex("stance")
		falling_input_idx = this.animator:getInputIndex("is_falling")
		aiming_input_idx = this.animator:getInputIndex("is_aiming")
	end

	if pitch < -0.9 then pitch = -0.9 end
	if pitch > 0.9 then pitch = 0.9 end

	local gravity_speed = this.physical_controller:getGravitySpeed()

	local speed_y = 0
	if forward == 1 then
		speed_y = 3
	end
	if backward == 1 then
		speed_y = -3
	end

	local speed_x = 0;
	if left ~= 0 then
		speed_x = -3
	end
	if right ~= 0 then
		speed_x = 3
	end
	if sprint == 1 then
		speed_x = speed_x * 2
		speed_y = speed_y * 2
	end
	this.animator:setFloatInput(speed_input_idx, speed_y)
	this.animator:setFloatInput(dir_input_idx, speed_x)
	if crouched then
		this.animator:setFloatInput(stance_input_idx, 1)
	else
		this.animator:setFloatInput(stance_input_idx, 0)
	end
	this.animator:setBoolInput(falling_input_idx, gravity_speed < -4)
	this.animator:setBoolInput(aiming_input_idx, aiming)
	local yaw_rot = math.makeQuatFromYaw(yaw)
	local pitch_rot = math.makeQuatFromPitch(pitch)
	this.rotation = yaw_rot
	camera_pivot.rotation = math.mulQuat(yaw_rot, pitch_rot)
end
