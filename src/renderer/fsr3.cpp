#include "core/job_system.h"
#include "core/log.h"
#include "core/os.h"
#include "core/tag_allocator.h"
#include "engine/engine.h"
#include "ffx_api/ffx_api.h"
#include "ffx_api/ffx_upscale.h"
#include "ffx_api/dx12/ffx_api_dx12.h"
#undef near
#undef far
#undef NEAR
#undef RELATIVE
#undef FAR
#include "pipeline.h"
#include "renderer.h"

namespace Lumix {

namespace gpu {
	void* getDX12Device();
	void* getDX12CommandList();
	void* getDX12Resource(TextureHandle h);
	void resetCommandList();
}

enum class FfxResourceStates {
    FFX_RESOURCE_STATE_COMMON               = (1 << 0),
    FFX_RESOURCE_STATE_UNORDERED_ACCESS     = (1 << 1),
    FFX_RESOURCE_STATE_COMPUTE_READ         = (1 << 2),
    FFX_RESOURCE_STATE_PIXEL_READ           = (1 << 3),
    FFX_RESOURCE_STATE_PIXEL_COMPUTE_READ   = (FFX_RESOURCE_STATE_PIXEL_READ | FFX_RESOURCE_STATE_COMPUTE_READ),
    FFX_RESOURCE_STATE_COPY_SRC             = (1 << 4),
    FFX_RESOURCE_STATE_COPY_DEST            = (1 << 5),
    FFX_RESOURCE_STATE_GENERIC_READ         = (FFX_RESOURCE_STATE_COPY_SRC | FFX_RESOURCE_STATE_COMPUTE_READ), 
    FFX_RESOURCE_STATE_INDIRECT_ARGUMENT    = (1 << 6),
    FFX_RESOURCE_STATE_PRESENT              = (1 << 7),
    FFX_RESOURCE_STATE_RENDER_TARGET        = (1 << 8),
};

static PfnFfxCreateContext api_ffxCreateContext;
static PfnFfxDestroyContext api_ffxDestroyContext;
static PfnFfxDispatch api_ffxDispatch;
static PfnFfxConfigure api_ffxConfigure;
static PfnFfxQuery api_ffxQuery;

struct FSR3Plugin : RenderPlugin {
	FSR3Plugin(IAllocator& allocator) 
		: m_allocator(allocator, "FSR3")
		, m_contexts(allocator)
	{
		m_alloc_callbacks.pUserData = this;
		m_alloc_callbacks.alloc = [](void* user_data, uint64_t size) -> void* {
			return static_cast<FSR3Plugin*>(user_data)->m_allocator.allocate(size, 8);
		};
		m_alloc_callbacks.dealloc = [](void* user_data, void* ptr) {
			static_cast<FSR3Plugin*>(user_data)->m_allocator.deallocate(ptr);
		};
	}

	static void ffxMessageCallback(uint32_t type, const wchar_t* message) {
		ASSERT(false);
	}
	
	struct Context {
		IVec2 size;
		Pipeline* pipeline;
		ffxContext fsr;
		u32 frames_since_last_use = 0;
	};

	void frame(Renderer&) override {
		for (i32 i = m_contexts.size() - 1; i >= 0; --i) {
			UniquePtr<Context>& ctx = m_contexts[i];
			++ctx->frames_since_last_use;
			if (ctx->frames_since_last_use < 6) continue;
			
			ffxReturnCode_t retCode = api_ffxDestroyContext(&ctx->fsr, &m_alloc_callbacks);
			m_contexts.swapAndPop(i);
			if (retCode != FFX_API_RETURN_OK) {
				logError("Failed to destroy FSR3 context");
			}
		}
	}

