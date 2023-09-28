local forward = 0
local backward = 0
local left = 0
local right = 0
local yaw = 0
local sprint = 0
local jump = 0
local pitch = 0
camera_pivot = -1
Editor.setPropertyType(this, "camera_pivot", Editor.ENTITY_PROPERTY)

function onInputEvent(event : InputEvent)
    if event.type == "axis" and event.device.type == "mouse" then
        yaw = yaw + event.x * -0.003
		pitch = math.max(-math.pi / 2, math.min(math.pi / 2, pitch + event.y * -0.003))
    end
    if event.type == "button" then
		if event.device.type == "keyboard" then
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
            if event.key_id == LumixAPI.INPUT_KEYCODE_SPACE then 
                if event.down then
                    jump = 1
                else
                    jump = 0
                end
            end			
        end		
    end	
end

function onControllerHit(obj)
    local a = obj.rigid_actor
    local dir = {math.sin(yaw), 0, math.cos(yaw)}
    a:applyForce(dir)
end

function multiplyQuaternions(q1, q2)
    return {
        q1[4]*q2[1] + q1[1]*q2[4] + q1[2]*q2[3] - q1[3]*q2[2],
        q1[4]*q2[2] - q1[1]*q2[3] + q1[2]*q2[4] + q1[3]*q2[1],
        q1[4]*q2[3] + q1[1]*q2[2] - q1[2]*q2[1] + q1[3]*q2[4],
        q1[4]*q2[4] - q1[1]*q2[1] - q1[2]*q2[2] - q1[3]*q2[3]
    }
end

function update(td)

    local speed = 3 
    local disp_x = 0 
    local disp_z = 0 
    local disp_y = 0 

    if forward == 1 then 
        if sprint == 1 then 
            speed = 6 
        else 
            speed = 3 
        end		
        disp_z = td * -speed 
    end

    if backward == 1 then 
        disp_z = td * speed 
    end

    if left == 1 then  
        disp_x = td * -speed 
    end

    if right == 1 then  
        disp_x = td * speed 
    end

    if jump == 1 then 
        disp_y = td * speed 
    end

    local a2 = yaw * 0.5
    local a3 = pitch * 0.5
    this.rotation = {0, math.sin(a2), 0, math.cos(a2) }
	local yaw_quat = {0, math.sin(yaw * 0.5), 0, math.cos(yaw * 0.5)}
    local pitch_quat = {math.sin(pitch * 0.5), 0, 0, math.cos(pitch * 0.5)}
    camera_pivot.rotation = multiplyQuaternions(yaw_quat, pitch_quat)
    local dir_x = math.sin(yaw) * disp_z + math.cos(yaw) * disp_x  
    local dir_z = math.cos(yaw) * disp_z - math.sin(yaw) * disp_x  
    this.physical_controller:move({dir_x, disp_y, dir_z})  
end
