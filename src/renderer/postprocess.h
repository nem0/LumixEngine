#pragma once

#include "engine/engine.h"
#include "engine/resource_manager.h"
#include "pipeline.h"
#include "render_module.h"
#include "renderer.h"
#include "shader.h"
#include "texture.h"
#include <imgui/imgui.h>

namespace Lumix {

struct CubemapSky : public RenderPlugin {
	Renderer& m_renderer;
	Shader* m_shader = nullptr;

	CubemapSky(Renderer& renderer) : m_renderer(renderer) {}

	void shutdown(Renderer& renderer) override {
		m_shader->decRefCount();
	}

	void init() {
		ResourceManagerHub& rm = m_renderer.getEngine().getResourceManager();
		m_shader = rm.load<Shader>(Path("engine/shaders/cubemap_sky.hlsl"));
	}

	RenderBufferHandle renderBeforeTransparent(const GBuffer& gbuffer, RenderBufferHandle input, Pipeline& pipeline) override {
		if (!m_shader->isReady()) return input;

		RenderModule* module = pipeline.getModule();
		EntityPtr env_entity = module->getActiveEnvironment();
		if (!env_entity.isValid()) return input;

		Environment& env = module->getEnvironment(*env_entity);
		if (!env.cubemap_sky) return input;
		if (!env.cubemap_sky->isReady()) return input;

		pipeline.beginBlock("sky");
		m_renderer.setRenderTargets(Span(&input, 1), gbuffer.DS);
		const gpu::StateFlags state = gpu::getStencilStateBits(0, gpu::StencilFuncs::EQUAL, 0, 0xff, gpu::StencilOps::KEEP, gpu::StencilOps::KEEP, gpu::StencilOps::REPLACE);
		struct {
			float intensity;
			gpu::BindlessHandle texture;
		} ub = {
			env.sky_intensity,
			gpu::getBindlessHandle(env.cubemap_sky->handle)
		};
		pipeline.setUniform(ub);
		pipeline.drawArray(0, 3, *m_shader, 0, state);
		pipeline.endBlock();
		return input;
	}
};


struct TDAO : public RenderPlugin {
	Renderer& m_renderer;
	Shader* m_shader = nullptr;
	float m_xz_range = 100;
	float m_y_range = 200;
	float m_intensity = 0.9f;
	bool m_enabled = true;
	float m_scale = 0.01f;
	
	struct PipelineInstanceData {
		RenderBufferHandle rb = INVALID_RENDERBUFFER;
		DVec3 last_camera_pos = DVec3(DBL_MAX);
	};

	TDAO(Renderer& renderer) : m_renderer(renderer) {}

	void shutdown(Renderer& renderer) override {
		m_shader->decRefCount();
	}

	void init() {
		ResourceManagerHub& rm = m_renderer.getEngine().getResourceManager();
		m_shader = rm.load<Shader>(Path("engine/shaders/tdao.hlsl"));
	}

	void debugUI(Pipeline& pipeline) override {
		if (!ImGui::BeginMenu("TDAO")) return;
		ImGui::Checkbox("Enable", &m_enabled);
		ImGui::DragFloat("Intensity", &m_intensity, 0.01f, FLT_MIN, FLT_MAX);
		ImGui::DragFloat("Scale", &m_scale, 0.01f, FLT_MIN, FLT_MAX);
		if (ImGui::RadioButton("Debug", pipeline.m_debug_show_plugin == this)) {
			pipeline.m_debug_show_plugin = this;
			pipeline.m_debug_show = Pipeline::DebugShow::PLUGIN;
		}
		ImGui::EndMenu();
	}

	bool debugOutput(RenderBufferHandle input, Pipeline& pipeline) override {
		if (pipeline.m_debug_show_plugin != this) return false;
		
		auto* data = pipeline.getData<PipelineInstanceData>();
		if (data->rb != INVALID_RENDERBUFFER) {
			const Viewport& vp = pipeline.getViewport();
			pipeline.copy(input, data->rb, {(i32)vp.w, (i32)vp.h}, {1, 0, 0, 0}, {1, 0, 0, 0}, {1, 0, 0, 0});
		}
		return true;
	}

