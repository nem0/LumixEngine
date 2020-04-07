function postprocess(env, transparent_phase, hdr_buffer, gbuffer0, gbuffer1, gbuffer_depth, shadowmap)
	if not enabled then return hdr_buffer end
	if transparent_phase ~= "pre" then return hdr_buffer end
	env.beginBlock("sky")
	if env.procedural_sky_shader == nil then
		env.procedural_sky_shader = env.preloadShader("pipelines/sky.shd")
	end
	env.setRenderTargets(hdr_buffer, gbuffer_depth)
	local state = {
		stencil_write_mask = 0,
		stencil_func = env.STENCIL_NOT_EQUAL,
		stencil_ref = 1,
		stencil_mask = 0xff,
		stencil_sfail = env.STENCIL_KEEP,
		stencil_zfail = env.STENCIL_KEEP,
		stencil_zpass = env.STENCIL_REPLACE,
		depth_write = false,
		depth_test = false
	}
	env.drawArray(0, 4, env.procedural_sky_shader, {}, {}, {}, state)
	env.endBlock()
	return hdr_buffer
end

function awake()
	_G["postprocesses"] = _G["postprocesses"] or {}
	_G["postprocesses"]["sky"] = postprocess
end

function onDestroy()
	_G["postprocesses"]["sky"] = nil
end