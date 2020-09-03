object_atmo = true

function postprocess(env, transparent_phase, hdr_buffer, gbuffer0, gbuffer1, gbuffer_depth, shadowmap)
	if not enabled then return hdr_buffer end
	if transparent_phase ~= "pre" then return hdr_buffer end
	env.beginBlock("atmo")
	if env.atmo_shader == nil then
		env.atmo_shader = env.preloadShader("pipelines/atmo.shd")
		env.atmo_scattering_shader = env.preloadShader("pipelines/atmo_scattering.shd")
		env.atmo_transmittance_shader = env.preloadShader("pipelines/atmo_transmittance.shd")
		env.inscatter_precomputed = env.createTexture2D(64, 128, "rgba32f")
		env.transmittance_precomputed = env.createTexture2D(128, 128, "rg32f")
		
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
	
	env.beginBlock("precompute_transmittance")
	env.bindImageTexture(env.transmittance_precomputed, 0)
	env.dispatch(env.atmo_transmittance_shader, 128 / 16, 128 / 16, 1)
	env.bindImageTexture(env.inscatter_precomputed, 0)
	env.bindRawTexture(env.transmittance_precomputed, 1)
	env.endBlock()

	env.beginBlock("precompute_inscatter")
	env.dispatch(env.atmo_scattering_shader, 64 / 16, 128 / 16, 1)
	env.endBlock()

	env.bindRawTexture(env.inscatter_precomputed, 1);
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