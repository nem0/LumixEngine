return {
	show_debug = false,
	only_autoexposure = false,
	luma_limit = 64,
	accomodation_speed = 1,
	exposure = 1,
	enabled = false,

	gui = function(self)
		_, self.enabled = ImGui.Checkbox("Enabled", self.enabled)
		_, self.show_debug = ImGui.Checkbox("Show debug", self.show_debug)
		_, self.only_autoexposure = ImGui.Checkbox("Only autoexposure", self.only_autoexposure)
		_, self.exposure = ImGui.DragFloat("Exposure", self.exposure)
		_, self.accomodation_speed = ImGui.DragFloat("Accomodation speed", self.accomodation_speed)
		_, self.luma_limit = ImGui.DragFloat("Luma limit", self.luma_limit)
	end,

	blurUpscale = function(env, buffer, smaller_buffer, rel_size, rb_desc, state) 
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
	end,

	blur = function(env, buffer, rel_size, rb_desc, state) 
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
	end,

	downscale = function(env, big, rel_size, rb_desc, state)
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
	end,

	autoexposure = function(self, env, hdr_buffer : RenderBuffer) : ()
		if env.PROBE ~= nil then
			return 
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
		env.drawcallUniforms(env.viewport_w, env.viewport_h, self.accomodation_speed) 
		env.dispatch(env.avg_luminance_shader, (env.viewport_w + 15) / 16, (env.viewport_h + 15) / 16, 1);

		env.bindShaderBuffer(env.lum_buf, 1, true)
		env.dispatch(env.avg_luminance_shader, 256, 1, 1, "PASS2");

		env.endBlock()
	end,

	tonemap = function(self, env, hdr_buffer)
		if not self.enabled then return hdr_buffer end
		env.beginBlock("tonemap")
		local format = "rgba16f"
		if env.APP ~= nil or env.PREVIEW ~= nil then
			format = "rgba8"
		end
		env.bloom_tonemap_rb_desc = env.bloom_tonemap_rb_desc or env.createRenderbufferDesc { format = format, debug_name = "tonemap_bloom" }
		local rb = env.createRenderbuffer(env.bloom_tonemap_rb_desc)
		env.setRenderTargets(rb)
		env.bindShaderBuffer(env.lum_buf, 5, false)
		env.drawcallUniforms(self.exposure) 
		env.drawArray(0, 3, env.bloom_tonemap_shader
			, { hdr_buffer }
			, env.bloom_state
		)
		env.endBlock()
		return rb
	end,

	postprocess = function(self, env, hdr_buffer, gbuffer, shadowmap) : ()
		if not self.enabled then return end

		env.bloom_state = env.bloom_state or env.createRenderState({
			depth_write = false,
			depth_test = false
		})
		env.bloom_blend_state = env.bloom_blend_state or env.createRenderState({
			depth_test = false,
			depth_write = false,
			blending = "add"
		})

		if env.bloom_shader == nil then
			env.avg_luminance_shader = env.preloadShader("pipelines/avg_luminance.shd")
			env.bloom_shader = env.preloadShader("pipelines/bloom.shd")
			env.bloom_tonemap_shader = env.preloadShader("pipelines/bloom_tonemap.shd")
			env.bloom_blur_shader = env.preloadShader("pipelines/bloom_blur.shd")
		end
		if env.blur_shader == nil then
			env.blur_shader = env.preloadShader("pipelines/blur.shd")
		end

		self:autoexposure(env, hdr_buffer)

		if not self.only_autoexposure then
			env.beginBlock("bloom")
			env.bloom_rb_desc = env.bloom_rb_desc or env.createRenderbufferDesc { rel_size = {0.5, 0.5}, format = "rgba16f", debug_name = "bloom" }
			local bloom_rb = env.createRenderbuffer(env.bloom_rb_desc)
		
			env.setRenderTargets(bloom_rb)
			env.viewport(0, 0, 0.5 * env.viewport_w, 0.5 * env.viewport_h)
			env.drawcallUniforms(self.luma_limit)
			env.bindShaderBuffer(env.lum_buf, 5, false)
			env.drawArray(0
				, 3
				, env.bloom_shader
				, { hdr_buffer }
				, env.bloom_state
				, "EXTRACT"
			)

			if self.show_debug then
				env.debugRenderbuffer(bloom_rb, hdr_buffer, {1, 0, 0, 0}, {0, 1, 0, 0}, {0, 0, 1, 0}, {0, 0, 0, 1}, {0, 0, 0, 1})
			else 
				env.bloom_ds_rbs = env.bloom_ds_rbs or {
					env.createRenderbufferDesc { rel_size = {0.25, 0.25}, format = "rgba16f", debug_name = "bloom_downscaled" },
					env.createRenderbufferDesc { rel_size = {0.125, 0.125}, format = "rgba16f", debug_name = "bloom_downscaled" },
					env.createRenderbufferDesc { rel_size = {0.0625, 0.0625}, format = "rgba16f", debug_name = "bloom_downscaled" },
					env.createRenderbufferDesc { rel_size = {0.03125, 0.03125}, format = "rgba16f", debug_name = "bloom_downscaled" }
				}

				local bloom2_rb = self.downscale(env, bloom_rb, {0.25, 0.25}, env.bloom_ds_rbs[1], env.bloom_state)
				local bloom4_rb = self.downscale(env, bloom2_rb, {0.125, 0.125}, env.bloom_ds_rbs[2], env.bloom_state)
				local bloom8_rb = self.downscale(env, bloom4_rb, {0.0625, 0.0625}, env.bloom_ds_rbs[3], env.bloom_state)
				local bloom16_rb = self.downscale(env, bloom8_rb, {0.03125, 0.03125}, env.bloom_ds_rbs[4], env.bloom_state)

				env.bloom_blur_rb_desc = env.bloom_blur_rb_desc or env.createRenderbufferDesc { rel_size = { 0.03125, 0.03125 }, debug_name = "bloom_blur", format = "rgba16f" }

				self.blur(env, bloom16_rb, { 0.03125, 0.03125 }, env.bloom_blur_rb_desc, env.bloom_state)
				
				env.bloom_rbs = env.bloom_rbs or {
					env.createRenderbufferDesc { rel_size = { 0.0625, 0.0625 }, debug_name = "bloom_blur", format = "rgba16f" },
					env.createRenderbufferDesc { rel_size = { 0.125, 0.125 }, debug_name = "bloom_blur", format = "rgba16f" },
					env.createRenderbufferDesc { rel_size = { 0.25, 0.25 }, debug_name = "bloom_blur", format = "rgba16f" },
					env.createRenderbufferDesc { rel_size = { 0.5, 0.5 }, debug_name = "bloom_blur", format = "rgba16f" },
				}
				
				self.blurUpscale(env, bloom8_rb, bloom16_rb, { 0.0625, 0.0625}, env.bloom_rbs[1], env.bloom_state)
				self.blurUpscale(env, bloom4_rb, bloom8_rb, { 0.125, 0.125}, env.bloom_rbs[2], env.bloom_state)
				self.blurUpscale(env, bloom2_rb, bloom4_rb, { 0.25, 0.25}, env.bloom_rbs[3], env.bloom_state)
				self.blurUpscale(env, bloom_rb, bloom2_rb, { 0.5, 0.5}, env.bloom_rbs[4], env.bloom_state)

				env.setRenderTargets(hdr_buffer)
				env.drawArray(0, 3, env.bloom_shader
					, { bloom_rb }
					, env.bloom_blend_state
				)
			end
			env.endBlock()
		end
	end
}
