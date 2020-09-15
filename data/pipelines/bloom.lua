enabled = false
luma_limit = 1
local state = {
	depth_write = false,
	depth_test = false
}

function blur(env, buffer, format, w, h, tmp_rb_dbg_name) 
	local blur_buf = env.createRenderbuffer(w, h, format, tmp_rb_dbg_name)
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

function downscale(env, big, w, h)
	local small = env.createRenderbuffer(w, h, "rgba16f", "bloom_downscaled")
	env.setRenderTargets(small)
	env.viewport(0, 0, w, h)
	env.drawArray(0
		, 4
		, env.bloom_downscale_shader
		, { big }
		, {}
		, {}
		, state
	)
	return small
end

function postprocess(env, transparent_phase, hdr_buffer, gbuffer0, gbuffer1, gbuffer_depth, shadowmap)
	if not enabled then return hdr_buffer end
	if transparent_phase ~= "post" then return hdr_buffer end
	
	env.beginBlock("bloom")

	if env.bloom_extract_shader == nil then
		env.bloom_extract_shader = env.preloadShader("pipelines/bloom_extract.shd")
		env.bloom_shader = env.preloadShader("pipelines/bloom.shd")
		env.bloom_downscale_shader = env.preloadShader("pipelines/bloom_downscale.shd")
	end
	if env.blur_shader == nil then
		env.blur_shader = env.preloadShader("pipelines/blur.shd")
	end
	local bloom_rb = env.createRenderbuffer(0.5 * env.viewport_w, 0.5 * env.viewport_h, "rgba16f", "bloom")
	
	env.setRenderTargets(bloom_rb)
	env.viewport(0, 0, 0.5 * env.viewport_w, 0.5 * env.viewport_h)
	env.drawcallUniforms( luma_limit );
	env.drawArray(0
		, 4
		, env.bloom_extract_shader
		, { hdr_buffer }
		, { }
		, {}
		, state
	)

	local bloom2_rb = downscale(env, bloom_rb, env.viewport_w * 0.25, env.viewport_h * 0.25)
	local bloom4_rb = downscale(env, bloom2_rb, env.viewport_w * 0.125, env.viewport_h * 0.125)
	local bloom8_rb = downscale(env, bloom4_rb, env.viewport_w * 0.0625, env.viewport_h * 0.0625)
	local bloom16_rb = downscale(env, bloom8_rb, env.viewport_w * 0.03125, env.viewport_h * 0.03125)

	blur(env, bloom_rb, "rgba16f", env.viewport_w * 0.5, env.viewport_h * 0.5, "bloom_blur")
	blur(env, bloom2_rb, "rgba16f", env.viewport_w * 0.25, env.viewport_h * 0.25, "bloom_blur")
	blur(env, bloom4_rb, "rgba16f", env.viewport_w * 0.125, env.viewport_h * 0.125, "bloom_blur")
	blur(env, bloom8_rb, "rgba16f", env.viewport_w * 0.0625, env.viewport_h * 0.0625, "bloom_blur")
	blur(env, bloom16_rb, "rgba16f", env.viewport_w * 0.03125, env.viewport_h * 0.03125, "bloom_blur")

	env.setRenderTargets(hdr_buffer)
	env.drawArray(0, 4, env.bloom_shader
		, { bloom_rb, bloom2_rb, bloom4_rb, bloom8_rb, bloom16_rb }
		, {}
		, {}
		, { depth_test = false, depth_write = false, blending = "add" });
	env.endBlock()
	return hdr_buffer
end

function awake()
	_G["postprocesses"] = _G["postprocesses"] or {}
	_G["postprocesses"]["bloom"] = postprocess
end

function onDestroy()
	_G["postprocesses"]["bloom"] = nil
end