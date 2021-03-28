sky = -1
Editor.setPropertyType(this, "sky", Editor.RESOURCE_PROPERTY, "texture")

function postprocess(env, transparent_phase, hdr_buffer, gbuffer0, gbuffer1, gbuffer2, gbuffer_depth, shadowmap)
	if not enabled then return hdr_buffer end
	if transparent_phase ~= "pre" then return hdr_buffer end
	if sky == -1 then return hdr_buffer end
	env.beginBlock("sky")
	if env.cubemap_sky_shader == nil then
		env.cubemap_sky_shader = env.preloadShader("pipelines/cubemap_sky.shd")
	end
	env.setRenderTargetsDS(hdr_buffer, gbuffer_depth)
	env.bindTextures({sky}, 0)
	local state = {
		stencil_write_mask = 0,
		stencil_func = env.STENCIL_EQUAL,
		stencil_ref = 0,
		stencil_mask = 0xff,
		stencil_sfail = env.STENCIL_KEEP,
		stencil_zfail = env.STENCIL_KEEP,
		stencil_zpass = env.STENCIL_REPLACE,
		depth_write = false,
		depth_test = false
	}
	env.drawArray(0, 3, env.cubemap_sky_shader, {}, state)
	env.endBlock()
	return hdr_buffer
end

function awake()
	_G["postprocesses"] = _G["postprocesses"] or {}
	_G["postprocesses"]["cubemap_sky"] = postprocess
end

function onDestroy()
	_G["postprocesses"]["cubemap_sky"] = nil
end