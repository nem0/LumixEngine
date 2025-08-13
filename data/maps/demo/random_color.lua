function start()
	local c = {0, math.random(), math.random(), 1}
	this.model_instance:overrideMaterialVec4(0, "Material color", c)
end