	Context& getOrCreateContext(Pipeline& pipeline) {
		const Viewport& vp = pipeline.getViewport();
		IVec2 size = {vp.w, vp.h};
		// look for existing context
		for (const UniquePtr<Context>& ctx : m_contexts) {
			if (ctx->pipeline == &pipeline) {
				ctx->frames_since_last_use = 0;
				if (ctx->size == size) return *ctx.get();
				
				// we found context with different size, mark it for deletion
				// we can't delete it here because gpu might still be using it
				ctx->pipeline = nullptr; // set to nullptr so we don't reuse it
				break;
			}
		}
		
		// new context
		UniquePtr<Context> ctx = UniquePtr<Context>::create(m_allocator);
		ctx->pipeline = &pipeline;
		ctx->size = size;
		Context* ctx_ptr = ctx.get();
		m_contexts.push(ctx.move());
		
		DrawStream& stream = pipeline.getRenderer().getDrawStream();
		// init gpu resources
		stream.pushLambda([ctx_ptr, size, this](){
			ffxCreateBackendDX12Desc createBackend = {
				.header = {
					.type = FFX_API_CREATE_CONTEXT_DESC_TYPE_BACKEND_DX12,
				},
				.device =  (ID3D12Device*)gpu::getDX12Device(),
			};

			ffxCreateContextDescUpscale createUpscale = {
				.flags = FFX_UPSCALE_ENABLE_DEPTH_INVERTED | FFX_UPSCALE_ENABLE_DEPTH_INFINITE | FFX_UPSCALE_ENABLE_HIGH_DYNAMIC_RANGE,
				.maxRenderSize = { (u32)size.x, (u32)size.y },
				.maxUpscaleSize = { (u32)size.x, (u32)size.y },
			};

			#ifdef LUMIX_DEBUG
				createUpscale.flags |= FFX_UPSCALE_ENABLE_DEBUG_CHECKING;
				createUpscale.fpMessage = &ffxMessageCallback;
			#endif
			createUpscale.header.type = FFX_API_CREATE_CONTEXT_DESC_TYPE_UPSCALE;
			createUpscale.header.pNext = &createBackend.header;

			ffxReturnCode_t retCode = api_ffxCreateContext(&ctx_ptr->fsr, &createUpscale.header, &m_alloc_callbacks);
			if (retCode != FFX_API_RETURN_OK) {
				logError("Failed to create FSR3 context");
			}
		});

		return *ctx_ptr;
	}

	RenderBufferHandle renderAA(const GBuffer& gbuffer, RenderBufferHandle input, Pipeline& pipeline) override {
		DrawStream& stream = pipeline.getRenderer().getDrawStream();
		
		Context& ctx = getOrCreateContext(pipeline);

		gpu::TextureHandle color = pipeline.toTexture(input);
		gpu::TextureHandle depth = pipeline.toTexture(gbuffer.DS);
		gpu::TextureHandle motion_vectors = pipeline.toTexture(gbuffer.D);

		RenderBufferHandle output = pipeline.createRenderbuffer({
			.format = gpu::TextureFormat::RGBA16F,
			.flags = gpu::TextureFlags::RENDER_TARGET | gpu::TextureFlags::NO_MIPS | gpu::TextureFlags::COMPUTE_WRITE,
			.debug_name = "fsr3_output"
		});
		const float time_delta = pipeline.getRenderer().getEngine().getLastTimeDelta();
		gpu::TextureHandle output_tex = pipeline.toTexture(output);
		const Viewport& vp = pipeline.getViewport();

		pipeline.enablePixelJitter(true);
		pipeline.beginBlock("FSR3 Upscale");
		stream.pushLambda([color, depth, motion_vectors, output_tex, vp, time_delta, ctx_ptr = &ctx](){
			dispatch(color, depth, motion_vectors, output_tex, vp, time_delta, *ctx_ptr);
		});
		pipeline.endBlock();

		return output;
	}

