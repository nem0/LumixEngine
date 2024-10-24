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

struct Atmo : public RenderPlugin {
	Renderer& m_renderer;
	Shader* m_shader = nullptr;
	Shader* m_scattering_shader = nullptr;
	Shader* m_optical_depth_shader = nullptr;
	gpu::TextureHandle m_optical_depth_precomputed = gpu::INVALID_TEXTURE;
	gpu::TextureHandle m_inscatter_precomputed = gpu::INVALID_TEXTURE;

	Atmo(Renderer& renderer)
		: m_renderer(renderer)
	{}
	
	void shutdown() {
		if (m_optical_depth_precomputed != gpu::INVALID_TEXTURE) {
			m_renderer.getEndFrameDrawStream().destroy(m_optical_depth_precomputed);
			m_renderer.getEndFrameDrawStream().destroy(m_inscatter_precomputed);
		}
		m_shader->decRefCount();
		m_scattering_shader->decRefCount();
		m_optical_depth_shader->decRefCount();
	}

	void init() {
		ResourceManagerHub& rm = m_renderer.getEngine().getResourceManager();
		m_shader = rm.load<Shader>(Path("shaders/atmo.hlsl"));
		m_scattering_shader = rm.load<Shader>(Path("shaders/atmo_scattering.hlsl"));
		m_optical_depth_shader = rm.load<Shader>(Path("shaders/atmo_optical_depth.hlsl"));
	}

	RenderBufferHandle renderBeforeTransparent(const GBuffer& gbuffer, RenderBufferHandle hdr_rb, Pipeline& pipeline) override {
		PROFILE_FUNCTION();
		if (pipeline.getType() == PipelineType::PREVIEW) return hdr_rb;

		RenderModule* module = pipeline.getModule();
		EntityPtr env_entity = module->getActiveEnvironment();
		if (!env_entity.isValid()) return hdr_rb;
		Environment& env = module->getEnvironment(*env_entity);
		if (!env.atmo_enabled) return hdr_rb;

		if (m_optical_depth_precomputed == gpu::INVALID_TEXTURE) {
			const gpu::TextureFlags flags = gpu::TextureFlags::COMPUTE_WRITE | gpu::TextureFlags::NO_MIPS;
			m_optical_depth_precomputed = pipeline.getRenderer().createTexture(128, 128, 1, gpu::TextureFormat::RG32F, flags, {}, "optical_depth_precomputed");
			m_inscatter_precomputed = pipeline.getRenderer().createTexture(64, 128, 1, gpu::TextureFormat::RGBA32F, flags, {}, "inscatter_precomputed");
		}
		
		DrawStream& stream = pipeline.getRenderer().getDrawStream();
		pipeline.beginBlock("atmo");

		struct {
			float bot;
			float top;
			float distribution_rayleigh;
			float distribution_mie;
			Vec4 scatter_rayleigh;
			Vec4 scatter_mie;
			Vec4 absorb_mie;
			Vec4 sunlight;
			Vec4 resolution;
			Vec4 fog_scattering;
			float fog_top;
			float fog_enabled;
			float godarys_enabled;
			gpu::RWBindlessHandle output;
			gpu::BindlessHandle optical_depth;
			gpu::BindlessHandle depth_buffer;
			gpu::BindlessHandle inscatter_precomputed;
		} ub_data = {
			env.ground_r * 1000,
			env.atmo_r * 1000,
			env.height_distribution_rayleigh,
			env.height_distribution_mie,
			Vec4(env.scatter_rayleigh, 0) * 33.1f * 0.000001f,
			Vec4(env.scatter_mie, 0) * 3.996f * 0.000001f,
			Vec4(env.absorb_mie, 0) * 4.4f * 0.000001f,
			Vec4(env.sunlight_color, env.sunlight_strength),
			Vec4(128, 128, 1, 0),
			Vec4(env.fog_scattering * env.fog_density * 0.0001f, 0),
			env.fog_top,
			env.fog_density > 0 ? 1.f : 0.f,
			env.godrays_enabled ? 1.f : 0.f,
			gpu::getRWBindlessHandle(m_optical_depth_precomputed),
			gpu::INVALID_BINDLESS_HANDLE,
			pipeline.toBindless(gbuffer.DS, stream),
			gpu::INVALID_BINDLESS_HANDLE
		};

		stream.barrierWrite(m_optical_depth_precomputed);
		pipeline.beginBlock("precompute_transmittance");
		pipeline.setUniform(ub_data);
		pipeline.dispatch(*m_optical_depth_shader, 128 / 16, 128 / 16, 1);
		pipeline.endBlock();
		
		stream.barrierWrite(m_inscatter_precomputed);
		stream.memoryBarrier(m_optical_depth_precomputed);
		stream.barrierRead(m_optical_depth_precomputed);

		pipeline.beginBlock("precompute_inscatter");
		ub_data.resolution = Vec4(64, 128, 1, 0);
		ub_data.output = gpu::getRWBindlessHandle(m_inscatter_precomputed);
		pipeline.setUniform(ub_data);
		pipeline.dispatch(*m_scattering_shader, 64 / 16, 128 / 16, 1);
		pipeline.endBlock();
		
		stream.memoryBarrier(m_inscatter_precomputed);
		stream.barrierRead(m_inscatter_precomputed);
		
		ub_data.inscatter_precomputed = gpu::getBindlessHandle(m_inscatter_precomputed);
		ub_data.optical_depth = gpu::getBindlessHandle(m_optical_depth_precomputed);
		ub_data.output = pipeline.toRWBindless(hdr_rb, stream);
		pipeline.setUniform(ub_data);
		const Viewport& vp = pipeline.getViewport();
		pipeline.dispatch(*m_shader, (vp.w + 15) / 16, (vp.h + 15) / 16, 1);

		pipeline.endBlock();
		return hdr_rb;
	}
};

