return {
	max_steps = 20,
	stride = 4,
	current_frame_weight = 0.1,
	sss_history = -1,
	enabled = false,

	gui = function(self)
		_, self.enabled = ImGui.Checkbox("Enabled", self.enabled)
		_, self.max_steps = ImGui.DragInt("Max steps", self.max_steps)
		_, self.stride = ImGui.DragInt("Stride", self.stride)
		_, self.current_frame_weight = ImGui.DragFloat("Current frame weight", self.current_frame_weight)
	end,

	postprocess = function(self, env, hdr_buffer, gbuffer, shadowmap) : ()
		if not self.enabled then return end
		env.sss_shader = env.sss_shader or env.preloadShader("pipelines/sss.shd")
		env.sss_blit_shader = env.sss_blit_shader or env.preloadShader("pipelines/sss_blit.shd")

		env.beginBlock("SSS")
		
		env.sss_rb_desc = env.sss_rb_desc or env.createRenderbufferDesc { format = "r8", debug_name = "sss", compute_write = true }
		local sss_rb = env.createRenderbuffer(env.sss_rb_desc)
		if self.sss_history == -1 then
			self.sss_history = env.createRenderbuffer(env.sss_rb_desc)
			env.setRenderTargets(self.sss_history)
			env.clear(env.CLEAR_ALL, 1, 1, 1, 1, 0)
		end

		env.setRenderTargets()

		env.bindTextures({gbuffer.DS}, 0)
		env.bindImageTexture(sss_rb, 1)
		env.drawcallUniforms (
			env.viewport_w,
			env.viewport_h,
			self.max_steps, 
			self.stride,
			self.current_frame_weight,
			0, 0, 0
		)
		env.dispatch(env.sss_shader, (env.viewport_w + 15) / 16, (env.viewport_h + 15) / 16, 1)

		env.bindTextures({self.sss_history, gbuffer.DS}, 0)
		env.bindImageTexture(gbuffer.C, 0)
		env.bindImageTexture(sss_rb, 1)
		env.dispatch(env.sss_blit_shader, (env.viewport_w + 15) / 16, (env.viewport_h + 15) / 16, 1)

		self.sss_history = sss_rb
		env.keepRenderbufferAlive(self.sss_history)

		env.endBlock()
	end
}