#pragma once

#include "postprocess.h"
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
		m_shader = rm.load<Shader>(Path("pipelines/atmo.shd"));
		m_scattering_shader = rm.load<Shader>(Path("pipelines/atmo_scattering.shd"));
		m_optical_depth_shader = rm.load<Shader>(Path("pipelines/atmo_optical_depth.shd"));
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
		
		DrawStream& stream = pipeline.getRenderer().getEndFrameDrawStream();
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
			Vec4(env.fog_scattering, 0),
			env.fog_top,
			env.fog_enabled ? 1.f : 0.f,
			env.godrays_enabled ? 1.f : 0.f,
			gpu::getRWBindlessHandle(m_optical_depth_precomputed),
			gpu::getBindlessHandle(m_optical_depth_precomputed),
			pipeline.toBindless(gbuffer.DS, stream)
		};

		stream.barrierWrite(m_optical_depth_precomputed);
		pipeline.beginBlock("precompute_transmittance");
		pipeline.setUniform(ub_data);
		pipeline.dispatch(*m_optical_depth_shader, 128 / 16, 128 / 16, 1);
		pipeline.endBlock();
		
		stream.barrierWrite(m_inscatter_precomputed);
		stream.barrierRead(m_optical_depth_precomputed);
		stream.memoryBarrier(m_optical_depth_precomputed);

		pipeline.beginBlock("precompute_inscatter");
		ub_data.resolution = Vec4(64, 128, 1, 0);
		ub_data.output = gpu::getRWBindlessHandle(m_inscatter_precomputed);
		pipeline.setUniform(ub_data);
		pipeline.dispatch(*m_scattering_shader, 64 / 16, 128 / 16, 1);
		pipeline.endBlock();
		
		stream.barrierRead(m_inscatter_precomputed);
		stream.memoryBarrier(m_inscatter_precomputed);
		
		ub_data.inscatter_precomputed = gpu::getBindlessHandle(m_inscatter_precomputed);
		pipeline.setRenderTargets(Span(&hdr_rb, 1));
		pipeline.setUniform(ub_data);
		const gpu::StateFlags state = gpu::getBlendStateBits(gpu::BlendFactors::ONE, gpu::BlendFactors::SRC1_COLOR, gpu::BlendFactors::ONE, gpu::BlendFactors::ONE);
		pipeline.drawArray(0, 3, *m_shader, 0, state);

		pipeline.endBlock();
		return hdr_rb;
	}
};

struct FilmGrain : public RenderPlugin {
	Renderer& m_renderer;
	Shader* m_shader = nullptr;
	Texture* m_noise = nullptr;

	FilmGrain(Renderer& renderer) : m_renderer(renderer) {}

	void shutdown() {
		m_shader->decRefCount();
		m_noise->decRefCount();
	}

	void init() {
		ResourceManagerHub& rm = m_renderer.getEngine().getResourceManager();
		m_shader = rm.load<Shader>(Path("pipelines/film_grain.shd"));
		m_noise = rm.load<Texture>(Path("textures/common/blue_noise.tga"));
	}