	static FfxApiResource toFFXResource(gpu::TextureHandle texture, FfxResourceStates state, bool is_depth, IVec2 size) {
		FfxApiResource res = {};
		res.resource = gpu::getDX12Resource(texture);
		res.state = (u32)state;
		res.description = {
			.type = FFX_API_RESOURCE_TYPE_TEXTURE2D, 
			.flags = 0,
			.usage = u32(is_depth ? FFX_API_RESOURCE_USAGE_DEPTHTARGET : FFX_API_RESOURCE_USAGE_READ_ONLY),
		};

		res.description.mipCount = 1;
		res.description.depth = 1;
		res.description.width = size.x;
		res.description.height = size.y;

		return res;
	}

	static void dispatch(gpu::TextureHandle color, gpu::TextureHandle depth, gpu::TextureHandle motion_vectors, gpu::TextureHandle output, const Viewport& vp, float time_delta, Context& ctx) {
		const IVec2 size = { (int)vp.w, (int)vp.h };
		gpu::barrier(color, gpu::BarrierType::COMMON);
		gpu::barrier(depth, gpu::BarrierType::COMMON);
		gpu::barrier(motion_vectors, gpu::BarrierType::COMMON);
		gpu::barrier(output, gpu::BarrierType::COMMON);
		ffxDispatchDescUpscale desc = {
			.header = { .type = FFX_API_DISPATCH_DESC_TYPE_UPSCALE },
			.commandList = gpu::getDX12CommandList(),
			.color = toFFXResource(color, FfxResourceStates::FFX_RESOURCE_STATE_COMMON, false, size),
			.depth = toFFXResource(depth, FfxResourceStates::FFX_RESOURCE_STATE_COMMON, true, size),
			.motionVectors = toFFXResource(motion_vectors, FfxResourceStates::FFX_RESOURCE_STATE_COMMON, false, size),
			.output = toFFXResource(output, FfxResourceStates::FFX_RESOURCE_STATE_COMMON, false, size),
			.jitterOffset = { vp.pixel_offset.x, vp.pixel_offset.y },
			.motionVectorScale = {  0.5f * vp.w, -0.5f * vp.h },
			.renderSize = {(u32)vp.w, (u32)vp.h}, // The resolution that was used for rendering the input resources.
			.upscaleSize = {(u32)vp.w, (u32)vp.h}, // The resolution that the upscaler will upscale to
			.enableSharpening = false,
			.frameTimeDelta = time_delta * 1000.f,
			.preExposure = 1.f,
			.reset = false,
			.cameraNear = FLT_MAX,
			.cameraFar = FLT_MAX,
			.cameraFovAngleVertical = vp.fov,
			.viewSpaceToMetersFactor = 1.f,
			.flags = 0 //FFX_UPSCALE_FLAG_DRAW_DEBUG_VIEW
		};
		ffxReturnCode_t retCode =  api_ffxDispatch(&ctx.fsr, &desc.header);
		ASSERT(retCode == FFX_API_RETURN_OK);
		gpu::resetCommandList();
	}

	TagAllocator m_allocator;
	Array<UniquePtr<Context>> m_contexts;
	ffxAllocationCallbacks m_alloc_callbacks;
};

// init FSR3 if available
void initFSR3(Renderer& renderer, IAllocator& allocator) {
	void* ffx_lib = os::loadLibrary("amd_fidelityfx_dx12.dll");
	if (!ffx_lib) return;

	logInfo("Loaded amd_fidelityfx_dx12.dll");
	
	#define LOAD_FN(name) \
		api_##name = (decltype(api_##name))os::getLibrarySymbol(ffx_lib, #name); \
		if (!api_##name) { \
			logError("Failed to load ", #name, " from amd_fidelityfx_dx12.dll"); \
			os::unloadLibrary(ffx_lib); \
			return; \
		}

	LOAD_FN(ffxCreateContext);
	LOAD_FN(ffxDestroyContext);
	LOAD_FN(ffxConfigure);
	LOAD_FN(ffxQuery);
	LOAD_FN(ffxDispatch);

	#undef LOAD_FN

	static FSR3Plugin plugin(allocator);
	renderer.addPlugin(plugin);
	renderer.enableBuiltinTAA(false);
}

}