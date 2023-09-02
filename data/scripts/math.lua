function mulquat(a, b)
	return {
        a[4] * b[1] + b[4] * a[1] + a[2] * b[3] - b[2] * a[3],
		a[4] * b[2] + b[4] * a[2] + a[3] * b[1] - b[3] * a[1],
		a[4] * b[3] + b[4] * a[3] + a[1] * b[2] - b[1] * a[2],
		a[4] * b[4] - a[1] * b[1] - a[2] * b[2] - a[3] * b[3]
    }
end

function makeQuatFromYaw(yaw)
    local syaw = math.sin(yaw * 0.5)
    local cyaw = math.cos(yaw * 0.5)
    return {0, syaw, 0, cyaw }
end

function makeQuatFromPitch(pitch)
    local spitch = math.sin(pitch * 0.5)
    local cpitch = math.cos(pitch * 0.5)
    return {-spitch, 0, 0, cpitch}
end
