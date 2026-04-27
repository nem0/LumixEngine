#include "core/log.h"
#include "core/os.h"
#include "core/tag_allocator.h"
#include "engine/engine.h"
#include <imgui/imgui.h>
#undef near
#undef far
#undef NEAR
#undef RELATIVE
#undef FAR
#include "pipeline.h"
#include "renderer.h"

namespace Lumix {

/*
Header-free DLSS integration for DX12.

Runtime requirements:
- nvsdk_ngx_d.dll (or nvsdk_ngx.dll)
- nvngx_dlss.dll

If any symbol/library is missing or DLSS is unsupported on the machine,
initDLSS returns false and renderer falls back to FSR/TAA.
*/

namespace gpu {
	void* getDX12Device();
	void* getDX12CommandList();
	void* getDX12Resource(TextureHandle h);
	void resetCommandList();
}

namespace {

struct ID3D11Resource;

static constexpr u32 NGX_VERSION_API = 0x0000015;		// NGX parameter ABI version expected by the runtime.
static constexpr i32 NGX_ENGINE_TYPE_CUSTOM = 0; 		// Engine identifier type for custom/non-predefined engines.
static constexpr i32 NGX_FEATURE_SUPERSAMPLING = 1;		// NGX feature id for DLSS Super Resolution / DLAA path.

static constexpr i32 NGX_PERF_BALANCED = 1;				// DLSS Balanced: ~58% render scale.
static constexpr i32 NGX_PERF_MAX_PERF = 0;				// DLSS Performance: ~50% render scale
static constexpr i32 NGX_PERF_MAX_QUALITY = 2;			// DLSS Quality: ~67% render scale
static constexpr i32 NGX_PERF_ULTRA_PERFORMANCE = 3;	// DLSS Ultra Performance: ~33% render scale
static constexpr i32 NGX_PERF_ULTRA_QUALITY = 4;		// DLSS Ultra Quality: ~77% render scale
static constexpr i32 NGX_PERF_DLAA = 5;					// DLAA: 100% render scale (no upscaling).

static constexpr i32 NGX_DLSS_FLAG_IS_HDR = 1 << 0;				// Input color buffer is HDR.
static constexpr i32 NGX_DLSS_FLAG_MV_LOW_RES = 1 << 1; 		// Motion vectors are at render resolution (not output resolution).
static constexpr i32 NGX_DLSS_FLAG_MV_JITTERED = 1 << 2;		// Motion vectors already include camera jitter.
static constexpr i32 NGX_DLSS_FLAG_DEPTH_INVERTED = 1 << 3;		// Depth buffer uses reversed-Z convention.
static constexpr i32 NGX_DLSS_FLAG_AUTO_EXPOSURE = 1 << 6;		// Let DLSS estimate exposure automatically.
static constexpr i32 NGX_DLSS_FLAG_ALPHA_UPSCALING = 1 << 7;	// Enable alpha channel upscaling path.

inline bool ngxFailed(i32 value) {
	return (value & 0xFFF00000) == 0xBAD00000;
}

enum class NGXHandle : u32 {};
	
struct NGXParameter {
	virtual void Set(const char* name, unsigned long long value) = 0;
	virtual void Set(const char* name, float value) = 0;
	virtual void Set(const char* name, double value) = 0;
	virtual void Set(const char* name, unsigned int value) = 0;
	virtual void Set(const char* name, int value) = 0;
	virtual void Set(const char* name, ID3D11Resource* value) = 0;
	virtual void Set(const char* name, ID3D12Resource* value) = 0;
	virtual void Set(const char* name, void* value) = 0;

	virtual i32 Get(const char* name, unsigned long long* value) const = 0;
	virtual i32 Get(const char* name, float* value) const = 0;
	virtual i32 Get(const char* name, double* value) const = 0;
	virtual i32 Get(const char* name, unsigned int* value) const = 0;
	virtual i32 Get(const char* name, int* value) const = 0;
	virtual i32 Get(const char* name, ID3D11Resource** value) const = 0;
	virtual i32 Get(const char* name, ID3D12Resource** value) const = 0;
	virtual i32 Get(const char* name, void** value) const = 0;