	RenderBufferHandle renderAfterTonemap(const GBuffer& gbuffer, RenderBufferHandle input, Pipeline& pipeline) override {
		if (!m_shader->isReady()) return input;
		if (!m_noise->isReady()) return input;
		if (pipeline.getType() != PipelineType::GAME_VIEW) return input;

		RenderModule* module = pipeline.getModule();
		EntityPtr camera_entity = module->getActiveCamera();
		if (!camera_entity.isValid()) return input;

		Camera& camera = module->getCamera(*camera_entity);
		if (camera.film_grain_intensity <= 1e-5) return input;

		const RenderBufferHandle ldr_buffer = INVALID_RENDERBUFFER;
		pipeline.beginBlock("film_grain");

		const RenderBufferHandle res = pipeline.createRenderbuffer({
			.format = gpu::TextureFormat::RGBA8, 
			.flags = gpu::TextureFlags::COMPUTE_WRITE | gpu::TextureFlags::NO_MIPS | gpu::TextureFlags::RENDER_TARGET,
			.debug_name = "film_grain"
		});
		DrawStream& stream = pipeline.getRenderer().getDrawStream();
		struct {
			float intensity;
			float lumamount;
			gpu::BindlessHandle source;
			gpu::BindlessHandle noise;
			gpu::RWBindlessHandle output;
		} ubdata = {
			camera.film_grain_intensity,
			0.1f,
			pipeline.toBindless(input, stream),
			gpu::getBindlessHandle(m_noise->handle),
			pipeline.toRWBindless(res, stream)
		};
		const Viewport& vp = pipeline.getViewport();
		pipeline.setUniform(ubdata);
		pipeline.dispatch(*m_shader, (vp.w + 15) / 16, (vp.h + 15) / 16, 1);

		pipeline.endBlock();
		return res;
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
		m_shader = rm.load<Shader>(Path("pipelines/dof.shd"));
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
		}	ub = {
			camera.dof_distance,
			camera.dof_range,
			camera.dof_max_blur_size,
			camera.dof_sharp_range,
			pipeline.toBindless(input, stream),
			pipeline.toBindless(gbuffer.DS, stream)
		};

		pipeline.setUniform(ub);
		pipeline.setRenderTargets(Span(&dof_rb, 1));
		pipeline.drawArray(0, 3, *m_shader, 0, gpu::StateFlags::NONE);

		pipeline.setRenderTargets(Span(&input, 1));
		pipeline.renderTexturedQuad(pipeline.toBindless(dof_rb, stream), false, false);

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
		m_shader = rm.load<Shader>(Path("pipelines/cubemap_sky.shd"));
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
	Shader* m_tonemap_shader = nullptr;
	Shader* m_blur_shader = nullptr;
	Shader* m_avg_luminance_shader = nullptr;
	Shader* m_bloom_blur_shader = nullptr;
	gpu::BufferHandle m_lum_buf = gpu::INVALID_BUFFER;
	RenderBufferHandle m_extracted_rt = INVALID_RENDERBUFFER; // for debug view
	bool m_show_debug = false;

	Bloom(Renderer& renderer)
		: m_renderer(renderer)
	{}

	void shutdown() {
		m_shader->decRefCount();
		m_tonemap_shader->decRefCount();
		m_blur_shader->decRefCount();
		m_avg_luminance_shader->decRefCount();
		m_bloom_blur_shader->decRefCount();
		m_renderer.getEndFrameDrawStream().destroy(m_lum_buf);
	}

	void init() {
		ResourceManagerHub& rm = m_renderer.getEngine().getResourceManager();
		m_shader = rm.load<Shader>(Path("pipelines/bloom.shd"));
		m_tonemap_shader = rm.load<Shader>(Path("pipelines/bloom_tonemap.shd"));
		m_blur_shader = rm.load<Shader>(Path("pipelines/blur.shd"));
		m_avg_luminance_shader = rm.load<Shader>(Path("pipelines/avg_luminance.shd"));
		m_bloom_blur_shader = rm.load<Shader>(Path("pipelines/bloom_blur.shd"));
		m_lum_buf = m_renderer.createBuffer({2048}, gpu::BufferFlags::SHADER_BUFFER, "bloom");
	}

	void debugUI(Pipeline& pipeline) override {
		ImGui::Checkbox("Bloom", &m_show_debug);
	}

	bool debugOutput(RenderBufferHandle input, Pipeline& pipeline) override {
		if (!m_show_debug) return false;
		pipeline.copy(input, m_extracted_rt);
		pipeline.keepRenderbufferAlive(m_extracted_rt);
		return true;
	}

