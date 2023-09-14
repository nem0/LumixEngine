return {
	enabled = false,
	grainamount = 0.01,
	lumamount = 0.1,
	noise = LumixAPI.loadResource(LumixAPI.engine, "textures/common/blue_noise.tga", "texture"), -- TODO set from UI

	gui = function(self)
		_, self.enabled = ImGui.Checkbox("Enabled", self.enabled)
		_, self.grainamount = ImGui.DragFloat("Grain", self.grainamount)
		_, self.lumamount = ImGui.DragFloat("Luma", self.lumamount)
	end,

	postprocess = function(self, env, ldr_buffer, gbuffer, shadowmap)
		if not self.enabled then return ldr_buffer end
		if self.noise == -1 then return ldr_buffer end
		env.film_grain_rb_desc = env.film_grain_rb_desc or env.createRenderbufferDesc { rel_size = {1, 1}, format = "rgba8", debug_name = "film_grain" }
		local res = env.createRenderbuffer(env.film_grain_rb_desc)
		env.beginBlock("film_grain")
		if env.film_grain_shader == nil then
			env.film_grain_shader = env.preloadShader("pipelines/film_grain.shd")
		end

		env.drawcallUniforms(self.grainamount, self.lumamount)
		env.setRenderTargets(res)
		env.bindTextures({self.noise}, 1)
		env.drawArray(0, 3, env.film_grain_shader, 
			{ ldr_buffer },
			env.empty_state
		)
		env.endBlock()
		return res
	end
}