radius = 0.2
intensity = 1
use_temporal = false
blur = false
current_frame_weight = 0.05
local history_buf = -1

function postprocess(env, phase, hdr_buffer, gbuffer0, gbuffer1, gbuffer2, gbuffer_depth, shadowmap)
	if not enabled then return hdr_buffer end
	if phase ~= "pre_lightpass" then return hdr_buffer end

	env.ssao_shader = env.ssao_shader or env.preloadShader("pipelines/ssao.shd")
	env.blur_shader = env.blur_shader or env.preloadShader("pipelines/blur.shd")
	env.ssao_blit_shader = env.ssao_blit_shader or env.preloadShader("pipelines/ssao_blit.shd")
	env.ssao_resolve_shader = env.ssao_resolve_shader or env.preloadShader("pipelines/ssao_resolve.shd")

	local w = math.floor(env.viewport_w * 0.5)
	local h = math.floor(env.viewport_h * 0.5)
	local ssao_rb = env.createRenderbuffer { width = w, height = h, format = "r8", debug_name = "ssao", compute_write = true }

	if use_temporal and history_buf == -1 then
		history_buf = env.createRenderbuffer { width = w, height = h, format = "r8", debug_name = "ssao", compute_write = true }
		env.setRenderTargets(history_buf)
		env.clear(env.CLEAR_ALL, 1, 1, 1, 1, 0)
	end

	env.setRenderTargets()

	env.beginBlock("ssao " .. tostring(w) .. "x" .. tostring(h))
	env.drawcallUniforms(radius, intensity, w, h)
	env.bindTextures({gbuffer_depth, gbuffer1}, 0)
	env.bindImageTexture(ssao_rb, 2)
	env.dispatch(env.ssao_shader, (w + 15) / 16, (h + 15) / 16, 1)

	if use_temporal then
		env.beginBlock("ssao_resolve " .. tostring(w) .. "x" .. tostring(h))
		env.drawcallUniforms( w, h, 0, 0, current_frame_weight, 0, 0, 0 )
		env.bindTextures({gbuffer_depth, history_buf}, 0)
		env.bindImageTexture(ssao_rb, 2)
		env.dispatch(env.ssao_resolve_shader, (w + 15) / 16, (h + 15) / 16, 1)
		env.endBlock()
		if blur then
			env.blur(ssao_rb, "r8", w, h, "ssao_blur")
		end
	else
		if blur then
			env.blur(ssao_rb, "r8", w, h, "ssao_blur")
		end
	end

	env.beginBlock("ssao_blit " .. tostring(env.viewport_w) .. "x" .. tostring(env.viewport_h))
	env.drawcallUniforms( env.viewport_w, env.viewport_h, 0, 0 )
	env.bindTextures({ssao_rb}, 0)
	env.bindImageTexture(gbuffer1, 1)
	env.dispatch(env.ssao_blit_shader, (env.viewport_w + 15) / 16, (env.viewport_h + 15) / 16, 1)
	env.endBlock()
	env.endBlock()

	if use_temporal then
		history_buf = ssao_rb
		env.keepRenderbufferAlive(history_buf)
	end
end

function awake()
	_G["postprocesses"] = _G["postprocesses"] or {}
	_G["postprocesses"]["ssao"] = postprocess
end

function onDestroy()
	_G["postprocesses"]["ssao"] = nil
end


function onUnload()
	onDestroy()
end
