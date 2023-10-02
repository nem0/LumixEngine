return {
dot = function(a, b)
	return a[1] * b[1] + a[2] * b[2] + a[3] * b[3]
end,

mulQuat = function(a, b)
	return {
        a[4] * b[1] + b[4] * a[1] + a[2] * b[3] - b[2] * a[3],
		a[4] * b[2] + b[4] * a[2] + a[3] * b[1] - b[3] * a[1],
		a[4] * b[3] + b[4] * a[3] + a[1] * b[2] - b[1] * a[2],
		a[4] * b[4] - a[1] * b[1] - a[2] * b[2] - a[3] * b[3]
    }
end,

makeQuatFromYaw = function(yaw)
    local syaw = math.sin(yaw * 0.5)
    local cyaw = math.cos(yaw * 0.5)
    return {0, syaw, 0, cyaw }
end,

makeQuatFromPitch = function(pitch)
    local spitch = math.sin(pitch * 0.5)
    local cpitch = math.cos(pitch * 0.5)
    return {-spitch, 0, 0, cpitch}
end,

yawToDir = function(yaw)
	return {math.sin(yaw), 0, math.cos(yaw)}
end,

mulVec3Num = function(v, f)
	return {v[1] * f, v[2] * f, v[3] * f}
end,

addVec3 = function(a, b)
    return {a[1] + b[1], a[2] + b[2], a[3] + b[3]}
end,

subVec3 = function(a, b)
    return {a[1] - b[1], a[2] - b[2], a[3] - b[3]}
end,

mulVec3 = function(a, f)
    return {a[1] * f, a[2] * f, a[3] * f}
end,

}
