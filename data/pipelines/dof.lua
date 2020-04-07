near_blur = 1
near_sharp = 3
far_sharp = 50
far_blur = 70

function postprocess(env, phase, input, gbuffer0, gbuffer1, gbuffer_depth, shadowmap)
	if not enabled then return input end
	if phase ~= "post" then return input end
	env.beginBlock("dof")
	if env.dof_shader == nil then
		env.dof_shader = env.preloadShader("pipelines/dof.shd")
		env.dof_blur_shader = env.preloadShader("pipelines/dof_blur.shd")
	end

	local tmp_rb = env.createRenderbuffer(1, 1, true, "rgba16f", "dof_tmp")
	
	env.setRenderTargets(tmp_rb)
	env.drawArray(0, 4, env.dof_blur_shader, 
		{ 
			input,
			gbuffer_depth
		},
		{ { near_blur, near_sharp, far_sharp, far_blur } },
		{ "HBLUR", "NEAR" },
		{ depth_test = false, blending = "alpha"}
	)

	env.setRenderTargets(input)
	env.drawArray(0, 4, env.dof_blur_shader, 
		{
			tmp_rb,
			gbuffer_depth
		},
		{ { near_blur, near_sharp, far_sharp, far_blur } },
		{ "NEAR" },
		{ depth_test = false, blending = "alpha"}
	)

	env.setRenderTargets(tmp_rb)
	env.drawArray(0, 4, env.dof_blur_shader, 
		{ 
			input,
			gbuffer_depth
		},
		{ { near_blur, near_sharp, far_sharp, far_blur } },
		{ "HBLUR", "FAR" },
		{ depth_test = false, blending = "alpha"}
	)

	env.setRenderTargets(input)
	env.drawArray(0, 4, env.dof_blur_shader, 
		{
			tmp_rb,
			gbuffer_depth
		},
		{ { near_blur, near_sharp, far_sharp, far_blur } },
		{ "FAR" },
		{ depth_test = false, blending = "alpha"}
	)


	env.setRenderTargets(tmp_rb)
	env.drawArray(0, 4, env.dof_shader, 
		{
			input,
			gbuffer_depth
		}
	)

	
	env.endBlock()
	return tmp_rb
end

function awake()
	_G["postprocesses"] = _G["postprocesses"] or {}
	_G["postprocesses"]["dof"] = postprocess
end

function onDestroy()
	_G["postprocesses"]["dof"] = nil
end