function postprocess(env, transparent_phase, ldr_buffer, gbuffer0, gbuffer1, gbuffer2, gbuffer_depth, shadowmap)
	if not enabled then return ldr_buffer end
	if transparent_phase ~= "post_tonemap" then return ldr_buffer end
	env.fxaa_rb_desc = env.fxaa_rb_desc or env.createRenderbufferDesc { rel_size = {1, 1}, format = "srgba", debug_name = "fxaa" }
	local res = env.createRenderbuffer(env.fxaa_rb_desc)
	env.beginBlock("fxaa")
	if env.fxaa_shader == nil then
		env.fxaa_shader = env.preloadShader("pipelines/fxaa.shd")
	end

	env.setRenderTargets(res)
	env.drawArray(0, 3, env.fxaa_shader, 
		{ ldr_buffer },
		env.empty_state
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


function onUnload()
	onDestroy()
end
