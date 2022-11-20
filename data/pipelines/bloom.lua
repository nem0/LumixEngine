enabled = false
debug = false
only_autoexposure = false
luma_limit = 64
accomodation_speed = 1
exposure = 1

function blurUpscale(env, buffer, smaller_buffer, rel_size, rb_desc, state) 
	local blur_buf = env.createRenderbuffer(rb_desc)
	env.setRenderTargets(blur_buf)
	local w = rel_size[1] * env.viewport_w
	local h = rel_size[2] * env.viewport_h
	env.drawcallUniforms(1.0 / w, 1.0 / h, 0, 0)
	env.viewport(0, 0, w, h)
	env.drawArray(0, 3, env.bloom_blur_shader
		, { buffer, smaller_buffer }
		, state
		, "BLUR_H"
	)
	env.setRenderTargets(buffer)
	env.viewport(0, 0, w, h)
	env.drawArray(0, 3, env.blur_shader
		, { blur_buf }
		, state
	)
end

function blur(env, buffer, rel_size, rb_desc, state) 
	local blur_buf = env.createRenderbuffer(rb_desc)
	env.setRenderTargets(blur_buf)
	local w = rel_size[1] * env.viewport_w
	local h = rel_size[2] * env.viewport_h
	env.drawcallUniforms(1.0 / w, 1.0 / h, 0, 0)
	env.viewport(0, 0, w, h)
	env.drawArray(0, 3, env.blur_shader
		, { buffer }
		, state
		, "BLUR_H"
	)
	env.setRenderTargets(buffer)
	env.viewport(0, 0, w, h)
	env.drawArray(0, 3, env.blur_shader
		, { blur_buf }
		, state
	)
end

function downscale(env, big, rel_size, rb_desc, state)
	local small = env.createRenderbuffer(rb_desc)
	env.setRenderTargets(small)
	env.viewport(0, 0, env.viewport_w * rel_size[1], env.viewport_h * rel_size[2])
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

function tonemap(env, hdr_buffer, state)
	env.beginBlock("tonemap")
	local format = "rgba16f"
	if env.APP ~= nil or env.PREVIEW ~= nil then
		format = "rgba8"
	end
	env.bloom_tonemap_rb_desc = env.bloom_tonemap_rb_desc or env.createRenderbufferDesc { format = format, debug_name = "tonemap_bloom" }
	local rb = env.createRenderbuffer(env.bloom_tonemap_rb_desc)
	env.setRenderTargets(rb)
	env.bindShaderBuffer(env.lum_buf, 5, false)
	env.drawcallUniforms(exposure) 
	env.drawArray(0, 3, env.bloom_tonemap_shader
		, { hdr_buffer }
		, state
	)
	env.endBlock()
	return rb
end

