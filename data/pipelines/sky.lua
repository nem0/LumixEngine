function postprocess(env, transparent_phase, hdr_buffer, gbuffer0, gbuffer1, gbuffer_depth, shadowmap)
	if not enabled then return hdr_buffer end
	if transparent_phase ~= "pre" then return hdr_buffer end
	env.beginBlock("sky")
	if env.procedural_sky_shader == nil then
		env.procedural_sky_shader = env.preloadShader("pipelines/procedural_sky.shd")
	end
	env.setRenderTargets(0, hdr_buffer, gbuffer_depth)
	env.setStencil(0x0, env.STENCIL_NOT_EQUAL, 1, 0xff, env.STENCIL_KEEP, env.STENCIL_KEEP, env.STENCIL_REPLACE)
	env.drawArray(0, 4, env.procedural_sky_shader)
	env.setStencil(0xff, env.STENCIL_DISABLE, 1, 0xff, env.STENCIL_KEEP, env.STENCIL_KEEP, env.STENCIL_KEEP)
	env.endBlock()
	return hdr_buffer
end

function awake()
	if _G["postprocesses"] == nil then
		_G["postprocesses"] = {}
	end
	table.insert(_G["postprocesses"], postprocess)
end

function onDestroy()
	for i, v in ipairs(_G["postprocesses"]) do
		if v == postprocess then
			table.remove(_G["postprocesses"], i)
			break;
		end
	end
end