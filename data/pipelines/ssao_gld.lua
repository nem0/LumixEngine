radius = 0.35
intensity = 1
debug = false

function debugAO(env, rb, output, r_mask, g_mask, b_mask, a_mask, offsets)
	env.setRenderTargets(output)

	env.drawcallUniforms( 
		0, 0, 1, 1, 
		r_mask[1], r_mask[2], r_mask[3], r_mask[4], 
		g_mask[1], g_mask[2], g_mask[3], g_mask[4], 
		b_mask[1], b_mask[2], b_mask[3], b_mask[4], 
		a_mask[1], a_mask[2], a_mask[3], a_mask[4], 
		offsets[1], offsets[2], offsets[3], offsets[4]
	)
	env.bindTextures({rb}, 0)
	env.drawArray(0, 3, env.textured_quad_shader
		, { depth_test = false }
	)
end

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
	env.bindRenderbuffers({gbuffer_depth, gbuffer1}, 1)
	env.dispatch(env.ssao_shader_compute, w_comp, h_comp, 1)
	env.endBlock()

	-- TODO use for indirect light
	env.setRenderTargets(hdr_buffer)

	env.bindTextures({ssao_rb}, 1)
	env.drawArray(0, 3, env.ssao_blit_shader
		, { gbuffer_depth }
		, { depth_test = false, depth_write = false, blending = "multiply" });
	env.endBlock()

	if debug then
		debugAO(env, ssao_rb, hdr_buffer, {1, 0, 0, 0}, {0, 1, 0, 0}, {0, 0, 1, 0}, {0, 0, 0, 1}, {0, 0, 0, 1})
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