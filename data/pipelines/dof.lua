return {
	near_blur = 1,
	near_sharp = 3,
	far_sharp = 20,
	far_blur = 30,
	enabled = false,

	gui = function(self)
		_, self.enabled = ImGui.Checkbox("Enabled", self.enabled)
		_, self.near_blur = ImGui.DragFloat("Near blur", self.near_blur)
		_, self.near_sharp = ImGui.DragFloat("Near sharp", self.near_sharp)
		_, self.far_sharp = ImGui.DragFloat("Far sharp", self.far_sharp)
		_, self.far_blur = ImGui.DragFloat("Far blur", self.far_blur)
	end,

	postprocess = function(self, env, input, gbuffer, shadowmap)
		if not self.enabled then return input end
		env.beginBlock("dof")
		if env.dof_shader == nil then
			env.dof_shader = env.preloadShader("pipelines/dof.shd")
			env.dof_blur_shader = env.preloadShader("pipelines/dof_blur.shd")
		end

		env.dof_rb_desc = env.dof_rb_desc or env.createRenderbufferDesc { format = "rgba16f", debug_name = "dof_tmp" }
		local tmp_rb = env.createRenderbuffer(env.dof_rb_desc)
		
		env.dof_state = env.dof_state or env.createRenderState({ depth_test = false, blending = "alpha"})

		env.setRenderTargets(tmp_rb)
		env.drawcallUniforms(self.near_blur, self.near_sharp, self.far_sharp, self.far_blur)

		env.drawArray(0, 3, env.dof_blur_shader, 
			{ 
				input,
				gbuffer.DS
			},
			env.dof_state,
			{ "HBLUR", "NEAR" }
		)

		env.setRenderTargets(input)
		env.drawArray(0, 3, env.dof_blur_shader, 
			{
				tmp_rb,
				gbuffer.DS
			},
			env.dof_state,
			{ "NEAR" }
		)

		env.setRenderTargets(tmp_rb)
		env.drawArray(0, 3, env.dof_blur_shader, 
			{ 
				input,
				gbuffer.DS
			},
			env.dof_state,
			{ "HBLUR", "FAR" }
		)

		env.setRenderTargets(input)
		env.drawArray(0, 3, env.dof_blur_shader, 
			{
				tmp_rb,
				gbuffer.DS
			},
			env.dof_state,
			{ "FAR" }
		)


		env.setRenderTargets(tmp_rb)
		env.drawArray(0, 3, env.dof_shader, 
			{
				input,
				gbuffer.DS
			}
		)

		
		env.endBlock()
		return tmp_rb
	end
}
