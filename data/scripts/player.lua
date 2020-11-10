local forward = 0
local left = 0
local right = 0
local yaw = 0
local sprint = 0
local speed_input_idx = -1

function onInputEvent(event)
    if event.type == LumixAPI.INPUT_EVENT_AXIS and event.device.type == LumixAPI.INPUT_DEVICE_MOUSE then
        yaw = yaw + event.x * -0.01
    end
    if event.type == LumixAPI.INPUT_EVENT_BUTTON then
		if event.device.type == LumixAPI.INPUT_DEVICE_KEYBOARD then
			if event.key_id == string.byte("W") then
                if event.down then
                    forward = 1
                else
                    forward = 0
                end
			end
			if event.key_id == LumixAPI.INPUT_KEYCODE_LEFT then
                if event.down then
                    left = 1
                else
                    left = 0
                end
			end
			if event.key_id == LumixAPI.INPUT_KEYCODE_RIGHT then
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

function start()
    speed_input_idx = this.animator:getInputIndex("speed")
    yaw_input_idx = this.animator:getInputIndex("yaw")
end

function clamp(x, a, b)
    if x < a then return a end
    if x > b then return b end
    return x
end

function update(td)
    if speed_input_idx == -1 then 
        speed_input_idx = this.animator:getInputIndex("speed")
        yaw_input_idx = this.animator:getInputIndex("yaw")
    end

    local speed = 0
    if forward == 1 then
        if sprint == 1 then
            speed = 6
        else
            speed = 3
        end
    end

    this.animator:setFloatInput(speed_input_idx, speed)
    local a2 = yaw * 0.5
    this.rotation = {0, math.sin(a2), 0, math.cos(a2) }
end