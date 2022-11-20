grainamount = 0.01
lumamount = 0.1
noise = -1
Editor.setPropertyType(this, "noise", Editor.RESOURCE_PROPERTY, "texture")

function postprocess(env, transparent_phase, ldr_buffer, gbuffer0, gbuffer1, gbuffer2, gbuffer_depth, shadowmap)
	if not enabled then return ldr_buffer end
	if transparent_phase ~= "post_tonemap" then return ldr_buffer end
	if noise == -1 then return ldr_buffer end
	env.film_grain_rb_desc = env.film_grain_rb_desc or env.createRenderbufferDesc { rel_size = {1, 1}, format = "rgba8", debug_name = "film_grain" }
	local res = env.createRenderbuffer(env.film_grain_rb_desc)
	env.beginBlock("film_grain")
	if env.film_grain_shader == nil then
		env.film_grain_shader = env.preloadShader("pipelines/film_grain.shd")
	end

	env.drawcallUniforms(grainamount, lumamount)
	env.setRenderTargets(res)
	env.bindTextures({noise}, 1)
	env.drawArray(0, 3, env.film_grain_shader, 
		{ ldr_buffer },
		env.empty_state
	)
	env.endBlock()
	return res
end

function awake()
	_G["postprocesses"] = _G["postprocesses"] or {}
	_G["postprocesses"]["filmgrain"] = postprocess
end

function onDestroy()
	_G["postprocesses"]["filmgrain"] = nil
end

function onUnload()
	onDestroy()
end