	void computeAvgLuminance(RenderBufferHandle input, float accomodation_speed, Pipeline& pipeline) {
		pipeline.beginBlock("autoexposure");

		const Viewport& vp = pipeline.getViewport();
		DrawStream& stream = pipeline.getRenderer().getDrawStream();

		struct {
			Vec2 size;
			float accomodation_speed;
			gpu::BindlessHandle image;
			gpu::RWBindlessHandle histogram;
		} ubdata = {
			Vec2((float)vp.w, (float)vp.h),
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
		pipeline.setRenderTargets(Span(&small, 1));
		const u32 define_mask = 1 << pipeline.getRenderer().getShaderDefineIdx("DOWNSCALE");
		DrawStream& stream = pipeline.getRenderer().getDrawStream();
		pipeline.setUniform(pipeline.toBindless(big, stream));
		pipeline.drawArray(0, 3, *m_shader, define_mask, gpu::StateFlags::NONE);
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
			.debug_name = "bloom" 
		});
	
		const Viewport& vp = pipeline.getViewport();
		pipeline.setRenderTargets(Span(&bloom_rb, 1));
		pipeline.viewport(0, 0, vp.w >> 1, vp.h >> 1);
		const u32 define_mask = 1 << pipeline.getRenderer().getShaderDefineIdx("EXTRACT");
		DrawStream& stream = pipeline.getRenderer().getDrawStream();
		struct {
			float avg_lum_multiplier;
			gpu::BindlessHandle histogram;
			u32 input;
		} ubdata = {
			camera->bloom_avg_bloom_multiplier,
			gpu::getBindlessHandle(m_lum_buf), 
			pipeline.toBindless(input, stream)
		};
		stream.barrierRead(m_lum_buf);
		pipeline.setUniform(ubdata);
		pipeline.drawArray(0, 3, *m_shader, define_mask, gpu::StateFlags::NONE);
		m_extracted_rt = bloom_rb;

		if (m_show_debug) {
			pipeline.endBlock();
			return input;
		}

		RenderBufferHandle bloom2_rb = downscale(bloom_rb, { 
			.type = RenderbufferDesc::FIXED,
			.fixed_size = {vp.w >> 2, vp.h >> 2},
			.format = gpu::TextureFormat::RGBA16F,
			.debug_name = "bloom2"
		}, pipeline);
		RenderBufferHandle bloom4_rb = downscale(bloom2_rb, {
			.type = RenderbufferDesc::FIXED, 
			.fixed_size = {vp.w >> 3, vp.h >> 3}, 
			.format = gpu::TextureFormat::RGBA16F,
			.debug_name = "bloom4"	
		}, pipeline);
		RenderBufferHandle bloom8_rb = downscale(bloom4_rb, {
			.type = RenderbufferDesc::FIXED, 
			.fixed_size = {vp.w >> 4, vp.h >> 4}, 
			.format = gpu::TextureFormat::RGBA16F,
			.debug_name = "bloom8"
		}, pipeline);
		RenderBufferHandle bloom16_rb = downscale(bloom8_rb, {
			.type = RenderbufferDesc::FIXED, 
			.fixed_size = {vp.w >> 5, vp.h >> 5}, 
			.format = gpu::TextureFormat::RGBA16F,
			.debug_name = "bloom16"
		}, pipeline);

		blur({ vp.w >> 5, vp.h >> 5 }, bloom16_rb, pipeline);
		blurUpscale({ vp.w >> 4, vp.h >> 4 }, bloom8_rb, bloom16_rb, pipeline);
		blurUpscale({ vp.w >> 3, vp.h >> 3 }, bloom4_rb, bloom8_rb, pipeline);
		blurUpscale({ vp.w >> 2, vp.h >> 2 }, bloom2_rb, bloom4_rb, pipeline);
		blurUpscale({ vp.w >> 1, vp.h >> 1 }, bloom_rb, bloom2_rb, pipeline);

		pipeline.setRenderTargets(Span(&input, 1));
		pipeline.setUniform(pipeline.toBindless(bloom_rb, stream));
		pipeline.drawArray(0, 3, *m_shader, 0, gpu::getBlendStateBits(gpu::BlendFactors::ONE, gpu::BlendFactors::ONE, gpu::BlendFactors::ONE, gpu::BlendFactors::ONE));

