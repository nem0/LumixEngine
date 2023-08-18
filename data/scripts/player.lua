local forward = 0
local backward = 0
local left = 0
local right = 0
local yaw = 0
local pitch = 0
local sprint = 0
local speed_input_idx = -1
local crouched_input_idx = -1
local falling_input_idx = -1
local aiming_input_idx = -1
local dir_input_idx = -1
local crouched = false
local aiming = false
camera_pivot = -1
Editor.setPropertyType(this, "camera_pivot", Editor.ENTITY_PROPERTY)

function onInputEvent(event)
    if event.type == LumixAPI.INPUT_EVENT_AXIS and event.device.type == LumixAPI.INPUT_DEVICE_MOUSE then
        yaw = yaw + event.x * -0.01
        pitch = pitch + event.y * -0.01
    end
    if event.type == LumixAPI.INPUT_EVENT_BUTTON then
		if event.device.type == LumixAPI.INPUT_DEVICE_MOUSE then
            if event.down and event.key_id == 1 then
                aiming = true
            else 
                aiming = false
            end
        end
        if event.device.type == LumixAPI.INPUT_DEVICE_KEYBOARD then
			if event.key_id == string.byte("W") then
                if event.down then
                    forward = 1
                else
                    forward = 0
                end
			end
            if event.key_id == string.byte("S") then
                if event.down then
                    backward = 1
                else
                    backward = 0
                end
			end
            if event.key_id == string.byte("C") then
                if event.down then
                    crouched = not crouched
                end
			end
			if event.key_id == string.byte("A") then
                if event.down then
                    left = 1
                else
                    left = 0
                end
			end
			if event.key_id == string.byte("D") then
                if event.down then
                    right = 1
                else
                    right = 0
                end
			end
			if event.key_id == LumixAPI.INPUT_KEYCODE_SHIFT then
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
    local dir = {math.sin(yaw) * 50, 0, math.cos(yaw) * 50}
    a:applyForce(dir)
end

function start()
    speed_input_idx = this.animator:getInputIndex("speed_y")
    dir_input_idx = this.animator:getInputIndex("speed_x")
    crouched_input_idx = this.animator:getInputIndex("is_crouched")
    falling_input_idx = this.animator:getInputIndex("is_falling")
    aiming_input_idx = this.animator:getInputIndex("is_aiming")
end

function mulquat(a, b)
	return {
        a[4] * b[1] + b[4] * a[1] + a[2] * b[3] - b[2] * a[3],
		a[4] * b[2] + b[4] * a[2] + a[3] * b[1] - b[3] * a[1],
		a[4] * b[3] + b[4] * a[3] + a[1] * b[2] - b[1] * a[2],
		a[4] * b[4] - a[1] * b[1] - a[2] * b[2] - a[3] * b[3]
    }
end

function update(td)
    if speed_input_idx == -1 then 
        speed_input_idx = this.animator:getInputIndex("speed_y")
        dir_input_idx = this.animator:getInputIndex("speed_x")
        crouched_input_idx = this.animator:getInputIndex("is_crouched")
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
    this.animator:setBoolInput(crouched_input_idx, crouched)
    this.animator:setBoolInput(falling_input_idx, gravity_speed < -4)
    this.animator:setBoolInput(aiming_input_idx, aiming)
    local syaw = math.sin(yaw * 0.5)
    local cyaw = math.cos(yaw * 0.5)
    local spitch = math.sin(pitch * 0.5)
    local cpitch = math.cos(pitch * 0.5)
    this.rotation = {0, syaw, 0, cyaw }
    camera_pivot.rotation = mulquat({0, syaw, 0, cyaw }, {-spitch, 0, 0, cpitch})
end