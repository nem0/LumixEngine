local quality = {HIGH = 1, MEDIUM = 2, LOW = 3}

radius = 0.2
intensity = 1
blur_quality = quality.MEDIUM
Editor.setPropertyType(this, "blur_quality", Editor.INT_PROPERTY)
noise = -1
Editor.setPropertyType(this, "noise", Editor.RESOURCE_PROPERTY, "texture")
debug = false

function bilateralBlur(env, in_buffer, out_buffer, depth_buffer, w, h, shader) 
	env.beginBlock("blur")
	local blur_buf = env.createRenderbuffer(w, h, "r8", "tmp_blur")
	if blur_quality ~= quality.LOW then
	
	env.setRenderTargets(blur_buf)
	env.viewport(0, 0, w, h)
	if blur_quality == quality.HIGH then
		env.drawcallUniforms(w, h, 1.0 / w, 1.0 / h, 1, 8)
	else
		env.drawcallUniforms(w, h, 1.0 / w, 1.0 / h, 2, 4)
	end
	env.drawArray(0, 3, shader
		, { depth_buffer, in_buffer }
		, { depth_test = false, depth_write = false }
		, "BLUR_H"
	)
	env.setRenderTargets(out_buffer)
	env.viewport(0, 0, w, h)
	env.drawArray(0, 3, shader
		, { depth_buffer, blur_buf }
		, { depth_test = false, depth_write = false }
	)

	else
	
	local blur_shader = env.preloadShader("pipelines/blur.shd")
	env.setRenderTargets(blur_buf)
	env.viewport(0, 0, w, h)
	env.drawcallUniforms(1.0 / w, 1.0 / h, 0, 0)
	env.drawArray(0, 3, blur_shader
		, { in_buffer }
		, { depth_test = false, depth_write = false }
		, "BLUR_H"
	)
	env.setRenderTargets(out_buffer)
	env.viewport(0, 0, w, h)
	env.drawArray(0, 3, blur_shader
		, { blur_buf }
		, { depth_test = false, depth_write = false }
	)

	end
	env.endBlock()
end

function postprocess(env, transparent_phase, hdr_buffer, gbuffer0, gbuffer1, gbuffer_depth, shadowmap)
	if not enabled then return hdr_buffer end
	if transparent_phase ~= "pre" then return hdr_buffer end
	if noise == -1 then return hdr_buffer end
	env.beginBlock("ssao")
	if env.ssao_shader == nil then
		env.ssao_shader = env.preloadShader("pipelines/ssao_gld.shd")
	end
	if env.ssao_blur_shader == nil then
		env.ssao_blur_shader = env.preloadShader("pipelines/ssao_upscale.shd")
	end
	if env.ssao_blit_shader == nil then
		env.ssao_blit_shader = env.preloadShader("pipelines/ssao_blit.shd")
	end
	local w = env.viewport_w * 0.5
	local h = env.viewport_h * 0.5
	local ssao_rb = env.createRenderbuffer(w, h, "r8", "ssao")
	local blur_rb = env.createRenderbuffer(env.viewport_w, env.viewport_h, "r8", "blur")

	env.beginBlock("occlusion")

	env.setRenderTargets(ssao_rb)
	env.viewport(0, 0, w, h)
	env.drawcallUniforms(radius, intensity, 0, 0, w, h, 4, 4)
	local state = {
		depth_write = false,
		depth_test = false
	}

	env.drawArray(0
		, 3
		, env.ssao_shader
		, { gbuffer_depth, gbuffer1, noise}
		, state
	)
	env.endBlock()

	bilateralBlur(env, ssao_rb, blur_rb, gbuffer_depth, env.viewport_w, env.viewport_h, env.ssao_blur_shader)

	-- TODO use for indirect light
	env.setRenderTargets(hdr_buffer)
	env.drawArray(0, 3, env.ssao_blit_shader
		, { blur_rb }
		, { depth_test = false, depth_write = false, blending = "multiply" })
	
	env.endBlock()

	if debug then
		env.debugRenderbuffer(blur_rb, hdr_buffer, {1, 0, 0, 0}, {0, 1, 0, 0}, {0, 0, 1, 0}, {0, 0, 0, 1}, {0, 0, 0, 1})
	end

	return hdr_buffer
end

function awake()
	_G["postprocesses"] = _G["postprocesses"] or {}
	_G["postprocesses"]["ssao"] = postprocess
end

function onDestroy()
	_G["postprocesses"]["ssao"] = nil
end