		pipeline.endBlock();
		return input;
	}
	
	bool tonemap(RenderBufferHandle input, RenderBufferHandle& output, Pipeline& pipeline) override {
		const Camera* camera = getCamera(pipeline);
		if (pipeline.getType() != PipelineType::GAME_VIEW) return false;
		if (!camera || !camera->bloom_tonemap_enabled) return false;
		if (!m_tonemap_shader->isReady()) return false;

		pipeline.beginBlock("bloom tonemap");
		RenderBufferHandle rb = pipeline.createRenderbuffer({
			.format = gpu::TextureFormat::RGBA8,
			.debug_name = "tonemap_bloom"
		});
		
		DrawStream& stream = pipeline.getRenderer().getDrawStream();
		struct {
			float exposure;
			gpu::BindlessHandle input;
			gpu::BindlessHandle accum;
		} ubdata = {
			camera->bloom_exposure,
			pipeline.toBindless(input, stream),
			gpu::getBindlessHandle(m_lum_buf)
		};

		stream.barrierRead(m_lum_buf);
		pipeline.setRenderTargets(Span(&rb, 1));
		pipeline.setUniform(ubdata); 
		pipeline.drawArray(0, 3, *m_tonemap_shader, 0, gpu::StateFlags::NONE);
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
	bool m_show_debug = false;

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
		m_shader = rm.load<Shader>(Path("pipelines/sss.shd"));
		m_shader_blit = rm.load<Shader>(Path("pipelines/sss_blit.shd"));
	}

	void debugUI(Pipeline& pipeline) override {
		if (!ImGui::BeginMenu("SSS")) return;

		ImGui::Checkbox("Enable", &m_is_enabled);
		ImGui::Checkbox("Debug", &m_show_debug);
		ImGui::EndMenu();
	}

	bool debugOutput(RenderBufferHandle input, Pipeline& pipeline) override {
		if (!m_show_debug) return false;
		RenderBufferHandle rb = pipeline.getData<PipelineInstanceData>()->history;
		if (rb != INVALID_RENDERBUFFER) pipeline.copy(input, rb);
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
			Vec2 size;
			float max_steps;
			float stride;
			gpu::BindlessHandle depth;
			gpu::RWBindlessHandle sss_buffer;
		} ubdata = {
			Vec2((float)vp.w, (float)vp.h),
			(float)m_max_steps,
			m_stride,
			pipeline.toBindless(gbuffer.DS, stream),
			pipeline.toRWBindless(sss, stream)
		};
		pipeline.setUniform(ubdata);
		pipeline.dispatch(*m_shader, (vp.w + 15) / 16, (vp.h + 15) / 16, 1);
		stream.memoryBarrier(pipeline.toTexture(sss));

		struct {
			Vec2 size;
			float current_frame_weight;
			gpu::RWBindlessHandle sss;
			gpu::BindlessHandle history;
			gpu::BindlessHandle depthbuf;
			gpu::RWBindlessHandle gbufferC;
		} ubdata2 = {
			Vec2((float)vp.w, (float)vp.h),
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
	bool m_enabled = true;

	SSAO(Renderer& renderer) : m_renderer(renderer) {}
	
	void shutdown() {
		m_shader->decRefCount();
		m_blit_shader->decRefCount();
	}

	void init() {
		ResourceManagerHub& rm = m_renderer.getEngine().getResourceManager();
		m_shader = rm.load<Shader>(Path("pipelines/ssao.shd"));
		m_blit_shader = rm.load<Shader>(Path("pipelines/ssao_blit.shd"));
	}

	void debugUI(Pipeline& pipeline) override {
		ImGui::Checkbox("SSAO", &m_enabled);
	}

	void renderBeforeLightPass(const GBuffer& gbuffer, Pipeline& pipeline) override {
		PROFILE_FUNCTION();
		if (!m_shader->isReady()) return;
		if (!m_blit_shader->isReady()) return;
		if (!m_enabled) return;

		pipeline.beginBlock("ssao");
		const RenderBufferHandle ssao_rb = pipeline.createRenderbuffer({ 
			.format = gpu::TextureFormat::RGBA8, 
			.flags = gpu::TextureFlags::COMPUTE_WRITE, 
			.debug_name = "ssao" 
		});
		
		const Viewport& vp = pipeline.getViewport();
		DrawStream& stream = pipeline.getRenderer().getDrawStream();

		struct {
			float radius = 0.2f;
			float intensity = 3;
			float width;
			float height;
			gpu::BindlessHandle normal_buffer;
			gpu::BindlessHandle depth_buffer;
			gpu::RWBindlessHandle output;
		} udata = {
			.width = (float)vp.w,
			.height = (float)vp.h,
			.normal_buffer = pipeline.toBindless(gbuffer.B, stream),
			.depth_buffer = pipeline.toBindless(gbuffer.DS, stream),
			.output = pipeline.toRWBindless(ssao_rb, stream)
		};
		pipeline.setUniform(udata);
		pipeline.dispatch(*m_shader, (vp.w + 15) / 16, (vp.h + 15) / 16, 1);

		struct {
			Vec2 size;
			gpu::BindlessHandle ssao_buf;
			gpu::RWBindlessHandle gbufferB;
		} udata2 = {
			.size = Vec2((float)vp.w, (float)vp.h),
			.ssao_buf = pipeline.toBindless(ssao_rb, stream),
			.gbufferB = pipeline.toRWBindless(gbuffer.B, stream)
		};

		stream.memoryBarrier(pipeline.toTexture(ssao_rb));
		pipeline.setUniform(udata2);
		pipeline.getRenderer().getDrawStream().barrierWrite(pipeline.toTexture(gbuffer.B));
		pipeline.dispatch(*m_blit_shader, (vp.w + 15) / 16, (vp.h + 15) / 16, 1);
		pipeline.endBlock();
	}
};