	virtual void Reset() = 0;
};

using PfnNGXInitD3D12WithProjectID = i32(__cdecl*)(const char*, i32, const char*, const wchar_t*, ID3D12Device*, u32, const void*);
using PfnNGXInitD3D12 = i32(__cdecl*)(unsigned long long, const wchar_t*, ID3D12Device*, u32);
using PfnNGXShutdownD3D12 = i32(__cdecl*)(ID3D12Device*);
using PfnNGXGetCaps = i32(__cdecl*)(NGXParameter**);
using PfnNGXCreateFeatureD3D12 = i32(__cdecl*)(ID3D12GraphicsCommandList*, i32, NGXParameter*, NGXHandle**);
using PfnNGXReleaseFeatureD3D12 = i32(__cdecl*)(NGXHandle*);
using PfnNGXEvaluateFeatureD3D12 = i32(__cdecl*)(ID3D12GraphicsCommandList*, const NGXHandle*, const NGXParameter*, void*);

struct NGXApi {
	void* core_lib = nullptr;
	void* dlss_lib = nullptr;

	PfnNGXInitD3D12WithProjectID init_with_project_id = nullptr;
	PfnNGXInitD3D12 init = nullptr;
	PfnNGXShutdownD3D12 shutdown = nullptr;
	PfnNGXGetCaps get_caps = nullptr;
	PfnNGXCreateFeatureD3D12 create_feature = nullptr;
	PfnNGXReleaseFeatureD3D12 release_feature = nullptr;
	PfnNGXEvaluateFeatureD3D12 eval_feature = nullptr;