struct FilmGrain : public RenderPlugin {
	Renderer& m_renderer;
	Shader* m_shader = nullptr;
	float m_noise_scale = 2.f;

	FilmGrain(Renderer& renderer) : m_renderer(renderer) {}

	void shutdown() {
		m_shader->decRefCount();
	}

	void init() {
		ResourceManagerHub& rm = m_renderer.getEngine().getResourceManager();
		m_shader = rm.load<Shader>(Path("shaders/film_grain.hlsl"));
	}

	RenderBufferHandle renderAfterTonemap(const GBuffer& gbuffer, RenderBufferHandle input, Pipeline& pipeline) override {
		if (!m_shader->isReady()) return input;
		if (pipeline.getType() != PipelineType::GAME_VIEW) return input;

		RenderModule* module = pipeline.getModule();
		EntityPtr camera_entity = module->getActiveCamera();
		if (!camera_entity.isValid()) return input;

		Camera& camera = module->getCamera(*camera_entity);
		if (camera.film_grain_intensity <= 1e-5) return input;

		const RenderBufferHandle ldr_buffer = INVALID_RENDERBUFFER;
		pipeline.beginBlock("film_grain");

		DrawStream& stream = pipeline.getRenderer().getDrawStream();
		const Viewport& vp = pipeline.getViewport();
		struct {
			float intensity;
			float lumamount;
			gpu::RWBindlessHandle source;
		} ubdata = {
			camera.film_grain_intensity,
			0.1f,
			pipeline.toRWBindless(input, stream),
		};
		pipeline.setUniform(ubdata);
		pipeline.dispatch(*m_shader, (vp.w + 15) / 16, (vp.h + 15) / 16, 1);

		pipeline.endBlock();
		return input;
	}
};

struct DOF : public RenderPlugin {
	Renderer& m_renderer;
	Shader* m_shader = nullptr;

	DOF(Renderer& renderer) : m_renderer(renderer) {}

	void shutdown() {
		m_shader->decRefCount();
	}

	void init() {
		ResourceManagerHub& rm = m_renderer.getEngine().getResourceManager();
		m_shader = rm.load<Shader>(Path("shaders/dof.hlsl"));
	}

	RenderBufferHandle renderBeforeTonemap(const GBuffer& gbuffer, RenderBufferHandle input, Pipeline& pipeline) override {
		if (!m_shader->isReady()) return input;
		if (pipeline.getType() != PipelineType::GAME_VIEW) return input;

		RenderModule* module = pipeline.getModule();
		EntityPtr camera_entity = module->getActiveCamera();
		if (!camera_entity.isValid()) return input;

		Camera& camera = module->getCamera(*camera_entity);
		if (!camera.dof_enabled) return input;

		pipeline.beginBlock("dof");
		RenderBufferHandle dof_rb = pipeline.createRenderbuffer({
			.format = gpu::TextureFormat::RGBA16F,
			.flags = gpu::TextureFlags::COMPUTE_WRITE | gpu::TextureFlags::NO_MIPS | gpu::TextureFlags::RENDER_TARGET,
			.debug_name = "dof"
		});
		
		DrawStream& stream = pipeline.getRenderer().getDrawStream();
		struct {
			float distance;
			float range;
			float max_blur_size;
			float sharp_range;
			gpu::BindlessHandle texture;
			gpu::BindlessHandle depth;
			gpu::RWBindlessHandle output;
		}	ub = {
			camera.dof_distance,
			camera.dof_range,
			camera.dof_max_blur_size,
			camera.dof_sharp_range,
			pipeline.toBindless(input, stream),
			pipeline.toBindless(gbuffer.DS, stream),
			pipeline.toRWBindless(dof_rb, stream),
		};

		const Viewport& vp = pipeline.getViewport();
		pipeline.setUniform(ub);
		pipeline.dispatch(*m_shader, (vp.w + 15) / 16, (vp.h + 15) / 16, 1);

		pipeline.blit(pipeline.toBindless(dof_rb, stream), pipeline.toRWBindless(input, stream), {(i32)vp.w, (i32)vp.h});

		pipeline.endBlock();
		return input;
	}
};

struct CubemapSky : public RenderPlugin {
	Renderer& m_renderer;
	Shader* m_shader = nullptr;

	CubemapSky(Renderer& renderer) : m_renderer(renderer) {}

	void shutdown() {
		m_shader->decRefCount();
	}