struct TDAO : public RenderPlugin {
	Renderer& m_renderer;
	Shader* m_shader = nullptr;
	float m_xz_range = 100;
	float m_y_range = 200;
	float m_intensity = 0.3f;
	bool m_enabled = true;
	bool m_show_debug = false;
	
	struct PipelineInstanceData {
		RenderBufferHandle rb = INVALID_RENDERBUFFER;
	};

	TDAO(Renderer& renderer) : m_renderer(renderer) {}

	void shutdown() {
		m_shader->decRefCount();
	}

	void init() {
		ResourceManagerHub& rm = m_renderer.getEngine().getResourceManager();
		m_shader = rm.load<Shader>(Path("pipelines/tdao.shd"));
	}

	void debugUI(Pipeline& pipeline) override {
		if (!ImGui::BeginMenu("TDAO")) return;
		ImGui::Checkbox("Enable", &m_enabled);
		ImGui::Checkbox("Debug", &m_show_debug);
		ImGui::EndMenu();
	}

	bool debugOutput(RenderBufferHandle input, Pipeline& pipeline) override {
		if (!m_show_debug) return false;
		
		auto* data = pipeline.getData<PipelineInstanceData>();
		if (data->rb != INVALID_RENDERBUFFER) {
			pipeline.copy(input, data->rb, {1, 0, 0, 0}, {1, 0, 0, 0}, {1, 0, 0, 0});
		}
		return true;
	}


