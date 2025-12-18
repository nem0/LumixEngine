local lmath = require "scripts/math"

local bolts = {}

function start()
	for i = 1,5 do
		local bolt = {}
		bolts[i] = bolt
		bolt.entity = this.world:createEntity()
		bolt.entity:createComponent("particle_emitter")
		bolt.entity.particle_emitter.source = "maps/particles/lightning.pat"
		bolt.entity.particle_emitter:emitRibbons(0, 1)
		bolt.entity:createComponent("point_light")
		bolt.entity.point_light.range = 4;
		bolt.entity.point_light.intensity = 1;
		bolt.entity.point_light.color = {0.3, 0.3, 1};

		moveRibbon(bolt)
	end
end

function moveRibbon(bolt)
	local dir = {
		math.random() * 2 - 1,
		math.random() * 2 - 1,
		math.random() * 2 - 1
	}
	local hit, entity, hitpos, hitnormal = this.world.physics:raycastEx(this.position, dir)
	if hit and lmath.distSquared(hitpos, this.position) < 5 * 5 then
		bolt.entity.position = lmath.addVec3(hitpos, lmath.mulVec3(hitnormal, 0.15))
		bolt.timeout = math.random() * 0.7 + 0.2
		local emitter = bolt.entity.particle_emitter
		emitter:killRibbon(0, 0)
		emitter:emitRibbons(0, 1)
	else
		bolt.timeout = -1
	end
end

function update(time_delta)
	for _, bolt in pairs(bolts) do
		local emitter = bolt.entity.particle_emitter
		local pe_target_id = emitter:getGlobalID("g_target")
		emitter:setVec3Global(pe_target_id, this.position);
		bolt.timeout = bolt.timeout - time_delta
		bolt.entity.point_light.intensity = math.random() * 0.5 + 0.5
		if bolt.timeout < 0 then
			moveRibbon(bolt)
		end
	end
end
