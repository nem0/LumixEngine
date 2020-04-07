radius = 0.2
intensity = 1
local ssao_size = 1024

function blur(env, buffer, format, w, h, tmp_rb_dbg_name) 
	local blur_buf = env.createRenderbuffer(w, h, false, format, tmp_rb_dbg_name)
	env.setRenderTargets(blur_buf)
	env.viewport(0, 0, w, h)
	env.drawArray(0, 4, env.blur_shader
		, { buffer }
		, { {1.0 / w, 1.0 / h, 0, 0 }}
		, "BLUR_H"
		, { depth_test = false, depth_write = false }
	)
	env.setRenderTargets(buffer)
	env.viewport(0, 0, w, h)
	env.drawArray(0, 4, env.blur_shader
		, { blur_buf }
		, { {1.0 / w, 1.0 / h, 0, 0 } }
		, {}
		, { depth_test = false, depth_write = false }
	)
end

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
	local ssao_rb = env.createRenderbuffer(ssao_size, ssao_size, false, "r8", "ssao")
	env.setRenderTargets(ssao_rb)
	local state = {
		depth_write = false,
		depth_test = false
	}
	env.viewport(0, 0, ssao_size, ssao_size)
	env.drawArray(0
		, 4
		, env.ssao_shader
		, { gbuffer_depth, gbuffer1 }
		, { {radius, intensity, 0, 0} }
		, {}
		, state
	)
	blur(env, ssao_rb, "r8", ssao_size, ssao_size, "ssao_blur")
	
	env.setRenderTargets(hdr_buffer)
	env.drawArray(0, 4, env.ssao_blit_shader
		, { ssao_rb }
		, {}
		, {}
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