	void renderBeforeLightPass(const GBuffer& gbuffer, Pipeline& pipeline) override {
		PROFILE_FUNCTION();
		auto* inst_data = pipeline.getData<PipelineInstanceData>();

		if (!m_enabled) {
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

		// TODO
		bool camera_moved = true;
		if (camera_moved) {
			pipeline.setRenderTargets({}, inst_data->rb);
			pipeline.clear(gpu::ClearFlags::ALL, 0, 0, 0, 1, 0);

			CameraParams cp;
			const Quat rot(-0.707106769f, 0, 0, 0.707106769f);
			cp.pos = pipeline.getViewport().pos;
			ShiftedFrustum frustum;
			const float ratio = 1;
			frustum.computeOrtho({ 0, 0, 0 },
				rot * Vec3(0, 0, 1),
				rot * Vec3(0, 1, 0),
				m_xz_range,
				m_xz_range,
				-0.5f * m_y_range,
				0.5f * m_y_range);
			frustum.origin = pipeline.getViewport().pos;
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
			float u_intensity;
			float u_width;
			float u_height;
			float u_offset0;
			float u_offset1;
			float u_offset2;
			float u_range;
			float u_half_depth_range;
			float u_scale;
			float u_depth_offset;
			gpu::BindlessHandle u_depth_buffer;
			gpu::RWBindlessHandle u_gbufferB;
			gpu::BindlessHandle u_topdown_depthmap;
		} ubdata = {
			m_intensity,
			(float)pipeline.getViewport().w,
			(float)pipeline.getViewport().h,
			0,
			0,
			0,
			m_xz_range,
			m_y_range * 0.5f,
			0.01f,
			0.02f,
			pipeline.toBindless(gbuffer.DS, stream),
			pipeline.toRWBindless(gbuffer.B, stream),
			pipeline.toBindless(inst_data->rb, stream),
		};

		pipeline.setUniform(ubdata);
		pipeline.dispatch(*m_shader, (pipeline.getViewport().w + 15) / 16, (pipeline.getViewport().h + 15) / 16, 1);
		
		pipeline.endBlock();
	}
};

struct TAA : public RenderPlugin {
	Renderer& m_renderer;
	DVec3 m_last_camera_pos = DVec3(DBL_MAX, DBL_MAX, DBL_MAX);
	Shader* m_shader = nullptr;
	Shader* m_textured_quad_shader = nullptr;
	bool m_enabled = true;

	struct PipelineInstanceData {
		RenderBufferHandle history_rb = INVALID_RENDERBUFFER;
	};

	TAA(Renderer& renderer)
		: m_renderer(renderer)
	{}

	void shutdown() {
		m_shader->decRefCount();
		m_textured_quad_shader->decRefCount();
	}

	void init() {
		ResourceManagerHub& rm = m_renderer.getEngine().getResourceManager();
		m_shader = rm.load<Shader>(Path("pipelines/taa.shd"));
		m_textured_quad_shader = rm.load<Shader>(Path("pipelines/textured_quad.shd"));
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
		pipeline.dispatch(*m_shader, (display_size.x + 15) / 16, (display_size.y + 15) / 16, 11);

		struct {
			Vec4 offset_scale = Vec4(0, 0, 1, 1);
			Vec4 r_mask = Vec4(1, 0, 0, 0);
			Vec4 g_mask = Vec4(0, 1, 0, 0);
			Vec4 b_mask = Vec4(0, 0, 1, 0);
			Vec4 a_mask = Vec4(0, 0, 0, 1);;
			Vec4 offsets = Vec4(0, 0, 0, 1);
			gpu::BindlessHandle texture;
		} udata;
		udata.texture = pipeline.toBindless(taa_tmp, stream);

		const RenderBufferHandle taa_output = pipeline.createRenderbuffer({
			.format = gpu::TextureFormat::RGBA16F,
			.debug_name = "taa_output"	
		});
		stream.memoryBarrier(pipeline.toTexture(taa_tmp));
		pipeline.setRenderTargets(Span(&taa_output, 1));
		pipeline.setUniform(udata);
		// TODO textured_quad_shader does unnecessary computations
		pipeline.drawArray(0, 3, *m_textured_quad_shader);
		
		data->history_rb = taa_tmp;
		pipeline.keepRenderbufferAlive(data->history_rb);
		pipeline.endBlock();
		return taa_output;
	}
};


} // namespace