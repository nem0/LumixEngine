function postprocess(env, transparent_phase, ldr_buffer, gbuffer0, gbuffer1, gbuffer_depth, shadowmap)
	if not enabled then return ldr_buffer end
	if transparent_phase ~= "post_tonemap" then return ldr_buffer end
	local format = "rgba16f"
	if env.APP ~= nil or env.PREVIEW ~= nil then
		format = "rgba8"
	end
	local res = env.createRenderbuffer { width = env.viewport_w, height = env.viewport_h, format = format, debug_name = "fxaa" }
	env.beginBlock("fxaa")
	if env.fxaa_shader == nil then
		env.fxaa_shader = env.preloadShader("pipelines/fxaa.shd")
	end

	env.setRenderTargets(res)
	env.drawArray(0, 3, env.fxaa_shader, 
		{ ldr_buffer },
		{ depth_test = false, blending = ""}
	)
	env.endBlock()
	return res
end

function awake()
	_G["postprocesses"] = _G["postprocesses"] or {}
	_G["postprocesses"]["fxaa"] = postprocess
end

function onDestroy()
	_G["postprocesses"]["fxaa"] = nil
end