	NGXParameter* caps = nullptr;
	bool api_loaded = false;
	bool initialized = false;
	bool broken = false;
};

static NGXApi g_ngx;

template <typename T>
bool loadProc(T& fn, void* lib, const char* name) {
	fn = (T)os::getLibrarySymbol(lib, name);
	if (!fn) logError("DLSS: failed to load function ", name);
	return fn != nullptr;
}

template <typename T>
bool loadProcOptional(T& fn, void* lib, const char* name) {
	fn = (T)os::getLibrarySymbol(lib, name);
	return fn != nullptr;
}

bool loadNGXApi() {
	if (g_ngx.api_loaded) return true;

	g_ngx.core_lib = os::loadLibrary("nvsdk_ngx_d.dll");
	if (!g_ngx.core_lib) {
		logInfo("DLSS: nvsdk_ngx_d.dll not found, trying nvsdk_ngx.dll");
		g_ngx.core_lib = os::loadLibrary("nvsdk_ngx.dll");
	}
	if (!g_ngx.core_lib) {
		logInfo("DLSS: nvsdk_ngx_d.dll / nvsdk_ngx.dll not loaded");
		return false;
	}
	logInfo("DLSS: loaded NGX core library");

	g_ngx.dlss_lib = os::loadLibrary("nvngx_dlss.dll");
	if (g_ngx.dlss_lib) logInfo("DLSS: loaded nvngx_dlss.dll");
	else logInfo("DLSS: nvngx_dlss.dll not loaded");

	bool ok = true;
	if (loadProcOptional(g_ngx.init_with_project_id, g_ngx.core_lib, "NVSDK_NGX_D3D12_Init_ProjectID")
		|| loadProcOptional(g_ngx.init_with_project_id, g_ngx.core_lib, "NVSDK_NGX_D3D12_Init_with_ProjectID")) {
		logInfo("DLSS: using project-id initialization");
	}
	else if (loadProcOptional(g_ngx.init, g_ngx.core_lib, "NVSDK_NGX_D3D12_Init")) {
		logInfo("DLSS: project-id init not found, using NVSDK_NGX_D3D12_Init (AppID=0)");
	}
	else {
		logError("DLSS: failed to load function NVSDK_NGX_D3D12_Init_ProjectID / NVSDK_NGX_D3D12_Init");
		ok = false;
	}
	ok = ok && loadProc(g_ngx.shutdown, g_ngx.core_lib, "NVSDK_NGX_D3D12_Shutdown1");
	ok = ok && loadProc(g_ngx.get_caps, g_ngx.core_lib, "NVSDK_NGX_D3D12_GetCapabilityParameters");
	ok = ok && loadProc(g_ngx.create_feature, g_ngx.core_lib, "NVSDK_NGX_D3D12_CreateFeature");
	ok = ok && loadProc(g_ngx.release_feature, g_ngx.core_lib, "NVSDK_NGX_D3D12_ReleaseFeature");
	if (!loadProcOptional(g_ngx.eval_feature, g_ngx.core_lib, "NVSDK_NGX_D3D12_EvaluateFeature_C")) {
		logInfo("DLSS: NVSDK_NGX_D3D12_EvaluateFeature_C not found, trying NVSDK_NGX_D3D12_EvaluateFeature");
		ok = ok && loadProc(g_ngx.eval_feature, g_ngx.core_lib, "NVSDK_NGX_D3D12_EvaluateFeature");
	}

	if (!ok) {
		if (g_ngx.dlss_lib) os::unloadLibrary(g_ngx.dlss_lib);
		os::unloadLibrary(g_ngx.core_lib);
		g_ngx = {};
		return false;
	}
	g_ngx.api_loaded = true;
	logInfo("DLSS: NGX entry points resolved");
	return true;
}

inline void setUI(NGXParameter* p, const char* name, u32 value) { p->Set(name, (unsigned int)value); }
inline void setI(NGXParameter* p, const char* name, i32 value) { p->Set(name, value); }
inline void setF(NGXParameter* p, const char* name, float value) { p->Set(name, value); }
// Converts engine texture handle to native D3D12 resource pointer and stores it in NGX params.
inline void setTex(NGXParameter* p, const char* name, gpu::TextureHandle h) { p->Set(name, (ID3D12Resource*)gpu::getDX12Resource(h)); }
inline ID3D12GraphicsCommandList* getCmdList() { return (ID3D12GraphicsCommandList*)gpu::getDX12CommandList(); }

} // anonymous namespace

struct DLSSPlugin : RenderPlugin {
	explicit DLSSPlugin(IAllocator& allocator)
		: m_allocator(allocator, "DLSS")
		, m_contexts(allocator)
	{}

	enum class Mode : i32 {
		DLAA = 0,
		QUALITY,
		BALANCED,
		PERFORMANCE,
		ULTRA_PERFORMANCE
	};

	struct Context {
		IVec2 size = {-1, -1};
		IVec2 output_size = {-1, -1};
		Pipeline* pipeline = nullptr;
		NGXHandle* feature = nullptr;
		bool reset = true;
		u32 warmup_frames = 1;
		u32 frames_since_last_use = 0;
		i32 last_create_result = 0;
		i32 last_eval_result = 0;
		i32 perf_quality = NGX_PERF_DLAA;
	};

	// Releases NGX feature handle owned by a pipeline context.
	void releaseContext(Context& ctx) {
		if (ctx.feature) {
			g_ngx.release_feature(ctx.feature);
			ctx.feature = nullptr;
		}
	}

	void frame(Renderer&) override {
		for (i32 i = m_contexts.size() - 1; i >= 0; --i) {
			UniquePtr<Context>& ctx = m_contexts[i];
			++ctx->frames_since_last_use;
			if (ctx->frames_since_last_use < 6) continue;
			releaseContext(*ctx);
			m_contexts.swapAndPop(i);
		}
	}

	void pipelineDestroyed(Pipeline& pipeline) override {
		for (i32 i = m_contexts.size() - 1; i >= 0; --i) {
			if (m_contexts[i]->pipeline != &pipeline) continue;
			releaseContext(*m_contexts[i]);
			m_contexts.swapAndPop(i);
		}
	}

