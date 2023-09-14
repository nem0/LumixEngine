return {
	radius = 0.2,
	intensity = 3,
	use_temporal = true,
	blur = false,
	current_frame_weight = 0.05,
	history_buf = -1,
	enabled = true,

	gui = function(self)
		_, self.enabled = ImGui.Checkbox("Enabled", self.enabled)
		_, self.intensity = ImGui.DragFloat("Intensity", self.intensity)
		_, self.radius = ImGui.DragFloat("Radius", self.radius)
		_, self.use_temporal = ImGui.Checkbox("Temporal", self.use_temporal)
	end,

	postprocess = function(self, env, hdr_buffer, gbuffer, shadowmap) : ()
		if not self.enabled then return end
		env.ssao_shader = env.ssao_shader or env.preloadShader("pipelines/ssao.shd")
		env.blur_shader = env.blur_shader or env.preloadShader("pipelines/blur.shd")
		env.ssao_blit_shader = env.ssao_blit_shader or env.preloadShader("pipelines/ssao_blit.shd")
		env.ssao_resolve_shader = env.ssao_resolve_shader or env.preloadShader("pipelines/ssao_resolve.shd")
		env.ssao_rb_desc = env.ssao_rb_desc or env.createRenderbufferDesc { rel_size = {0.5, 0.5}, format = "r8", debug_name = "ssao", compute_write = true }

		local w = math.floor(env.viewport_w * 0.5)
		local h = math.floor(env.viewport_h * 0.5)
		local ssao_rb = env.createRenderbuffer(env.ssao_rb_desc)

		if self.use_temporal and self.history_buf == -1 then
			self.history_buf = env.createRenderbuffer(env.ssao_rb_desc)
			env.setRenderTargets(self.history_buf)
			env.clear(env.CLEAR_ALL, 1, 1, 1, 1, 0)
		end

		env.setRenderTargets()

		env.beginBlock("ssao " .. tostring(w) .. "x" .. tostring(h))
		env.drawcallUniforms(self.radius, self.intensity, w, h)
		env.bindTextures({gbuffer.DS, gbuffer.B}, 0)
		env.bindImageTexture(ssao_rb, 2)
		env.dispatch(env.ssao_shader, (w + 15) / 16, (h + 15) / 16, 1)

		env.ssao_blur_rb_desc = env.ssao_blur_rb_desc or env.createRenderbufferDesc { format = "r8", rel_size = {0.5, 0.5}, debug_name = "ssao_blur" }
		if self.use_temporal then
			env.beginBlock("ssao_resolve " .. tostring(w) .. "x" .. tostring(h))
			env.drawcallUniforms( w, h, 0, 0, self.current_frame_weight, 0, 0, 0 )
			env.bindTextures({gbuffer.DS, self.history_buf}, 0)
			env.bindImageTexture(ssao_rb, 2)
			env.dispatch(env.ssao_resolve_shader, (w + 15) / 16, (h + 15) / 16, 1)
			env.endBlock()
			if self.blur then
				env.blur(ssao_rb, w, h, env.ssao_blur_rb_desc)
			end
		else
			if self.blur then
				env.blur(ssao_rb, w, h, env.ssao_blur_rb_desc)
			end
		end

		env.beginBlock("ssao_blit " .. tostring(env.viewport_w) .. "x" .. tostring(env.viewport_h))
		env.drawcallUniforms( env.viewport_w, env.viewport_h, 0, 0 )
		env.bindTextures({ssao_rb}, 0)
		env.bindImageTexture(gbuffer.B, 1)
		env.dispatch(env.ssao_blit_shader, (env.viewport_w + 15) / 16, (env.viewport_h + 15) / 16, 1)
		env.endBlock()
		env.endBlock()

		if self.use_temporal then
			self.history_buf = ssao_rb
			env.keepRenderbufferAlive(self.history_buf)
		end
	end
}
