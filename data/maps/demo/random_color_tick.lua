local t = 0
local g = 0

function start()
	g = math.random()
end

function update(time_delta)
	t = t + time_delta * 3
	local c = {0, g, math.abs(math.cos(t)), 1}
	this.model_instance:overrideMaterialVec4(0, "Material color", c)
end
