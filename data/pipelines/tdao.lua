return {
	enabled = true,
	intensity = 0.3,
	show_debug = false,
	resolution = 512,
	res_changed = false,
	range = 100,
	scale = 0.01,
	depth_range = 200,
	depth_offset = 0.02,
	last_camera_pos = nil,
	update_every_frame = false,

	gui = function(self)
		_, self.show_debug = ImGui.Checkbox("Debug", self.show_debug)
		_, self.update_every_frame = ImGui.Checkbox("Update every frame", self.update_every_frame)
		_, self.enabled = ImGui.Checkbox("Enabled", self.enabled)
		_, self.intensity = ImGui.DragFloat("Intensity", self.intensity)
		_, self.range = ImGui.DragFloat("XZ Range", self.range)
		_, self.scale = ImGui.DragFloat("Scale", self.scale)
		_, self.depth_range = ImGui.DragFloat("Depth range", self.depth_range)
		_, self.depth_offset = ImGui.DragFloat("Depth offset", self.depth_offset)
		self.res_changed, self.resolution = ImGui.DragInt("Resolution", self.resolution)
	end,

	debug = function(self, env, hdr_buffer)
		if self.show_debug then
			env.debugRenderbuffer(env.tdao_rb, hdr_buffer, {1, 0, 0, 0}, {0, 1, 0, 0}, {0, 0, 1, 0}, {0, 0, 0, 1}, {0, 0, 0, 1})
		end
	end,

	postprocess = function(self, env, hdr_buffer, gbuffer, shadowmap) : ()
		if not self.enabled then 
			self.last_camera_pos = nil
			env.tdao_rb = nil
			return 
		end
		local w = self.resolution
		local h = self.resolution
		env.tdao_shader = env.tdao_shader or env.preloadShader("pipelines/tdao.shd")
		if self.res_changed then 
			self.last_camera_pos = nil
			env.tdao_rb = nil
			env.tdao_rb_desc = nil 
		end
		env.tdao_rb_desc = env.tdao_rb_desc or env.createRenderbufferDesc { size = {w, h}, format = "depth32", debug_name = "tdao" }
		if env.tdao_rb == nil then
			env.tdao_rb = env.createRenderbuffer(env.tdao_rb_desc)
		end
		env.keepRenderbufferAlive(env.tdao_rb)

		local step_size = 5
		local camera_pos = env.getCameraPositionFromParams(env.getCameraParams())
		-- floor position so that ao does not shimmer when the camera moves
		local offset0 = camera_pos[1] - math.floor(camera_pos[1] / step_size) * step_size
		local offset1 = camera_pos[2] - math.floor(camera_pos[2] / step_size) * step_size
		local offset2 = camera_pos[3] - math.floor(camera_pos[3] / step_size) * step_size
		camera_pos[1] = camera_pos[1] - offset0
		camera_pos[2] = camera_pos[2] - offset1
		camera_pos[3] = camera_pos[3] - offset2
		
		local camera_moved = self.last_camera_pos == nil 
			or math.abs(camera_pos[1] - self.last_camera_pos[1]) > 0.1
			or math.abs(camera_pos[2] - self.last_camera_pos[2]) > 0.1
			or math.abs(camera_pos[3] - self.last_camera_pos[3]) > 0.1

		env.beginBlock("tdao")
		if camera_moved or self.update_every_frame then
			env.setRenderTargetsDS(env.tdao_rb)
			env.clear(env.CLEAR_ALL, 0, 0, 0, 1, 0)
		
			env.setOrthoCustomCameraParams(camera_pos, {-0.707106769, 0, 0, 0.707106769}, w, h, self.range, -self.depth_range * 0.5, self.depth_range * 0.5, true)
	
			env.viewport(0, 0, w, h)
			env.pass(env.CUSTOM_CAMERA_PARAMS)
	
			local entities = env.cull(env.CUSTOM_CAMERA_PARAMS
				, { layer = "default", define = "DEPTH" }
				, { layer = "impostor", define = "DEPTH" })
			env.renderBucket(entities.default)
			env.renderBucket(entities.impostor)
			env.setRenderTargets()
		end
		self.last_camera_pos = camera_pos

		env.drawcallUniforms(self.intensity, env.viewport_w, env.viewport_h, offset0, offset1, offset2, self.range, self.depth_range * 0.5, self.scale, self.depth_offset)
		env.bindTextures({gbuffer.DS, env.tdao_rb}, 0)
		env.bindImageTexture(gbuffer.B, 2)
		env.dispatch(env.tdao_shader, (env.viewport_w + 15) / 16, (env.viewport_h + 15) / 16, 1)

		env.endBlock()

--[[
		env.blur_shader = env.blur_shader or env.preloadShader("pipelines/blur.shd")
		env.ssao_blit_shader = env.ssao_blit_shader or env.preloadShader("pipelines/ssao_blit.shd")
		env.ssao_resolve_shader = env.ssao_resolve_shader or env.preloadShader("pipelines/ssao_resolve.shd")

		local w = math.floor(env.viewport_w * 0.5)
		local h = math.floor(env.viewport_h * 0.5)

		if self.use_temporal and self.history_buf == -1 then
			self.history_buf = env.createRenderbuffer(env.ssao_rb_desc)
			env.setRenderTargets(self.history_buf)
			env.clear(env.CLEAR_ALL, 1, 1, 1, 1, 0)
		end

		env.setRenderTargets()

		env.beginBlock("ssao " .. tostring(w) .. "x" .. tostring(h))
		env.drawcallUniforms(self.radius, self.intensity, w, h)
		env.bindTextures({gbuffer.DS, gbuffer.B}, 0)
		env.bindImageTexture(ssao_rb, 2)
		env.dispatch(env.ssao_shader, (w + 15) / 16, (h + 15) / 16, 1)

		env.ssao_blur_rb_desc = env.ssao_blur_rb_desc or env.createRenderbufferDesc { format = "r8", rel_size = {0.5, 0.5}, debug_name = "ssao_blur" }
		if self.use_temporal then
			env.beginBlock("ssao_resolve " .. tostring(w) .. "x" .. tostring(h))
			env.drawcallUniforms( w, h, 0, 0, self.current_frame_weight, 0, 0, 0 )
			env.bindTextures({gbuffer.DS, self.history_buf}, 0)
			env.bindImageTexture(ssao_rb, 2)
			env.dispatch(env.ssao_resolve_shader, (w + 15) / 16, (h + 15) / 16, 1)
			env.endBlock()
			if self.blur then
				env.blur(ssao_rb, w, h, env.ssao_blur_rb_desc)
			end
		else
			if self.blur then
				env.blur(ssao_rb, w, h, env.ssao_blur_rb_desc)
			end
		end

		env.beginBlock("ssao_blit " .. tostring(env.viewport_w) .. "x" .. tostring(env.viewport_h))
		env.drawcallUniforms( env.viewport_w, env.viewport_h, 0, 0 )
		env.bindTextures({ssao_rb}, 0)
		env.bindImageTexture(gbuffer.B, 1)
		env.dispatch(env.ssao_blit_shader, (env.viewport_w + 15) / 16, (env.viewport_h + 15) / 16, 1)
		env.endBlock()
		env.endBlock()

		if self.use_temporal then
			self.history_buf = ssao_rb
			env.keepRenderbufferAlive(self.history_buf)
		end]]
	end
}