	void shutdown(Renderer&) override {
		for (UniquePtr<Context>& ctx : m_contexts) {
			releaseContext(*ctx);
		}
		m_contexts.clear();
	}

	// Finds pipeline-local DLSS context or creates one.
	// Recreates feature lazily when viewport size changes.
	Context& getOrCreateContext(Pipeline& pipeline, const IVec2& output_size, i32 perf_quality) {
		const Viewport& vp = pipeline.getViewport();
		const IVec2 size = {vp.w, vp.h};
		for (UniquePtr<Context>& ctx : m_contexts) {
			if (ctx->pipeline != &pipeline) continue;
			ctx->frames_since_last_use = 0;
			if (ctx->size != size || ctx->output_size != output_size || ctx->perf_quality != perf_quality) {
				releaseContext(*ctx);
				ctx->size = size;
				ctx->output_size = output_size;
				ctx->perf_quality = perf_quality;
				ctx->reset = true;
				ctx->warmup_frames = 1;
			}
			return *ctx.get();
		}

		UniquePtr<Context> ctx = UniquePtr<Context>::create(m_allocator);
		ctx->pipeline = &pipeline;
		ctx->size = size;
		ctx->output_size = output_size;
		ctx->perf_quality = perf_quality;
		ctx->warmup_frames = 1;
		Context* out = ctx.get();
		m_contexts.push(ctx.move());
		return *out;
	}

	// Creates NGX super-sampling feature for the given context if it does not exist yet.
	// Uses current viewport as both render and output dimensions.
	bool createFeatureIfNeeded(Context& ctx, const Viewport& vp) {
		if (ctx.feature) return true;
		gpu::pushDebugGroup("DLSS CreateFeature");
		NGXParameter* p = g_ngx.caps;
		p->Reset();
		// Current implementation runs DLSS in 1:1 render/output mode.
		// This keeps integration simple and behaves as DLAA-like AA path.
		setUI(p, "CreationNodeMask", 1);
		setUI(p, "VisibilityNodeMask", 1);
		setUI(p, "Width", (u32)vp.w);
		setUI(p, "Height", (u32)vp.h);
		setUI(p, "OutWidth", (u32)ctx.output_size.x);
		setUI(p, "OutHeight", (u32)ctx.output_size.y);
		setI(p, "PerfQualityValue", ctx.perf_quality);
		setI(p, "DLSS.Feature.Create.Flags", NGX_DLSS_FLAG_IS_HDR | NGX_DLSS_FLAG_DEPTH_INVERTED | NGX_DLSS_FLAG_MV_LOW_RES | NGX_DLSS_FLAG_AUTO_EXPOSURE);
		setI(p, "DLSS.Enable.Output.Subrects", 0);
		const i32 res = g_ngx.create_feature(getCmdList(), NGX_FEATURE_SUPERSAMPLING, p, &ctx.feature);
		ctx.last_create_result = res;
		if (ngxFailed(res) || !ctx.feature) {
			gpu::popDebugGroup();
			logError("DLSS feature creation failed, code=", res);
			return false;
		}
		logInfo("DLSS: feature created for ", vp.w, "x", vp.h, " -> ", ctx.output_size.x, "x", ctx.output_size.y);
		ctx.reset = true;
		gpu::popDebugGroup();
		return true;
	}

