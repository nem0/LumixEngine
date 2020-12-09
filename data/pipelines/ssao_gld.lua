radius = 0.35
intensity = 1
debug = false

function postprocess(env, transparent_phase, hdr_buffer, gbuffer0, gbuffer1, gbuffer_depth, shadowmap)
	if not enabled then return hdr_buffer end
	if transparent_phase ~= "pre" then return hdr_buffer end
	env.beginBlock("ssao")
	if env.ssao_shader_compute == nil then
		env.ssao_shader_compute = env.preloadShader("pipelines/ssao_gld.shd")
	end
	if env.ssao_blit_shader == nil then
		env.ssao_blit_shader = env.preloadShader("pipelines/ssao_blur.shd")
	end
	local w = env.viewport_w * 0.5
	local h = env.viewport_h * 0.5
	local ssao_rb = env.createTexture2D(w, h, "r8", "ssao")

	env.viewport(0, 0, w, h)
	
	local w_comp = 1 + w / 16;
	local h_comp = 1 + h / 16;
	env.beginBlock("compute_ao")
	env.drawcallUniforms(radius, intensity)
	env.bindImageTexture(ssao_rb, 0)
	env.bindTextures({gbuffer_depth, gbuffer1}, 1)
	env.dispatch(env.ssao_shader_compute, w_comp, h_comp, 1)
	env.endBlock()

	-- TODO use for indirect light
	env.setRenderTargets(hdr_buffer)
	env.drawArray(0, 3, env.ssao_blit_shader
		, { gbuffer_depth, ssao_rb }
		, { depth_test = false, depth_write = false, blending = "multiply" });
	env.endBlock()

	if debug then
		env.debugRenderbuffer(ssao_rb, hdr_buffer, {1, 0, 0, 0}, {0, 1, 0, 0}, {0, 0, 1, 0}, {0, 0, 0, 1}, {0, 0, 0, 1})
	end

	return hdr_buffer
end

function awake()
	_G["postprocesses"] = _G["postprocesses"] or {}
	_G["postprocesses"]["ssao"] = postprocess
end

function onDestroy()
	_G["postprocesses"]["ssao"] = nil
end