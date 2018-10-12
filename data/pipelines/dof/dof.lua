local dof_shader = nil
local dof_blur_shader = nil

function postprocess(env, phase, input, gbuffer0, gbuffer1, gbuffer_depth, shadowmap)
	if not enabled then return input end
	if phase ~= "post" then return input end
	env.beginBlock("dof")
	if dof_shader == nil then
		dof_shader = env.preloadShader("pipelines/dof/dof.shd")
		dof_blur_shader = env.preloadShader("pipelines/dof/dof_blur.shd")
	end

	local tmp_rb = env.createRenderbuffer(1, 1, true, "rgba16f")
	
	env.blending("")
	env.setRenderTargets(0, tmp_rb)
	env.drawArray(0, 4, dof_blur_shader, 
		{ 
			u_source = input,
			u_depth = gbuffer_depth
		},
		{},
		{ "HBLUR", "NEAR" }
	)

	env.setRenderTargets(0, input)
	env.drawArray(0, 4, dof_blur_shader, 
		{
			u_source = tmp_rb,
			u_depth = gbuffer_depth
		},
		{},
		{ "NEAR" }
	)

	env.setRenderTargets(0, tmp_rb)
	env.drawArray(0, 4, dof_shader, 
		{
			u_source = input,
			u_depth = gbuffer_depth
		}
	)

	
	env.endBlock()
	return tmp_rb
end

function awake()
	if _G["postprocesses"] == nil then
		_G["postprocesses"] = {}
	end
	table.insert(_G["postprocesses"], postprocess)
end

function onDestroy()
	for i, v in ipairs(_G["postprocesses"]) do
		if v == postprocess then
			table.remove(_G["postprocesses"], i)
			break;
		end
	end
end