	// Evaluates DLSS on current command list. Returns false on NGX failure.
	static bool evaluate(Context& ctx, gpu::TextureHandle color, gpu::TextureHandle depth, gpu::TextureHandle mv, gpu::TextureHandle output, const Viewport& vp, float frame_time_delta, i32* out_res = nullptr) {
		gpu::pushDebugGroup("DLSS Evaluate");
		NGXParameter* p = g_ngx.caps;
		p->Reset();

		setTex(p, "Color", color); // HDR scene color input.
		setTex(p, "Output", output); // DLSS output target.
		setTex(p, "Depth", depth); // Scene depth (inverted HW depth is enabled in create flags).
		setTex(p, "MotionVectors", mv); // Screen-space motion vectors.
		setF(p, "Jitter.Offset.X", vp.pixel_offset.x); // Current frame jitter X.
		setF(p, "Jitter.Offset.Y", vp.pixel_offset.y); // Current frame jitter Y.
		setI(p, "Reset", ctx.reset ? 1 : 0); // Reset DLSS history for first/invalidated frame.
		setF(p, "MV.Scale.X", 0.5f * vp.w); // Motion vector normalization scale X.
		setF(p, "MV.Scale.Y", -0.5f * vp.h); // Motion vector normalization scale Y with Y flip.
		setF(p, "FrameTimeDeltaInMsec", frame_time_delta * 1000.f);

		const i32 res = g_ngx.eval_feature(getCmdList(), ctx.feature, p, nullptr);
		ctx.last_eval_result = res;
		if (ngxFailed(res)) {
			if (out_res) *out_res = res;
			gpu::popDebugGroup();
			return false;
		}
		ctx.reset = false;
		gpu::resetCommandList();
		gpu::popDebugGroup();
		return true;
	}

	// RenderPlugin AA hook. Runs DLSS and returns output buffer when initialized.
	// Returns INVALID_RENDERBUFFER to let other AA paths continue when DLSS is unavailable.
	RenderBufferHandle renderAA(const GBuffer& gbuffer, RenderBufferHandle input, Pipeline& pipeline) override {
		if (!m_enabled || !g_ngx.initialized || !g_ngx.caps || g_ngx.broken) return INVALID_RENDERBUFFER;

		Renderer& renderer = pipeline.getRenderer();
		DrawStream& stream = renderer.getDrawStream();
		const Viewport vp = pipeline.getViewport();
		const IVec2 display_size = pipeline.getDisplaySize();
		const float frame_time_delta = renderer.getEngine().getLastTimeDelta();
		const i32 perf_quality = getPerfQuality();
		Context& ctx = getOrCreateContext(pipeline, display_size, perf_quality);

		// Scene/game views can report a transient size for one frame during layout changes.
		// Let the viewport settle before recreating/evaluating DLSS to avoid partial output.
		if (ctx.warmup_frames > 0) {
			--ctx.warmup_frames;
			return INVALID_RENDERBUFFER;
		}

		RenderBufferHandle output = renderer.createRenderbuffer({
			.size = display_size,
			.format = gpu::TextureFormat::RGBA16F,
			.flags = gpu::TextureFlags::RENDER_TARGET | gpu::TextureFlags::NO_MIPS | gpu::TextureFlags::COMPUTE_WRITE,
			.debug_name = "dlss_output"
		});

		gpu::TextureHandle color = renderer.toTexture(input);
		gpu::TextureHandle depth = renderer.toTexture(gbuffer.DS);
		gpu::TextureHandle motion = renderer.toTexture(gbuffer.D);
		gpu::TextureHandle out_tex = renderer.toTexture(output);

		pipeline.enablePixelJitter(true);
		pipeline.beginBlock("DLSS");
		stream.pushLambda([this, color, depth, motion, out_tex, vp, frame_time_delta, ctx_ptr = &ctx](){
			gpu::barrier(color, gpu::BarrierType::COMMON);
			gpu::barrier(depth, gpu::BarrierType::COMMON);
			gpu::barrier(motion, gpu::BarrierType::COMMON);
			gpu::barrier(out_tex, gpu::BarrierType::WRITE);
			if (!createFeatureIfNeeded(*ctx_ptr, vp)) {
				g_ngx.broken = true;
				logWarning("DLSS disabled for this session, feature creation failed.");
				return;
			}
			i32 eval_res = 0;
			if (!evaluate(*ctx_ptr, color, depth, motion, out_tex, vp, frame_time_delta, &eval_res)) {
				g_ngx.broken = true;
				logWarning("DLSS disabled for this session, evaluate failed with code=", eval_res, ". Falling back to other AA.");
			}
		});
		pipeline.endBlock();
		renderer.releaseRenderbuffer(input);
		return output;
	}

