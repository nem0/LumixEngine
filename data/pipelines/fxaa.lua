function postprocess(env, transparent_phase, ldr_buffer, gbuffer0, gbuffer1, gbuffer_depth, shadowmap)
	if not enabled then return ldr_buffer end
	if transparent_phase ~= "post_tonemap" then return ldr_buffer end
	local res
	if env.SCENE_VIEW ~= nil then
		res = env.createRenderbuffer(env.viewport_w, env.viewport_h, "rgba16f", "fxaa")
	else
		res = env.createRenderbuffer(env.viewport_w, env.viewport_h, "rgba8", "fxaa")
	end
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