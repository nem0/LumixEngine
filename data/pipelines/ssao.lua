radius = 0.2
intensity = 1
debug = false

function postprocess(env, transparent_phase, hdr_buffer, gbuffer0, gbuffer1, gbuffer_depth, shadowmap)
	if not enabled then return hdr_buffer end
	if transparent_phase ~= "pre" then return hdr_buffer end
	env.beginBlock("ssao")
	if env.ssao_shader == nil then
		env.ssao_shader = env.preloadShader("pipelines/ssao.shd")
	end
	if env.blur_shader == nil then
		env.blur_shader = env.preloadShader("pipelines/blur.shd")
	end
	if env.ssao_blit_shader == nil then
		env.ssao_blit_shader = env.preloadShader("pipelines/ssao_blit.shd")
	end
	local w = env.viewport_w * 0.5
	local h = env.viewport_h * 0.5
	local ssao_rb = env.createRenderbuffer { width = w, height = h, format = "r8", debug_name = "ssao" }
	env.setRenderTargets(ssao_rb)
	local state = {
		depth_write = false,
		depth_test = false
	}
	env.viewport(0, 0, w, h)
	env.drawcallUniforms( radius, intensity )
	
	env.drawArray(0
		, 3
		, env.ssao_shader
		, { gbuffer_depth, gbuffer1 }
		, state
	)
	env.blur(ssao_rb, "r8", w, h, "ssao_blur")
	
	-- TODO use for indirect light
	env.setRenderTargets(hdr_buffer)
	env.drawArray(0, 3, env.ssao_blit_shader
		, { ssao_rb }
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