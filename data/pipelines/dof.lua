return {
	focus_distance = 10,
	blur_range = 20,
	enabled = false,
	max_blur_size = 10,
	sharp_range = 0,

	gui = function(self)
		_, self.enabled = ImGui.Checkbox("Enabled", self.enabled)
		_, self.focus_distance = ImGui.DragFloat("Focus distance", self.focus_distance)
		_, self.sharp_range = ImGui.DragFloat("Sharp range", self.sharp_range)
		_, self.blur_range = ImGui.DragFloat("Blur range", self.blur_range)
		_, self.max_blur_size = ImGui.DragFloat("Max blur size", self.max_blur_size)
	end,

	postprocess = function(self, env, input, gbuffer, shadowmap)
		if not self.enabled then return input end

		env.beginBlock("dof")
		env.dof_shader = env.dof_shader or env.preloadShader("pipelines/dof.shd")
		env.dof_rb_desc = env.dof_rb_desc or env.createRenderbufferDesc { format = "rgba16f", debug_name = "dof_tmp" }
		local tmp_rb = env.createRenderbuffer(env.dof_rb_desc)
		
		env.drawcallUniforms(self.focus_distance, self.blur_range, self.max_blur_size, self.sharp_range)

		-- TODO optimize - halfres
		env.setRenderTargets(tmp_rb)
		env.drawArray(0, 3, env.dof_shader, 
			{
				input,
				gbuffer.DS
			}
		)

		env.drawcallUniforms( 
			0, 0, 1, 1, 
			1, 0, 0, 0, 
			0, 1, 0, 0, 
			0, 0, 1, 0, 
			0, 0, 0, 1, 
			0, 0, 0, 1
		)
	
		env.setRenderTargets(input)
		env.drawArray(0, 3, env.textured_quad_shader
			, { tmp_rb }
			, env.empty_state
		)

		env.endBlock()
		return input
	end
}
