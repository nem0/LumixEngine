local start_pos = this.position;

local vel = {10, 0, 0}
clutter = Lumix.Entity.NULL

function randomVelocity()
	local angle = math.random(0, 6.29)
	
	vel[1] = math.cos(angle) * 10
	vel[3] = math.sin(angle) * 10
end

function update(time_delta)
	local p = this.position 
	p[1] = p[1] + time_delta * vel[1]
	p[3] = p[3] + time_delta * vel[3]
	this.position = p

	if p[1] < start_pos[1] and vel[1] < 0 then randomVelocity() end
	if p[3] < start_pos[3] and vel[3] < 0 then randomVelocity() end
	if p[1] > start_pos[1] + 18 and vel[1] > 0 then randomVelocity() end
	if p[3] > start_pos[3] + 18 and vel[3] > 0 then randomVelocity() end

	local id = clutter.particle_emitter:getGlobalID("effector_pos")
	clutter.particle_emitter:setVec3Global(id, this.position)
end