	void debugUI(Pipeline& pipeline) override {
		if (ImGui::BeginMenu("DLSS")) {
			ImGui::Checkbox("Enabled", &m_enabled);
			i32 mode = (i32)m_mode;
			if (ImGui::RadioButton("DLAA", &mode, (i32)Mode::DLAA)
				|| ImGui::RadioButton("Quality", &mode, (i32)Mode::QUALITY)
				|| ImGui::RadioButton("Balanced", &mode, (i32)Mode::BALANCED)
				|| ImGui::RadioButton("Performance", &mode, (i32)Mode::PERFORMANCE)
				|| ImGui::RadioButton("Ultra Performance", &mode, (i32)Mode::ULTRA_PERFORMANCE)) {
				m_mode = (Mode)mode;
				pipeline.setRenderToDisplayRatio(getRenderToDisplayScale());
			}
			ImGui::Text("NGX loaded: %s", g_ngx.api_loaded ? "yes" : "no");
			ImGui::Text("Initialized: %s", g_ngx.initialized ? "yes" : "no");
			ImGui::Text("Broken: %s", g_ngx.broken ? "yes" : "no");
			ImGui::Text("Caps: %s", g_ngx.caps ? "yes" : "no");
			ImGui::Text("Contexts: %d", m_contexts.size());

			const Viewport& vp = pipeline.getViewport();
			ImGui::Text("Viewport: %d x %d", vp.w, vp.h);

			Context* active = nullptr;
			for (UniquePtr<Context>& ctx : m_contexts) {
				if (ctx->pipeline == &pipeline) {
					active = ctx.get();
					break;
				}
			}

			if (active) {
				ImGui::Text("Active ctx size: %d x %d", active->size.x, active->size.y);
				ImGui::Text("Output size: %d x %d", active->output_size.x, active->output_size.y);
				ImGui::Text("Feature: %s", active->feature ? "created" : "missing");
				ImGui::Text("Reset: %s", active->reset ? "yes" : "no");
				ImGui::Text("Warmup frames: %u", active->warmup_frames);
				ImGui::Text("Last create result: %d", active->last_create_result);
				ImGui::Text("Last eval result: %d", active->last_eval_result);
			}
			else {
				ImGui::Text("No active context for this pipeline.");
			}

			ImGui::EndMenu();
		}
	}

	TagAllocator m_allocator;
	Array<UniquePtr<Context>> m_contexts;
	bool m_enabled = true;
	Mode m_mode = Mode::DLAA;

	i32 getPerfQuality() const {
		switch (m_mode) {
			case Mode::DLAA: return NGX_PERF_DLAA;
			case Mode::QUALITY: return NGX_PERF_MAX_QUALITY;
			case Mode::BALANCED: return NGX_PERF_BALANCED;
			case Mode::PERFORMANCE: return NGX_PERF_MAX_PERF;
			case Mode::ULTRA_PERFORMANCE: return NGX_PERF_ULTRA_PERFORMANCE;
		}
		return NGX_PERF_DLAA;
	}