function postprocess(env, transparent_phase, hdr_buffer, gbuffer0, gbuffer1, gbuffer2, gbuffer_depth, shadowmap)
	if not enabled then return hdr_buffer end
	env.custom_tonemap = true

	env.bloom_state = env.bloom_state or env.createRenderState({
		depth_write = false,
		depth_test = false
	})
	env.bloom_blend_state = env.bloom_blend_state or env.createRenderState({
		depth_test = false,
		depth_write = false,
		blending = "add"
	})

	if transparent_phase == "tonemap" then return tonemap(env, hdr_buffer, env.bloom_state) end
	if transparent_phase ~= "post" then return hdr_buffer end
	
	if env.bloom_shader == nil then
		env.avg_luminance_shader = env.preloadShader("pipelines/avg_luminance.shd")
		env.bloom_shader = env.preloadShader("pipelines/bloom.shd")
		env.bloom_tonemap_shader = env.preloadShader("pipelines/bloom_tonemap.shd")
		env.bloom_blur_shader = env.preloadShader("pipelines/bloom_blur.shd")
	end
	if env.blur_shader == nil then
		env.blur_shader = env.preloadShader("pipelines/blur.shd")
	end

	autoexposure(env, hdr_buffer)

	if not only_autoexposure then
		env.beginBlock("bloom")
		env.bloom_rb_desc = env.bloom_rb_desc or env.createRenderbufferDesc { rel_size = {0.5, 0.5}, format = "rgba16f", debug_name = "bloom" }
		local bloom_rb = env.createRenderbuffer(env.bloom_rb_desc)
	
		env.setRenderTargets(bloom_rb)
		env.viewport(0, 0, 0.5 * env.viewport_w, 0.5 * env.viewport_h)
		env.drawcallUniforms(luma_limit)
		env.bindShaderBuffer(env.lum_buf, 5, false)
		env.drawArray(0
			, 3
			, env.bloom_shader
			, { hdr_buffer }
			, env.bloom_state
			, "EXTRACT"
		)

		if debug then
			env.debugRenderbuffer(bloom_rb, hdr_buffer, {1, 0, 0, 0}, {0, 1, 0, 0}, {0, 0, 1, 0}, {0, 0, 0, 1}, {0, 0, 0, 1})
		else 
			env.bloom_ds_rbs = env.bloom_ds_rbs or {
				env.createRenderbufferDesc { rel_size = {0.25, 0.25}, format = "rgba16f", debug_name = "bloom_downscaled" },
				env.createRenderbufferDesc { rel_size = {0.125, 0.125}, format = "rgba16f", debug_name = "bloom_downscaled" },
				env.createRenderbufferDesc { rel_size = {0.0625, 0.0625}, format = "rgba16f", debug_name = "bloom_downscaled" },
				env.createRenderbufferDesc { rel_size = {0.03125, 0.03125}, format = "rgba16f", debug_name = "bloom_downscaled" }
			}

			local bloom2_rb = downscale(env, bloom_rb, {0.25, 0.25}, env.bloom_ds_rbs[1], env.bloom_state)
			local bloom4_rb = downscale(env, bloom2_rb, {0.125, 0.125}, env.bloom_ds_rbs[2], env.bloom_state)
			local bloom8_rb = downscale(env, bloom4_rb, {0.0625, 0.0625}, env.bloom_ds_rbs[3], env.bloom_state)
			local bloom16_rb = downscale(env, bloom8_rb, {0.03125, 0.03125}, env.bloom_ds_rbs[4], env.bloom_state)

			env.bloom_blur_rb_desc = env.bloom_blur_rb_desc or env.createRenderbufferDesc { rel_size = { 0.03125, 0.03125 }, debug_name = "bloom_blur", format = "rgba16f" }

			blur(env, bloom16_rb, { 0.03125, 0.03125 }, env.bloom_blur_rb_desc, env.bloom_state)
			
			env.bloom_rbs = env.bloom_rbs or {
				env.createRenderbufferDesc { rel_size = { 0.0625, 0.0625 }, debug_name = "bloom_blur", format = "rgba16f" },
				env.createRenderbufferDesc { rel_size = { 0.125, 0.125 }, debug_name = "bloom_blur", format = "rgba16f" },
				env.createRenderbufferDesc { rel_size = { 0.25, 0.25 }, debug_name = "bloom_blur", format = "rgba16f" },
				env.createRenderbufferDesc { rel_size = { 0.5, 0.5 }, debug_name = "bloom_blur", format = "rgba16f" },
			}
			
			blurUpscale(env, bloom8_rb, bloom16_rb, { 0.0625, 0.0625}, env.bloom_rbs[1], env.bloom_state)
			blurUpscale(env, bloom4_rb, bloom8_rb, { 0.125, 0.125}, env.bloom_rbs[2], env.bloom_state)
			blurUpscale(env, bloom2_rb, bloom4_rb, { 0.25, 0.25}, env.bloom_rbs[3], env.bloom_state)
			blurUpscale(env, bloom_rb, bloom2_rb, { 0.5, 0.5}, env.bloom_rbs[4], env.bloom_state)

			env.setRenderTargets(hdr_buffer)
			env.drawArray(0, 3, env.bloom_shader
				, { bloom_rb }
				, env.bloom_blend_state
			)
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
