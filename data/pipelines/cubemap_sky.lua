return {
	enabled = false,
	intensity = 1.0,
	sky = nil,

	gui = function(self)
		_, self.enabled = ImGui.Checkbox("Enabled", self.enabled)
		_, self.intensity = ImGui.DragFloat("Intensity", self.intensity)
	end,

	postprocess = function(self, env, hdr_buffer, gbuffer, shadowmap) : ()
		if not self.enabled then return end
		self.sky = self.sky or LumixAPI.loadResource(LumixAPI.engine, "textures/Yokohama2/cube.ltc", "texture"), -- TODO set from UI
		env.beginBlock("sky")
		if env.cubemap_sky_shader == nil then
			env.cubemap_sky_shader = env.preloadShader("pipelines/cubemap_sky.shd")
		end
		env.setRenderTargetsDS(hdr_buffer, gbuffer.DS)
		env.bindTextures({self.sky}, 0)
		env.cubemap_sky_state = env.cubemap_sky_state or env.createRenderState({
			stencil_write_mask = 0,
			stencil_func = env.STENCIL_EQUAL,
			stencil_ref = 0,
			stencil_mask = 0xff,
			stencil_sfail = env.STENCIL_KEEP,
			stencil_zfail = env.STENCIL_KEEP,
			stencil_zpass = env.STENCIL_REPLACE,
			depth_write = false,
			depth_test = false
		})
		env.drawcallUniforms(self.intensity, 0, 0, 0)
		env.drawArray(0, 3, env.cubemap_sky_shader, {}, env.cubemap_sky_state)
		env.endBlock()
	end
}