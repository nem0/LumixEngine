return {
	enabled = false,

	gui = function(self)
		_, self.enabled = ImGui.Checkbox("Enabled", self.enabled)
	end,

	postprocess = function(self, env, ldr_buffer, gbuffer, shadowmap)
		if not self.enabled then return ldr_buffer end
		env.fxaa_rb_desc = env.fxaa_rb_desc or env.createRenderbufferDesc { rel_size = {1, 1}, format = "srgba", debug_name = "fxaa" }
		local res = env.createRenderbuffer(env.fxaa_rb_desc)
		env.beginBlock("fxaa")
		if env.fxaa_shader == nil then
			env.fxaa_shader = env.preloadShader("pipelines/fxaa.shd")
		end

		env.setRenderTargets(res)
		env.drawArray(0, 3, env.fxaa_shader, 
			{ ldr_buffer },
			env.empty_state
		)
		env.endBlock()
		return res
	end
}