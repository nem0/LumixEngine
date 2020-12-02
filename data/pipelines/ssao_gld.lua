radius = 0.1
intensity = 1
debug = false

function postprocess(env, transparent_phase, hdr_buffer, gbuffer0, gbuffer1, gbuffer_depth, shadowmap)
	if not enabled then return hdr_buffer end
	if transparent_phase ~= "pre" then return hdr_buffer end
	env.beginBlock("ssao")
	if env.ssao_shader == nil then
		env.ssao_shader = env.preloadShader("pipelines/ssao_gld.shd")
	end
	if env.ssao_blit_shader == nil then
		env.ssao_blit_shader = env.preloadShader("pipelines/ssao_blur.shd")
	end
	local w = env.viewport_w * 0.5
	local h = env.viewport_h * 0.5
	local ssao_rb = env.createTexture2D(w, h, "r8", "ssao")
	local state = {
		depth_write = false,
		depth_test = false
	}
	
	env.drawcallUniforms(radius, intensity, w, h)
	
	env.beginBlock("compute_ao")
	env.bindImageTexture(ssao_rb, 0)
	env.bindRenderbuffers({gbuffer_depth, gbuffer1}, 1)
	env.dispatch(env.ssao_shader, w / 8, h / 8, 1)
	env.endBlock()

	-- TODO use for indirect light
	env.setRenderTargets(hdr_buffer)

	env.bindRawTexture(ssao_rb, 1)
	env.drawArray(0, 3, env.ssao_blit_shader
		, { gbuffer_depth }
		, { depth_test = false, depth_write = false, blending = "multiply" });
	env.endBlock()

	return hdr_buffer
end

function awake()
	_G["postprocesses"] = _G["postprocesses"] or {}
	_G["postprocesses"]["ssao"] = postprocess
end

function onDestroy()
	_G["postprocesses"]["ssao"] = nil
end