	float getRenderToDisplayScale() const {
		switch (m_mode) {
			case Mode::DLAA: return 1.f;
			case Mode::QUALITY: return 1.f / 0.67f;
			case Mode::BALANCED: return 1.f / 0.58f;
			case Mode::PERFORMANCE: return 1.f / 0.5f;
			case Mode::ULTRA_PERFORMANCE: return 1.f / 0.33f;
		}
		return 1.f;
	}
};

bool initDLSS(Renderer& renderer, IAllocator& allocator) {
	if (g_ngx.initialized) return true;

	// Runtime gate: no NGX libs/symbols => no DLSS path.
	if (!loadNGXApi()) {
		logInfo("DLSS: NGX runtime not available");
		return false;
	}

	ID3D12Device* device = (ID3D12Device*)gpu::getDX12Device();
	if (!device) {
		logInfo("DLSS: DX12 device is not available");
		return false;
	}

	// Initialize NGX for current DX12 device.
	constexpr char PROJECT_ID[] = "6f392de5-bb1a-4d80-9f86-7d7f07b6df9d";
	constexpr char ENGINE_VERSION[] = "Lumix";
	// Project-ID path avoids dependence on NVIDIA-assigned numeric AppID.
	i32 init_res = 0;
	if (g_ngx.init_with_project_id) {
		init_res = g_ngx.init_with_project_id(PROJECT_ID, NGX_ENGINE_TYPE_CUSTOM, ENGINE_VERSION, L".", device, NGX_VERSION_API, nullptr);
	}
	else if (g_ngx.init) {
		init_res = g_ngx.init(0, L".", device, NGX_VERSION_API);
	}
	else {
		logWarning("DLSS init function is not available");
		return false;
	}
	if (ngxFailed(init_res)) {
		logWarning("DLSS NGX init failed, code=", init_res);
		return false;
	}
	logInfo("DLSS: NGX initialized");

	// Query global capability map and verify DLSS SuperSampling availability.
	NGXParameter* caps = nullptr;
	const i32 caps_res = g_ngx.get_caps(&caps);
	if (ngxFailed(caps_res) || !caps) {
		logWarning("DLSS capability query failed, code=", caps_res);
		// Some NGX runtime combinations crash on shutdown from failed init paths.
		// Keep DLSS disabled for this run instead of risking process termination.
		g_ngx.broken = true;
		return false;
	}
	g_ngx.caps = caps;
	logInfo("DLSS: capability parameters acquired");

	i32 available = 0;
	const i32 avail_res = g_ngx.caps->Get("SuperSampling.Available", &available);
	if (ngxFailed(avail_res)) {
		logInfo("DLSS availability query failed, code=", avail_res);
		g_ngx.broken = true;
		g_ngx.caps = nullptr;
		return false;
	}
	if (available == 0) {
		i32 feature_init_result = 0;
		const i32 feature_init_res = g_ngx.caps->Get("SuperSampling.FeatureInitResult", &feature_init_result);
		i32 needs_updated_driver = 0;
		const i32 driver_res = g_ngx.caps->Get("SuperSampling.NeedsUpdatedDriver", &needs_updated_driver);
		i32 min_driver_major = 0;
		const i32 min_driver_major_res = g_ngx.caps->Get("SuperSampling.MinDriverVersionMajor", &min_driver_major);
		i32 min_driver_minor = 0;
		const i32 min_driver_minor_res = g_ngx.caps->Get("SuperSampling.MinDriverVersionMinor", &min_driver_minor);

		logWarning("DLSS runtime reported SuperSampling unavailable.");
		if (!ngxFailed(feature_init_res)) logInfo("DLSS: SuperSampling.FeatureInitResult=", feature_init_result);
		if (!ngxFailed(driver_res)) logInfo("DLSS: SuperSampling.NeedsUpdatedDriver=", needs_updated_driver);
		if (!ngxFailed(min_driver_major_res) && !ngxFailed(min_driver_minor_res)) {
			logInfo("DLSS: minimum driver version reported by NGX is ", min_driver_major, ".", min_driver_minor);
		}
		g_ngx.caps = nullptr;
		return false;
	}
	else {
		logInfo("DLSS: SuperSampling is available");
	}

	// Register DLSS as AA plugin and disable builtin TAA when active.
	g_ngx.initialized = true;
	static DLSSPlugin plugin(allocator);
	renderer.addPlugin(plugin);
	renderer.enableBuiltinTAA(false);
	logInfo("DLSS initialized and plugin registered");
	return true;
}

} // namespace Lumix