	void init() {
		ResourceManagerHub& rm = m_renderer.getEngine().getResourceManager();
		m_shader = rm.load<Shader>(Path("shaders/cubemap_sky.hlsl"));
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
		pipeline.setRenderTargets(Span(&input, 1), gbuffer.DS);
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


struct Bloom : public RenderPlugin {
	Renderer& m_renderer;
	Shader* m_shader = nullptr;
	Shader* m_extract_shader = nullptr;
	Shader* m_downscale_shader = nullptr;
	Shader* m_tonemap_shader = nullptr;
	Shader* m_blur_shader = nullptr;
	Shader* m_avg_luminance_shader = nullptr;
	Shader* m_bloom_blur_shader = nullptr;
	gpu::BufferHandle m_lum_buf = gpu::INVALID_BUFFER;
	RenderBufferHandle m_extracted_rt = INVALID_RENDERBUFFER; // for debug view

	Bloom(Renderer& renderer)
		: m_renderer(renderer)
	{}

	void shutdown() {
		m_shader->decRefCount();
		m_extract_shader->decRefCount();
		m_downscale_shader->decRefCount();
		m_tonemap_shader->decRefCount();
		m_blur_shader->decRefCount();
		m_avg_luminance_shader->decRefCount();
		m_bloom_blur_shader->decRefCount();
		m_renderer.getEndFrameDrawStream().destroy(m_lum_buf);
	}

	void init() {
		ResourceManagerHub& rm = m_renderer.getEngine().getResourceManager();
		m_shader = rm.load<Shader>(Path("shaders/bloom.hlsl"));
		m_extract_shader = rm.load<Shader>(Path("shaders/bloom_extract.hlsl"));
		m_downscale_shader = rm.load<Shader>(Path("shaders/bloom_downscale.hlsl"));
		m_tonemap_shader = rm.load<Shader>(Path("shaders/bloom_tonemap.hlsl"));
		m_blur_shader = rm.load<Shader>(Path("shaders/blur.hlsl"));
		m_avg_luminance_shader = rm.load<Shader>(Path("shaders/avg_luminance.hlsl"));
		m_bloom_blur_shader = rm.load<Shader>(Path("shaders/bloom_blur.hlsl"));
		m_lum_buf = m_renderer.createBuffer({2048}, gpu::BufferFlags::SHADER_BUFFER, "bloom");
	}

	void debugUI(Pipeline& pipeline) override {
		if (ImGui::RadioButton("Bloom", pipeline.m_debug_show_plugin == this)) {
			pipeline.m_debug_show_plugin = this;
			pipeline.m_debug_show = Pipeline::DebugShow::PLUGIN;
		}
	}

	bool debugOutput(RenderBufferHandle input, Pipeline& pipeline) override {
		if (pipeline.m_debug_show_plugin != this) return false;
		const Viewport& vp = pipeline.getViewport();
		pipeline.copy(input, m_extracted_rt, {(i32)vp.w, (i32)vp.h});
		pipeline.keepRenderbufferAlive(m_extracted_rt);
		return true;
	}

	void computeAvgLuminance(RenderBufferHandle input, float accomodation_speed, Pipeline& pipeline) {
		pipeline.beginBlock("autoexposure");

		const Viewport& vp = pipeline.getViewport();
		DrawStream& stream = pipeline.getRenderer().getDrawStream();

		struct {
			float accomodation_speed;
			gpu::BindlessHandle image;
			gpu::RWBindlessHandle histogram;
		} ubdata = {
			accomodation_speed,
			pipeline.toBindless(input, stream),
			gpu::getRWBindlessHandle(m_lum_buf)
		};
		pipeline.setUniform(ubdata);
		stream.barrierWrite(m_lum_buf);
		stream.memoryBarrier(m_lum_buf);
		pipeline.dispatch(*m_avg_luminance_shader, 1, 1, 1, "PASS0");
		stream.memoryBarrier(m_lum_buf);
		pipeline.dispatch(*m_avg_luminance_shader, (vp.w + 15) / 16, (vp.h + 15) / 16, 1);
		stream.memoryBarrier(m_lum_buf);
		pipeline.dispatch(*m_avg_luminance_shader, 1, 1, 1, "PASS2");
		stream.memoryBarrier(m_lum_buf);
		stream.barrierRead(m_lum_buf);

		pipeline.endBlock();
	}

	RenderBufferHandle downscale(RenderBufferHandle big, RenderbufferDesc small_desc, Pipeline& pipeline) {
		ASSERT(small_desc.type == RenderbufferDesc::FIXED);
		RenderBufferHandle small = pipeline.createRenderbuffer(small_desc);
		DrawStream& stream = pipeline.getRenderer().getDrawStream();
		struct {
			gpu::BindlessHandle input;
			gpu::RWBindlessHandle output;
		} ubdata = {
			pipeline.toBindless(big, stream),
			pipeline.toRWBindless(small, stream)
		};
		stream.memoryBarrier(pipeline.toTexture(big));
		pipeline.setUniform(ubdata);
		pipeline.dispatch(*m_downscale_shader, (small_desc.fixed_size.x + 15) / 16, (small_desc.fixed_size.y + 15 ) / 16, 1);
		return small;
	}


	void blurUpscale(IVec2 big_size, RenderBufferHandle big, RenderBufferHandle small, Pipeline& pipeline) {
		RenderBufferHandle blur_buf = pipeline.createRenderbuffer({
			.type = RenderbufferDesc::FIXED,
			.fixed_size = big_size,
			.format = gpu::TextureFormat::RGBA16F,
			.debug_name = "bloom_blur"
			});
		pipeline.setRenderTargets(Span(&blur_buf, 1));
		const u32 blur_h_mask = 1 << pipeline.getRenderer().getShaderDefineIdx("BLUR_H");
		DrawStream& stream = pipeline.getRenderer().getDrawStream();
		struct {
			Vec4 inv_sm_size;
			gpu::BindlessHandle input;
			gpu::BindlessHandle input2;
		} ubdata = {
			Vec4(1.0f / (float)big_size.x, 1.0f / (float)big_size.y, 0, 0), 
			pipeline.toBindless(big, stream),
			pipeline.toBindless(small, stream)
		};
		pipeline.setUniform(ubdata);
		pipeline.drawArray(0, 3, *m_bloom_blur_shader, blur_h_mask, gpu::StateFlags::NONE);
		
		pipeline.setRenderTargets(Span(&big, 1));
		ubdata.input = pipeline.toBindless(blur_buf, stream);
		pipeline.setUniform(ubdata);
		pipeline.drawArray(0, 3, *m_blur_shader, 0, gpu::StateFlags::NONE);
	}

	void blur(IVec2 size, RenderBufferHandle src, Pipeline& pipeline) {
		RenderBufferHandle blur_buf = pipeline.createRenderbuffer({
			.type = RenderbufferDesc::FIXED,
			.fixed_size = size,
			.format = gpu::TextureFormat::RGBA16F, 
			.debug_name = "bloom_blur"
		});
		const u32 blur_h_mask = 1 << pipeline.getRenderer().getShaderDefineIdx("BLUR_H");

		DrawStream& stream = pipeline.getRenderer().getDrawStream();
		struct {
			Vec4 inv_sm_size;
			gpu::BindlessHandle input;
		} ubdata = {
			Vec4(1.0f / (float)size.x, 1.0f / (float)size.y, 0, 0), 
			pipeline.toBindless(src, stream)
		};

		pipeline.setRenderTargets(Span(&blur_buf, 1));
		pipeline.setUniform(ubdata);
		pipeline.drawArray(0, 3, *m_blur_shader, blur_h_mask, gpu::StateFlags::NONE);

		pipeline.setRenderTargets(Span(&src, 1));
		ubdata.input = pipeline.toBindless(blur_buf, stream);
		pipeline.setUniform(ubdata);
		pipeline.drawArray(0, 3, *m_blur_shader, 0, gpu::StateFlags::NONE);
	}

	const Camera* getCamera(Pipeline& pipeline) {
		RenderModule* module = pipeline.getModule();
		EntityPtr camera_entity = module->getActiveCamera();
		if (!camera_entity.isValid()) return nullptr;
		return &module->getCamera(*camera_entity);
	}

	// TODO fix - this extracts UI
	RenderBufferHandle renderBeforeTonemap(const GBuffer& gbuffer, RenderBufferHandle input, Pipeline& pipeline) override {
		m_extracted_rt = INVALID_RENDERBUFFER;

		if (pipeline.getType() != PipelineType::GAME_VIEW) return input;
		if (!m_shader->isReady()) return input;
		if (!m_tonemap_shader->isReady()) return input;
		if (!m_avg_luminance_shader->isReady()) return input;
		if (!m_blur_shader->isReady()) return input;
		if (!m_bloom_blur_shader->isReady()) return input;

		const Camera* camera = getCamera(pipeline);
		if (!camera || !camera->bloom_enabled) return input;

		pipeline.beginBlock("bloom");
		computeAvgLuminance(input, camera->bloom_accomodation_speed, pipeline);

		RenderBufferHandle bloom_rb = pipeline.createRenderbuffer({
			.rel_size = {0.5f, 0.5f},
			.format = gpu::TextureFormat::RGBA16F, 
			.flags = gpu::TextureFlags::COMPUTE_WRITE | gpu::TextureFlags::NO_MIPS | gpu::TextureFlags::RENDER_TARGET,
			.debug_name = "bloom" 
		});
	
		const Viewport& vp = pipeline.getViewport();
		pipeline.viewport(0, 0, vp.w >> 1, vp.h >> 1);
		DrawStream& stream = pipeline.getRenderer().getDrawStream();
		struct {
			float avg_lum_multiplier;
			gpu::BindlessHandle histogram;
			gpu::BindlessHandle input;
			gpu::RWBindlessHandle output;
		} ubdata = {
			camera->bloom_avg_bloom_multiplier,
			gpu::getBindlessHandle(m_lum_buf), 
			pipeline.toBindless(input, stream),
			pipeline.toRWBindless(bloom_rb, stream)
		};
		stream.barrierRead(m_lum_buf);
		pipeline.setUniform(ubdata);
		pipeline.dispatch(*m_extract_shader, ((vp.w >> 1) + 15) / 16, ((vp.h >> 1) + 15) / 16, 1);
		m_extracted_rt = bloom_rb;

		if (pipeline.m_debug_show_plugin == this) {
			pipeline.endBlock();
			return input;
		}

		RenderBufferHandle bloom2_rb = downscale(bloom_rb, { 
			.type = RenderbufferDesc::FIXED,
			.fixed_size = {vp.w >> 2, vp.h >> 2},
			.format = gpu::TextureFormat::RGBA16F,
			.flags = gpu::TextureFlags::COMPUTE_WRITE | gpu::TextureFlags::NO_MIPS | gpu::TextureFlags::RENDER_TARGET,
			.debug_name = "bloom2"
		}, pipeline);
		RenderBufferHandle bloom4_rb = downscale(bloom2_rb, {
			.type = RenderbufferDesc::FIXED, 
			.fixed_size = {vp.w >> 3, vp.h >> 3}, 
			.format = gpu::TextureFormat::RGBA16F,
			.flags = gpu::TextureFlags::COMPUTE_WRITE | gpu::TextureFlags::NO_MIPS | gpu::TextureFlags::RENDER_TARGET,
			.debug_name = "bloom4"	
		}, pipeline);
		RenderBufferHandle bloom8_rb = downscale(bloom4_rb, {
			.type = RenderbufferDesc::FIXED, 
			.fixed_size = {vp.w >> 4, vp.h >> 4}, 
			.format = gpu::TextureFormat::RGBA16F,
			.flags = gpu::TextureFlags::COMPUTE_WRITE | gpu::TextureFlags::NO_MIPS | gpu::TextureFlags::RENDER_TARGET,
			.debug_name = "bloom8"
		}, pipeline);
		RenderBufferHandle bloom16_rb = downscale(bloom8_rb, {
			.type = RenderbufferDesc::FIXED, 
			.fixed_size = {vp.w >> 5, vp.h >> 5}, 
			.format = gpu::TextureFormat::RGBA16F,
			.flags = gpu::TextureFlags::COMPUTE_WRITE | gpu::TextureFlags::NO_MIPS | gpu::TextureFlags::RENDER_TARGET,
			.debug_name = "bloom16"
		}, pipeline);

		blur({ vp.w >> 5, vp.h >> 5 }, bloom16_rb, pipeline);
		blurUpscale({ vp.w >> 4, vp.h >> 4 }, bloom8_rb, bloom16_rb, pipeline);
		blurUpscale({ vp.w >> 3, vp.h >> 3 }, bloom4_rb, bloom8_rb, pipeline);
		blurUpscale({ vp.w >> 2, vp.h >> 2 }, bloom2_rb, bloom4_rb, pipeline);
		blurUpscale({ vp.w >> 1, vp.h >> 1 }, bloom_rb, bloom2_rb, pipeline);

		struct {
			gpu::BindlessHandle bloom;
			gpu::RWBindlessHandle output;
		} ub = {
			pipeline.toBindless(bloom_rb, stream),
			pipeline.toRWBindless(input, stream)
		};
		pipeline.setUniform(ub);
		pipeline.dispatch(*m_shader, (vp.w + 15) / 16, (vp.h + 15) / 16, 1);

		pipeline.endBlock();
		return input;
	}
	
	bool tonemap(RenderBufferHandle input, RenderBufferHandle& output, Pipeline& pipeline) override {
		const Camera* camera = getCamera(pipeline);
		if (pipeline.getType() != PipelineType::GAME_VIEW) return false;
		if (!camera || !camera->bloom_tonemap_enabled || !camera->bloom_enabled) return false;
		if (!m_tonemap_shader->isReady()) return false;

		const bool is_scene_view = pipeline.getType() == PipelineType::SCENE_VIEW;
		pipeline.beginBlock("bloom tonemap");
		RenderBufferHandle rb = pipeline.createRenderbuffer({
			.format = is_scene_view ? gpu::TextureFormat::RGBA16F : gpu::TextureFormat::RGBA8,
			.flags = gpu::TextureFlags::COMPUTE_WRITE | gpu::TextureFlags::NO_MIPS | gpu::TextureFlags::RENDER_TARGET,
			.debug_name = "tonemap_bloom"
		});
		
		DrawStream& stream = pipeline.getRenderer().getDrawStream();
		struct {
			float exposure;
			gpu::BindlessHandle input;
			gpu::BindlessHandle accum;
			gpu::RWBindlessHandle output;
		} ubdata = {
			camera->bloom_exposure,
			pipeline.toBindless(input, stream),
			gpu::getBindlessHandle(m_lum_buf),
			pipeline.toRWBindless(rb, stream)
		};

		stream.barrierRead(m_lum_buf);
		pipeline.setUniform(ubdata); 
		const Viewport& vp = pipeline.getViewport();
		pipeline.dispatch(*m_tonemap_shader, (vp.w + 15) / 16, (vp.h + 15) / 16, 1);
		pipeline.endBlock();
		output = rb;
		return true;
	}

};

struct SSS : public RenderPlugin {
	Renderer& m_renderer;
	Shader* m_shader = nullptr;
	Shader* m_shader_blit = nullptr;
	u32 m_max_steps = 20;
	float m_stride = 4;
	float m_current_frame_weight = 0.1f;
	bool m_is_enabled = false;

	struct PipelineInstanceData {
		RenderBufferHandle history = INVALID_RENDERBUFFER;
	};

	SSS(Renderer& renderer) : m_renderer(renderer) {}

	void shutdown() {
		m_shader->decRefCount();
		m_shader_blit->decRefCount();
	}

	void init() {
		ResourceManagerHub& rm = m_renderer.getEngine().getResourceManager();
		m_shader = rm.load<Shader>(Path("shaders/sss.hlsl"));
		m_shader_blit = rm.load<Shader>(Path("shaders/sss_blit.hlsl"));
	}

	void debugUI(Pipeline& pipeline) override {

		if (!ImGui::BeginMenu("SSS")) return;

		ImGui::Checkbox("Enable", &m_is_enabled);
		
		if (ImGui::RadioButton("Debug", pipeline.m_debug_show_plugin == this)) {
			pipeline.m_debug_show_plugin = this;
			pipeline.m_debug_show = Pipeline::DebugShow::PLUGIN;
		}
		ImGui::EndMenu();
	}

	bool debugOutput(RenderBufferHandle input, Pipeline& pipeline) override {
		if (pipeline.m_debug_show_plugin != this) return false;
		RenderBufferHandle rb = pipeline.getData<PipelineInstanceData>()->history;
		const Viewport& vp = pipeline.getViewport();
		if (rb != INVALID_RENDERBUFFER) pipeline.copy(input, rb, {(i32)vp.w, (i32)vp.h});
		return true;
	}

	void renderBeforeLightPass(const GBuffer& gbuffer, Pipeline& pipeline) override {
		if (!m_shader->isReady()) return;
		if (!m_shader_blit->isReady()) return;
		
		PipelineInstanceData* data = pipeline.getData<PipelineInstanceData>();	
		if (!m_is_enabled) {
			data->history = INVALID_RENDERBUFFER;
			return;
		}
		
		pipeline.beginBlock("SSS");
		const RenderbufferDesc rb_desc = {
			.format = gpu::TextureFormat::R8,
			.flags = gpu::TextureFlags::COMPUTE_WRITE | gpu::TextureFlags::RENDER_TARGET,
			.debug_name = "sss"
		};
		const RenderBufferHandle sss =  pipeline.createRenderbuffer(rb_desc);

		if (data->history == INVALID_RENDERBUFFER) {
			data->history = pipeline.createRenderbuffer(rb_desc);
			pipeline.setRenderTargets(Span(&data->history, 1));
			pipeline.clear(gpu::ClearFlags::ALL, 1, 1, 1, 1, 0);
		}

		const Viewport& vp = pipeline.getViewport();
		DrawStream& stream = pipeline.getRenderer().getDrawStream();

		struct {
			float max_steps;
			float stride;
			gpu::BindlessHandle depth;
			gpu::RWBindlessHandle sss_buffer;
		} ubdata = {
			(float)m_max_steps,
			m_stride,
			pipeline.toBindless(gbuffer.DS, stream),
			pipeline.toRWBindless(sss, stream)
		};
		pipeline.setUniform(ubdata);
		pipeline.dispatch(*m_shader, (vp.w + 15) / 16, (vp.h + 15) / 16, 1);
		stream.memoryBarrier(pipeline.toTexture(sss));

		struct {
			float current_frame_weight;
			gpu::RWBindlessHandle sss;
			gpu::BindlessHandle history;
			gpu::BindlessHandle depthbuf;
			gpu::RWBindlessHandle gbufferC;
		} ubdata2 = {
			m_current_frame_weight,
			pipeline.toRWBindless(sss, stream),
			pipeline.toBindless(data->history, stream),
			pipeline.toBindless(gbuffer.DS, stream),
			pipeline.toRWBindless(gbuffer.C, stream)
		};

		pipeline.setUniform(ubdata2);
		pipeline.dispatch(*m_shader_blit, (vp.w + 15) / 16, (vp.h + 15) / 16, 1);
		stream.memoryBarrier(pipeline.toTexture(gbuffer.C));

		data->history = sss;
		pipeline.keepRenderbufferAlive(data->history);
		pipeline.endBlock();
	}
};

struct SSAO : public RenderPlugin {
	Renderer& m_renderer;
	Shader* m_shader = nullptr;
	Shader* m_blit_shader = nullptr;
	Shader* m_blur_shader = nullptr;
	Shader* m_downscale_shader = nullptr;
	bool m_enabled = true;
	bool m_temporal = true;
	u32 m_blur_iterations = 1;
	u32 m_downscale = 1;
	float m_depth_diff_weight = 2;
	float m_radius = 0.4f;
	float m_intensity = 1.f;
	RenderBufferHandle m_temporal_rb;
	IVec2 m_temporal_size;

	SSAO(Renderer& renderer) : m_renderer(renderer) {}
	
	void shutdown() {
		m_shader->decRefCount();
		m_blit_shader->decRefCount();
		m_blur_shader->decRefCount();
		m_downscale_shader->decRefCount();
	}

	void init() {
		ResourceManagerHub& rm = m_renderer.getEngine().getResourceManager();
		m_shader = rm.load<Shader>(Path("shaders/ssao.hlsl"));
		m_blit_shader = rm.load<Shader>(Path("shaders/ssao_blit.hlsl"));
		m_blur_shader = rm.load<Shader>(Path("shaders/ssao_blur.hlsl"));
		m_downscale_shader = rm.load<Shader>(Path("shaders/ssao_downscale_depth.hlsl"));
	}

	void debugUI(Pipeline& pipeline) override {
		if (ImGui::BeginMenu("SSAO")) {
			ImGui::Checkbox("Enabled", &m_enabled);
			ImGui::Checkbox("Temporal", &m_temporal);
			ImGui::DragFloat("Radius", &m_radius, 0.1f, FLT_MIN, FLT_MAX);
			ImGui::DragFloat("Intensity", &m_intensity, 0.1f, FLT_MIN, FLT_MAX);
			ImGui::DragFloat("Depth difference weight", &m_depth_diff_weight, 0.1f, FLT_MIN, FLT_MAX);
			ImGui::DragInt("Blur iterations", (i32*)&m_blur_iterations, 1, 0, 50);
			const char* downscale_values[] = { "Disabled", "2x", "4x" };
			ImGui::TextUnformatted("Downscale");
			for (const char*& v : downscale_values) {
				const u32 idx = u32(&v - downscale_values);
				ImGui::SameLine();
				if (ImGui::RadioButton(v, m_downscale == idx)) m_downscale = idx;
			} 
			ImGui::EndMenu();
		}
	}

	void renderBeforeLightPass(const GBuffer& gbuffer, Pipeline& pipeline) override {
		PROFILE_FUNCTION();
		if (!m_shader->isReady()) return;
		if (!m_blit_shader->isReady()) return;
		if (!m_blur_shader->isReady()) return;
		if (!m_downscale_shader->isReady()) return;
		if (!m_enabled) return;

		const Viewport& vp = pipeline.getViewport();
		const u32 width = vp.w >> m_downscale;
		const u32 height = vp.h >> m_downscale;
		RenderBufferHandle ssao_rb = pipeline.createRenderbuffer({ 
			.type = RenderbufferDesc::FIXED,
			.fixed_size = IVec2(width, height),
			.format = gpu::TextureFormat::RGBA8,
			.flags = gpu::TextureFlags::COMPUTE_WRITE,
			.debug_name = "ssao"
		});

		DrawStream& stream = pipeline.getRenderer().getDrawStream();
		RenderBufferHandle depth_buffer = gbuffer.DS;
		pipeline.beginBlock("ssao");
		if (m_downscale > 0) {
			depth_buffer = pipeline.createRenderbuffer({ 
				.type = RenderbufferDesc::FIXED,
				.fixed_size = IVec2(width, height),
				.format = gpu::TextureFormat::R32F, 
				.flags = gpu::TextureFlags::COMPUTE_WRITE | gpu::TextureFlags::NO_MIPS, 
				.debug_name = "ssao downscaled depth"
			});
			pipeline.beginBlock("ssao downscale depth");
			struct {
				u32 scale;
				gpu::BindlessHandle input;
				gpu::RWBindlessHandle output;
			} udata = {
				.scale = u32(1 << m_downscale),
				.input = pipeline.toBindless(gbuffer.DS, stream),
				.output = pipeline.toRWBindless(depth_buffer, stream)
			};
			pipeline.setUniform(udata);
			pipeline.dispatch(*m_downscale_shader, (width + 7) / 8, (height + 7) / 8, 1);
			pipeline.endBlock();
		}

		if (m_temporal) {
			if (m_temporal_rb == INVALID_RENDERBUFFER || width != m_temporal_size.x || height != m_temporal_size.y) {
				m_temporal_rb = pipeline.createRenderbuffer({
					.type = RenderbufferDesc::FIXED,
					.fixed_size = IVec2(width, height),
					.format = gpu::TextureFormat::R8,
					.flags = gpu::TextureFlags::COMPUTE_WRITE | gpu::TextureFlags::NO_MIPS | gpu::TextureFlags::RENDER_TARGET,
					.debug_name = "ssao_temporal"
				});
				m_temporal_size = IVec2(width, height);
				// TODO compute shader
				pipeline.setRenderTargets(Span(&m_temporal_rb, 1), INVALID_RENDERBUFFER);
				pipeline.clear(gpu::ClearFlags::ALL, 1, 1, 1, 1, 1);
			}
			pipeline.keepRenderbufferAlive(m_temporal_rb);
		}
		else {
			m_temporal_rb = INVALID_RENDERBUFFER;
		}

		struct {
			Vec2 rcp_size;
			float radius;
			float intensity;
			u32 downscale;
			gpu::BindlessHandle normal_buffer;
			gpu::BindlessHandle depth_buffer;
			gpu::BindlessHandle history;
			gpu::BindlessHandle motion_vectors;
			gpu::RWBindlessHandle output;
		} udata = {
			.rcp_size = Vec2(1.0f / width, 1.0f / height),
			.radius = m_radius,
			.intensity = m_intensity,
			.downscale = m_downscale,
			.normal_buffer = pipeline.toBindless(gbuffer.B, stream),
			.depth_buffer = pipeline.toBindless(depth_buffer, stream),
			.history = pipeline.toBindless(m_temporal_rb, stream),
			.motion_vectors = pipeline.toBindless(gbuffer.D, stream),
			.output = pipeline.toRWBindless(ssao_rb, stream)
		};
		pipeline.setUniform(udata);
		pipeline.dispatch(*m_shader, (width + 15) / 16, (height + 15) / 16, 1, m_temporal ? "TEMPORAL" : nullptr);
		if (m_temporal) swap(ssao_rb, m_temporal_rb);

		if (m_blur_iterations > 0) {
			pipeline.beginBlock("ssao_blur");
			RenderBufferHandle ssao_blurred_rb = pipeline.createRenderbuffer({ 
				.type = RenderbufferDesc::FIXED,
				.fixed_size = IVec2(width, height),
				.format = gpu::TextureFormat::R8, 
				.flags = gpu::TextureFlags::COMPUTE_WRITE, 
				.debug_name = "ssao_blurred" 
			});

			RenderBufferHandle ssao_blurred2_rb;
			if (m_blur_iterations > 1) {
				ssao_blurred2_rb = pipeline.createRenderbuffer({ 
					.type = RenderbufferDesc::FIXED,
					.fixed_size = IVec2(width, height),
					.format = gpu::TextureFormat::R8, 
					.flags = gpu::TextureFlags::COMPUTE_WRITE, 
					.debug_name = "ssao_blurred2" 
				});
			}

			struct {
				Vec2 rcp_size;
				float weight_scale;
				u32 stride;
				u32 downscale;
				gpu::BindlessHandle input;
				gpu::BindlessHandle depth_buffer;
				gpu::RWBindlessHandle output;
			} blur_data = {
				.rcp_size = Vec2(1.0f / width, 1.0f / height),
				.weight_scale = 0.01f,
				.downscale = m_downscale,
				.depth_buffer = pipeline.toBindless(depth_buffer, stream),
			};

			stream.memoryBarrier(pipeline.toTexture(ssao_rb));
			for (u32 i = 0; i < m_blur_iterations; ++i) {
				blur_data.input = pipeline.toBindless(ssao_rb, stream);
				blur_data.output = pipeline.toRWBindless(ssao_blurred_rb, stream);
				blur_data.stride = m_blur_iterations - i;
				pipeline.setUniform(blur_data);
				pipeline.dispatch(*m_blur_shader, (width + 15) / 16, (height + 15) / 16, 1);
				ssao_rb = ssao_blurred_rb;
				stream.memoryBarrier(pipeline.toTexture(ssao_rb));
				swap(ssao_blurred_rb, ssao_blurred2_rb);
			}
			pipeline.endBlock();
		}

		struct {
			u32 downscale;
			float depth_diff_weight;
			gpu::BindlessHandle ssao_buf;
			gpu::BindlessHandle depth_buffer;
			gpu::BindlessHandle depth_buffer_small;
			gpu::RWBindlessHandle gbufferB;
		} udata2 = {
			.downscale = m_downscale,
			.depth_diff_weight = m_depth_diff_weight,
			.ssao_buf = pipeline.toBindless(ssao_rb, stream),
			.depth_buffer = pipeline.toBindless(gbuffer.DS, stream),
			.depth_buffer_small = pipeline.toBindless(depth_buffer, stream),
			.gbufferB = pipeline.toRWBindless(gbuffer.B, stream)
		};

		pipeline.beginBlock("ssao_blit");
		pipeline.setUniform(udata2);
		stream.barrierWrite(pipeline.toTexture(gbuffer.B));
		pipeline.dispatch(*m_blit_shader, (vp.w + 15) / 16, (vp.h + 15) / 16, 1);
		pipeline.endBlock();
		pipeline.endBlock();
	}
};

struct TDAO : public RenderPlugin {
	Renderer& m_renderer;
	Shader* m_shader = nullptr;
	float m_xz_range = 100;
	float m_y_range = 200;
	float m_intensity = 0.9f;
	bool m_enabled = true;
	DVec3 m_last_camera_pos = DVec3(DBL_MAX);
	
	struct PipelineInstanceData {
		RenderBufferHandle rb = INVALID_RENDERBUFFER;
	};

	TDAO(Renderer& renderer) : m_renderer(renderer) {}

	void shutdown() {
		m_shader->decRefCount();
	}

	void init() {
		ResourceManagerHub& rm = m_renderer.getEngine().getResourceManager();
		m_shader = rm.load<Shader>(Path("shaders/tdao.hlsl"));
	}

	void debugUI(Pipeline& pipeline) override {
		if (!ImGui::BeginMenu("TDAO")) return;
		ImGui::Checkbox("Enable", &m_enabled);
		
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

		if (!m_enabled) {
			m_last_camera_pos = DVec3(DBL_MAX);
			inst_data->rb = INVALID_RENDERBUFFER;
			return;
		}

		pipeline.beginBlock("tdao");
		if (inst_data->rb == INVALID_RENDERBUFFER) {
			inst_data->rb = pipeline.createRenderbuffer({
				.type = RenderbufferDesc::FIXED,
				.fixed_size = IVec2(512, 512),
				.format = gpu::TextureFormat::D32,
				.debug_name = "tdao"
			});
		}
		pipeline.keepRenderbufferAlive(inst_data->rb);
		DrawStream& stream = pipeline.getRenderer().getDrawStream();

		const Viewport& vp = pipeline.getViewport();
		const bool camera_moved = fabs(vp.pos.x - m_last_camera_pos.x) > 3 || fabs(vp.pos.y - m_last_camera_pos.y) > 3 || fabs(vp.pos.z - m_last_camera_pos.z) > 3;
		if (camera_moved) {
			m_last_camera_pos = vp.pos;
			pipeline.setRenderTargets({}, inst_data->rb);
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
			Vec4(Vec3(vp.pos - m_last_camera_pos), 0),
			Vec2(1.f / vp.w, 1.f / vp.h),
			m_intensity,
			1.f / m_xz_range,
			m_y_range * 0.5f,
			0.01f,
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

struct TAA : public RenderPlugin {
	Renderer& m_renderer;
	DVec3 m_last_camera_pos = DVec3(DBL_MAX, DBL_MAX, DBL_MAX);
	Shader* m_shader = nullptr;
	bool m_enabled = true;

	struct PipelineInstanceData {
		RenderBufferHandle history_rb = INVALID_RENDERBUFFER;
	};

	TAA(Renderer& renderer)
		: m_renderer(renderer)
	{}

	void shutdown() {
		m_shader->decRefCount();
	}

	void init() {
		ResourceManagerHub& rm = m_renderer.getEngine().getResourceManager();
		m_shader = rm.load<Shader>(Path("shaders/taa.hlsl"));
	}

	void debugUI(Pipeline& pipeline) override {
		ImGui::Checkbox("TAA", &m_enabled);
	}

	RenderBufferHandle renderAA(const GBuffer& gbuffer, RenderBufferHandle hdr_buffer, Pipeline& pipeline) override {
		PipelineInstanceData* data = pipeline.getData<PipelineInstanceData>();
		if (!m_enabled) {
			data->history_rb = INVALID_RENDERBUFFER;
			pipeline.enablePixelJitter(false);
			return hdr_buffer;
		}

		pipeline.enablePixelJitter(true);
		pipeline.beginBlock("taa");
		if (data->history_rb == INVALID_RENDERBUFFER) {
			data->history_rb = pipeline.createRenderbuffer({
				.format = gpu::TextureFormat::RGBA16F,
				.flags = gpu::TextureFlags::RENDER_TARGET | gpu::TextureFlags::NO_MIPS | gpu::TextureFlags::COMPUTE_WRITE,
				.debug_name = "taa"
			});
			pipeline.setRenderTargets(Span(&data->history_rb, 1));
			pipeline.clear(gpu::ClearFlags::ALL, 1, 1, 1, 1, 0);
		}

		const RenderBufferHandle taa_tmp = pipeline.createRenderbuffer({
			.format = gpu::TextureFormat::RGBA16F,
			.flags = gpu::TextureFlags::RENDER_TARGET | gpu::TextureFlags::NO_MIPS | gpu::TextureFlags::COMPUTE_WRITE,
			.debug_name = "taa2"
		});
		
		const IVec2& display_size = pipeline.getDisplaySize();
		DrawStream& stream = pipeline.getRenderer().getDrawStream();

		struct {
			Vec2 u_size;
			gpu::BindlessHandle u_history;
			gpu::BindlessHandle u_current;
			gpu::BindlessHandle u_motion_vectors;
			gpu::RWBindlessHandle u_output;
		} ub_data = {
			Vec2(display_size),
			pipeline.toBindless(data->history_rb, stream),
			pipeline.toBindless(hdr_buffer, stream),
			pipeline.toBindless(gbuffer.D, stream),
			pipeline.toRWBindless(taa_tmp, stream)
		};

		pipeline.setUniform(ub_data);
		pipeline.dispatch(*m_shader, (display_size.x + 15) / 16, (display_size.y + 15) / 16, 1);

		const RenderBufferHandle taa_output = pipeline.createRenderbuffer({
			.format = gpu::TextureFormat::RGBA16F,
			.flags = gpu::TextureFlags::RENDER_TARGET | gpu::TextureFlags::NO_MIPS | gpu::TextureFlags::COMPUTE_WRITE,
			.debug_name = "taa_output"	
		});
		stream.memoryBarrier(pipeline.toTexture(taa_tmp));
		// TODO blit does unnecessary computations
		gpu::BindlessHandle src = pipeline.toBindless(taa_tmp, stream);
		gpu::RWBindlessHandle dst = pipeline.toRWBindless(taa_output, stream);
		pipeline.blit(src, dst, display_size);
		
		data->history_rb = taa_tmp;
		pipeline.keepRenderbufferAlive(data->history_rb);
		pipeline.endBlock();
		return taa_output;
	}
};


} // namespace