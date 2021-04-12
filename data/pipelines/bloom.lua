enabled = false
debug = false
only_autoexposure = false
luma_limit = 1
accomodation_speed = 1
local state = {
	depth_write = false,
	depth_test = false
}

function blurUpscale(env, buffer, smaller_buffer, format, w, h, tmp_rb_dbg_name) 
	local blur_buf = env.createRenderbuffer { width = w, height = h, format = format, debug_name = tmp_rb_dbg_name }
	env.setRenderTargets(blur_buf)
	env.drawcallUniforms(1.0 / w, 1.0 / h, 0, 0)
	env.viewport(0, 0, w, h)
	env.drawArray(0, 3, env.bloom_blur_shader
		, { buffer, smaller_buffer }
		, { depth_test = false, depth_write = false }
		, "BLUR_H"
	)
	env.setRenderTargets(buffer)
	env.viewport(0, 0, w, h)
	env.drawArray(0, 3, env.blur_shader
		, { blur_buf }
		, { depth_test = false, depth_write = false }
	)
end

function blur(env, buffer, format, w, h, tmp_rb_dbg_name) 
	local blur_buf = env.createRenderbuffer { width = w, height = h, format = format, debug_name = tmp_rb_dbg_name }
	env.setRenderTargets(blur_buf)
	env.drawcallUniforms(1.0 / w, 1.0 / h, 0, 0)
	env.viewport(0, 0, w, h)
	env.drawArray(0, 3, env.blur_shader
		, { buffer }
		, { depth_test = false, depth_write = false }
		, "BLUR_H"
	)
	env.setRenderTargets(buffer)
	env.viewport(0, 0, w, h)
	env.drawArray(0, 3, env.blur_shader
		, { blur_buf }
		, { depth_test = false, depth_write = false }
	)
end

function downscale(env, big, w, h)
	local small = env.createRenderbuffer { width = w, height = h, format = "rgba16f", debug_name = "bloom_downscaled" }
	env.setRenderTargets(small)
	env.viewport(0, 0, w, h)
	env.drawArray(0
		, 3
		, env.bloom_shader
		, { big }
		, state
		, "DOWNSCALE"
	)
	return small
end

function autoexposure(env, hdr_buffer)
	if env.PROBE ~= nil then
		return hdr_buffer
	end

	env.beginBlock("autoexposure")
	
	if env.lum_buf == nil then
		env.lum_buf = env.createBuffer(2048);
	end
	
	env.setRenderTargets()

	env.bindShaderBuffer(env.lum_buf, 1, true)
	env.dispatch(env.avg_luminance_shader, 256, 1, 1, "PASS0");

	env.bindTextures({ hdr_buffer }, 0)
	env.bindShaderBuffer(env.lum_buf, 1, true)
	env.drawcallUniforms(env.viewport_w, env.viewport_h, accomodation_speed) 
	env.dispatch(env.avg_luminance_shader, (env.viewport_w + 15) / 16, (env.viewport_h + 15) / 16, 1);

	env.bindShaderBuffer(env.lum_buf, 1, true)
	env.dispatch(env.avg_luminance_shader, 256, 1, 1, "PASS2");

	env.endBlock()
end

function tonemap(env, hdr_buffer)
	env.beginBlock("tonemap")
	local format = "rgba16f"
	if env.APP ~= nil or env.PREVIEW ~= nil or env.screenshot_request == 1 then
		format = "rgba8"
	end
	local rb = env.createRenderbuffer { width = env.viewport_w, height = env.viewport_h, format = format, debug_name = "tonemap_bloom" }
	env.setRenderTargets(rb)
	env.bindShaderBuffer(env.lum_buf, 5, false)
	env.drawArray(0, 3, env.bloom_tonemap_shader
		, { hdr_buffer }
		, { depth_test = false }
	)
	env.endBlock()
	return rb
end

function postprocess(env, transparent_phase, hdr_buffer, gbuffer0, gbuffer1, gbuffer2, gbuffer_depth, shadowmap)
	if not enabled then return hdr_buffer end
	env.custom_tonemap = true
	if transparent_phase == "tonemap" then return tonemap(env, hdr_buffer) end
	if transparent_phase ~= "post" then return hdr_buffer end
	
	if env.bloom_shader == nil then
		env.avg_luminance_shader = env.preloadShader("pipelines/avg_luminance.shd")
		env.bloom_shader = env.preloadShader("pipelines/bloom.shd")
		env.bloom_tonemap_shader = env.preloadShader("pipelines/bloom_tonemap.shd")
		env.bloom_blur_shader = env.preloadShader("pipelines/bloom_blur.shd")
		env.textured_quad_shader = env.preloadShader("pipelines/textured_quad.shd")
	end
	if env.blur_shader == nil then
		env.blur_shader = env.preloadShader("pipelines/blur.shd")
	end

	autoexposure(env, hdr_buffer)

	if not only_autoexposure then
		env.beginBlock("bloom")
		local bloom_rb = env.createRenderbuffer { width = 0.5 * env.viewport_w, height = 0.5 * env.viewport_h, format = "rgba16f", debug_name = "bloom" }
	
		env.setRenderTargets(bloom_rb)
		env.viewport(0, 0, 0.5 * env.viewport_w, 0.5 * env.viewport_h)
		env.drawcallUniforms(luma_limit)
		env.bindShaderBuffer(env.lum_buf, 5, false)
		env.drawArray(0
			, 3
			, env.bloom_shader
			, { hdr_buffer }
			, state
			, "EXTRACT"
		)

		if debug then
			env.debugRenderbuffer(bloom_rb, hdr_buffer, {1, 0, 0, 0}, {0, 1, 0, 0}, {0, 0, 1, 0}, {0, 0, 0, 1}, {0, 0, 0, 1})
		else 
			local bloom2_rb = downscale(env, bloom_rb, env.viewport_w * 0.25, env.viewport_h * 0.25)
			local bloom4_rb = downscale(env, bloom2_rb, env.viewport_w * 0.125, env.viewport_h * 0.125)
			local bloom8_rb = downscale(env, bloom4_rb, env.viewport_w * 0.0625, env.viewport_h * 0.0625)
			local bloom16_rb = downscale(env, bloom8_rb, env.viewport_w * 0.03125, env.viewport_h * 0.03125)

			blur(env, bloom16_rb, "rgba16f", env.viewport_w * 0.03125, env.viewport_h * 0.03125, "bloom_blur")
			blurUpscale(env, bloom8_rb, bloom16_rb, "rgba16f", env.viewport_w * 0.0625, env.viewport_h * 0.0625, "bloom_blur")
			blurUpscale(env, bloom4_rb, bloom8_rb, "rgba16f", env.viewport_w * 0.125, env.viewport_h * 0.125, "bloom_blur")
			blurUpscale(env, bloom2_rb, bloom4_rb, "rgba16f", env.viewport_w * 0.25, env.viewport_h * 0.25, "bloom_blur")
			blurUpscale(env, bloom_rb, bloom2_rb, "rgba16f", env.viewport_w * 0.5, env.viewport_h * 0.5, "bloom_blur")

			env.setRenderTargets(hdr_buffer)
			env.drawArray(0, 3, env.bloom_shader
				, { bloom_rb }
				, { depth_test = false, depth_write = false, blending = "add" });
		end
		env.endBlock()
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


function onUnload()
	onDestroy()
end
