enabled = false
debug = false
luma_limit = 1
accomodation_speed = 1
local state = {
	depth_write = false,
	depth_test = false
}

function blur(env, buffer, format, w, h, tmp_rb_dbg_name) 
	local blur_buf = env.createRenderbuffer(w, h, format, tmp_rb_dbg_name)
	env.setRenderTargets(blur_buf)
	env.drawcallUniforms(1.0 / w, 1.0 / h, 0, 0)
	env.viewport(0, 0, w, h)
	env.drawArray(0, 4, env.blur_shader
		, { buffer }
		, { depth_test = false, depth_write = false }
		, "BLUR_H"
	)
	env.setRenderTargets(buffer)
	env.viewport(0, 0, w, h)
	env.drawArray(0, 4, env.blur_shader
		, { blur_buf }
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
		, state
	)
	return small
end

function downscaleLuma(env, big, size) 
	local small = env.createRenderbuffer(size, size, "r16f", "luma" .. tostring(size))
	env.setRenderTargets(small)
	env.viewport(0, 0, size, size)
	env.drawArray(0, 4, env.luma_shader
		, { big }
		, { depth_test = false }
	)
	return small
end

local lum_accum_rb

function autoexposure(env, hdr_buffer)
	if env.PROBE ~= nil then
		return hdr_buffer
	end

	env.beginBlock("autoexposure")
	local luma256_rb = env.createRenderbuffer(256, 256, "r16f", "luma256")
	env.setRenderTargets(luma256_rb)
	env.viewport(0, 0, 256, 256)
	env.drawArray(0, 4, env.luma_shader
		, { hdr_buffer }
		, { depth_test = false }
		, "EXTRACT_LUMA"
	)

	local luma64_rb = downscaleLuma(env, luma256_rb, 64)
	local luma16_rb = downscaleLuma(env, luma64_rb, 16)
	local luma4_rb = downscaleLuma(env, luma16_rb, 4)
	local luma1_rb = downscaleLuma(env, luma4_rb, 1)

	if env.lum_prev_rb == nil then
		env.lum_prev_rb = env.createPersistentRenderbuffer(1, 1, "r16f", "luma_prev")
		env.setRenderTargets(env.lum_prev_rb)
		env.clear(env.CLEAR_COLOR, 1, 1, 1, 1, 0)
	end
	
	lum_accum_rb = env.createRenderbuffer(1, 1, "r16f", "luma_accum")
	env.setRenderTargets(lum_accum_rb)
	env.viewport(0, 0, 1, 1)
	env.drawcallUniforms(accomodation_speed) 
	env.drawArray(0, 4, env.luma_shader
		, { luma1_rb, env.lum_prev_rb }
		, { depth_test = false }
		, "ACCUM_LUMA"
	)

	env.setRenderTargets(env.lum_prev_rb)
	env.viewport(0, 0, 1, 1)
	env.drawcallUniforms(0, 0, 1, 1, 
		1, 0, 0, 0, 
		0, 1, 0, 0, 
		0, 0, 1, 0, 
		0, 0, 0, 1, 
		0, 0, 0, 0)
	env.drawArray(0, 4, env.textured_quad_shader
		, { lum_accum_rb }
		, { depth_test = false })
	env.endBlock()
end

function tonemap(env, hdr_buffer)
	env.beginBlock("tonemap")
	local rb
	if env.APP ~= nil or env.PREVIEW ~= nil or env.screenshot_request == 1 then
		rb = env.createRenderbuffer(env.viewport_w, env.viewport_h, "rgba8", "tonemap_bloom")
	else
		rb = env.createRenderbuffer(env.viewport_w, env.viewport_h, "rgba16f", "tonemap_bloom")
	end
	env.setRenderTargets(rb)
	env.drawArray(0, 4, env.bloom_tonemap_shader
		, { hdr_buffer, lum_accum_rb }
		, { depth_test = false }
	)
	env.endBlock()
	return rb
end

function postprocess(env, transparent_phase, hdr_buffer, gbuffer0, gbuffer1, gbuffer_depth, shadowmap)
	env.custom_tonemap = true
	if not enabled then return hdr_buffer end
	if transparent_phase == "tonemap" then return tonemap(env, hdr_buffer) end
	if transparent_phase ~= "post" then return hdr_buffer end
	
	if env.bloom_extract_shader == nil then
		env.luma_shader = env.preloadShader("pipelines/luma.shd")
		env.bloom_extract_shader = env.preloadShader("pipelines/bloom_extract.shd")
		env.bloom_shader = env.preloadShader("pipelines/bloom.shd")
		env.bloom_tonemap_shader = env.preloadShader("pipelines/bloom_tonemap.shd")
		env.bloom_downscale_shader = env.preloadShader("pipelines/bloom_downscale.shd")
		env.textured_quad_shader = env.preloadShader("pipelines/textured_quad.shd")
	end
	if env.blur_shader == nil then
		env.blur_shader = env.preloadShader("pipelines/blur.shd")
	end

	autoexposure(env, hdr_buffer)

	env.beginBlock("bloom")
	local bloom_rb = env.createRenderbuffer(0.5 * env.viewport_w, 0.5 * env.viewport_h, "rgba16f", "bloom")
	
	env.setRenderTargets(bloom_rb)
	env.viewport(0, 0, 0.5 * env.viewport_w, 0.5 * env.viewport_h)
	env.drawcallUniforms( luma_limit );
	env.drawArray(0
		, 4
		, env.bloom_extract_shader
		, { hdr_buffer, lum_accum_rb }
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
		, { depth_test = false, depth_write = false, blending = "add" });
	env.endBlock()

	if debug then
		env.debugRenderbuffer(bloom_rb, hdr_buffer, {1, 0, 0, 0}, {0, 1, 0, 0}, {0, 0, 1, 0}, {0, 0, 0, 0}, {0, 0, 0, 1})
	end

	return hdr_buffer
end

function awake()
	_G["postprocesses"] = _G["postprocesses"] or {}
	_G["postprocesses"]["bloom"] = postprocess
end

function onDestroy()
	_G["postprocesses"]["bloom"] = nil
end