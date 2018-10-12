local film_grain_shader = nil

function postprocess(env, transparent_phase, ldr_buffer, gbuffer0, gbuffer1, gbuffer_depth, shadowmap)
	if not enabled then return ldr_buffer end
	if transparent_phase ~= "post_tonemap" then return ldr_buffer end
	local res = env.createRenderbuffer(1, 1, true, "rgba8")
	env.beginBlock("film_grain")
	if film_grain_shader == nil then
		film_grain_shader = env.preloadShader("pipelines/film_grain.shd")
	end

	env.blending("")
	env.setRenderTargets(0, res)
	env.drawArray(0, 4, film_grain_shader, { u_source = ldr_buffer })
	env.endBlock()
	return res
end

function awake()
	if _G["postprocesses"] == nil then
		_G["postprocesses"] = {}
	end
	table.insert(_G["postprocesses"], postprocess)
end

function onDestroy()
	for i, v in ipairs(_G["postprocesses"]) do
		if v == postprocess then
			table.remove(_G["postprocesses"], i)
			break;
		end
	end
end