	void renderBeforeLightPass(const GBuffer& gbuffer, Pipeline& pipeline) override {
		if (pipeline.getType() == PipelineType::PREVIEW) return;
		PROFILE_FUNCTION();
		auto* inst_data = pipeline.getData<PipelineInstanceData>();
		Renderer& renderer = pipeline.getRenderer();

		if (!m_enabled) {
			if (inst_data->rb != INVALID_RENDERBUFFER) {
				renderer.releaseRenderbuffer(inst_data->rb);
				inst_data->rb = INVALID_RENDERBUFFER;
			}
			inst_data->last_camera_pos = DVec3(DBL_MAX);
			return;
		}

		pipeline.beginBlock("tdao");
		if (inst_data->rb == INVALID_RENDERBUFFER) {
			inst_data->rb = renderer.createRenderbuffer({
				.size = IVec2(512, 512),
				.format = gpu::TextureFormat::D32,
				.debug_name = "tdao"
			});
		}
		DrawStream& stream = renderer.getDrawStream();

		const Viewport& vp = pipeline.getViewport();
		const bool camera_moved = fabs(vp.pos.x - inst_data->last_camera_pos.x) > 3 || fabs(vp.pos.y - inst_data->last_camera_pos.y) > 3 || fabs(vp.pos.z - inst_data->last_camera_pos.z) > 3;
		if (camera_moved) {
			inst_data->last_camera_pos = vp.pos;
			renderer.setRenderTargets({}, inst_data->rb);
			pipeline.clear(gpu::ClearFlags::ALL, 0, 0, 0, 1, 0);

			CameraParams cp;
			const Quat rot(-0.707106769f, 0, 0, 0.707106769f);
			cp.pos = vp.pos;
			ShiftedFrustum frustum;
			const float ratio = 1;
			frustum.computeOrtho({ 0, 0, 0 },
				rot * Vec3(0, 0, 1),
				rot * Vec3(0, 1, 0),
				m_xz_range,
				m_xz_range,
				-0.5f * m_y_range,
				0.5f * m_y_range);
			frustum.origin = vp.pos;
			cp.frustum = frustum;
			cp.lod_multiplier = 1;
			cp.is_shadow = false;

			cp.view = rot.toMatrix().fastInverted();
			cp.projection.setOrtho(-m_xz_range * ratio,
				m_xz_range * ratio,
				-m_xz_range,
				m_xz_range,
				-0.5f * m_y_range,
				0.5f * m_y_range,
				true);

			pipeline.viewport(0, 0, 512, 512);
			pipeline.pass(cp);

			BucketDesc buckets[] = {{.layer = "default", .define = "DEPTH"}, {.layer = "impostor", .define = "DEPTH"}};

			u32 view_id = pipeline.cull(cp, buckets);
			pipeline.renderBucket(view_id, 0);
			pipeline.renderBucket(view_id, 1);
		}

		struct {
			Vec4 offset;
			Vec2 rcp_size;
			float intensity;
			float rcp_range;
			float half_depth_range;
			float scale;
			float depth_offset;
			gpu::BindlessHandle u_depth_buffer;
			gpu::RWBindlessHandle u_gbufferB;
			gpu::BindlessHandle u_topdown_depthmap;
		} ubdata = {
			Vec4(Vec3(vp.pos - inst_data->last_camera_pos), 0),
			Vec2(1.f / vp.w, 1.f / vp.h),
			m_intensity,
			1.f / m_xz_range,
			m_y_range * 0.5f,
			m_scale,
			0.02f,
			pipeline.toBindless(gbuffer.DS, stream),
			pipeline.toRWBindless(gbuffer.B, stream),
			pipeline.toBindless(inst_data->rb, stream),
		};

		pipeline.setUniform(ubdata);
		pipeline.dispatch(*m_shader, (vp.w + 15) / 16, (vp.h + 15) / 16, 1);
		
		pipeline.endBlock();
	}
};




} // namespace