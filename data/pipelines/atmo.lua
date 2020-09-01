object_atmo = true

function postprocess(env, transparent_phase, hdr_buffer, gbuffer0, gbuffer1, gbuffer_depth, shadowmap)
	if not enabled then return hdr_buffer end
	if transparent_phase ~= "pre" then return hdr_buffer end
	env.beginBlock("atmo")
	if env.atmo_shader == nil then
		env.atmo_shader = env.preloadShader("pipelines/atmo.shd")
	end
	env.setRenderTargetsReadonlyDS(hdr_buffer, gbuffer_depth)
	local state = {
		blending = "add",
		depth_write = false,
		depth_test = false
	}
	if object_atmo == false then
		state.stencil_write_mask = 0
		state.stencil_func = env.STENCIL_NOT_EQUAL
		state.stencil_ref = 1
		state.stencil_mask = 0xff
		state.stencil_sfail = env.STENCIL_KEEP
		state.stencil_zfail = env.STENCIL_KEEP
		state.stencil_zpass = env.STENCIL_KEEP
	end
	env.drawArray(0, 4, env.atmo_shader, { gbuffer_depth }, {}, {}, state)
	env.endBlock()
	return hdr_buffer
end

function awake()
	_G["postprocesses"] = _G["postprocesses"] or {}
	_G["postprocesses"]["atmo"] = postprocess
end

function onDestroy()
	_G["postprocesses"]["atmo"] = nil
end