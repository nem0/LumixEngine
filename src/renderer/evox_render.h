#pragma once

#include "core/array.h"
#include "core/hash_map.h"
#include "core/path.h"
#include "core/string.h"
#include "evox/capi.h"
#include "evox/evox_module.h"
#include "renderer/renderer.h"

namespace Lumix {

struct Engine;
struct EvoxRenderContext;
struct Shader;

struct LUMIX_RENDERER_API EvoxRender : RenderPlugin {
	using NativeFunctions = HashMap<NativeFunctionKey, ex_native_fn, NativeFunctionKeyHash>;

	EvoxRender(Engine& engine, IAllocator& allocator);

	// Finds `main` and `debugUI` of `render.evox` in `runtime`, which is owned by the caller and must outlive the next
	// `detach`. Does nothing if the bytecode has no `render.evox` unit.
	void attach(ex_bytecode* bytecode, ex_runtime* runtime);
	// Drops the script state and everything that belongs to the runtime. Call it before the runtime is destroyed.
	void detach();
	Shader* loadShader(StringView path);
	// GPU buffers created by scripts (core:render.Storage), ids are index + 1, 0 is invalid.
	u32 createStorage(Renderer& renderer, u32 size);
	gpu::BufferHandle getStorage(u32 id) const;
	// The buffer is destroyed on the next renderFrame call.
	void releaseStorage(u32 id);
	// Zero-initialized memory of a script type with index `type_index`, one per pipeline and type. Only valid while a
	// script call runs. Returns nullptr for an invalid type.
	u8* getState(Pipeline& pipeline, u32 type_index, u32& size);

	// Picks what the debug view of the pipeline shows: the effect output `name` of the script. See ctx.showDebug.
	void showDebug(Pipeline& pipeline, StringView name);
	bool isDebugShown(Pipeline& pipeline, StringView name) const;

	void shutdown(Renderer& renderer) override;
	void pipelineDestroyed(Pipeline& pipeline) override;
	void debugUI(Pipeline& pipeline) override;
	// Runs `main` of render.evox, the whole 3D frame. Returns false if there is no script (or it failed), the pipeline draws only the UI then.
	bool renderFrame(RenderBufferHandle& output, Pipeline& pipeline);

private:
	static constexpr u32 INVALID_FUNCTION = 0xffFFffFF;


	// per pipeline script state, see core:render `state`
	struct StateBlock {
		Pipeline* pipeline;
		u32 type_index;
		u32 size;
		u8* memory;
		const ex_type* type;
	};

	// what the debug view of a pipeline shows, set by scripts with `showDebug`
	struct DebugSelection {
		Pipeline* pipeline;
		StaticString<64> name;
	};

	struct ShaderEntry {
		Path path;
		Shader* shader;
	};

	// A task that failed (e.g. a panic) stays suspended and can't run anything else, replace it.
	void recreateTask();
	// Queues all core:render.RenderBuffer in the value for release and frees the block.
	void freeState(StateBlock& block);
	void queueBuffersForRelease(const ex_type* type, const u8* memory);
	void destroyPending(Renderer& renderer);
	bool callEntry(u32 function, EvoxRenderContext& context);

	Engine& m_engine;
	IAllocator& m_allocator;
	// everything runs on the update thread: main is called by Pipeline::render and resource callbacks come from FileSystem::processCallbacks
	// owned by the Evox system
	ex_bytecode* m_bytecode = nullptr;
	ex_runtime* m_runtime = nullptr;
	ex_task* m_task = nullptr;
	// functions of render.evox (`main` and the optional `debugUI`), INVALID_FUNCTION if missing or failed
	u32 m_main_function = INVALID_FUNCTION;
	u32 m_debug_ui_function = INVALID_FUNCTION;
	Array<StateBlock> m_state;
	// render buffers owned by freed state, released on the next renderFrame call (render buffers are not released from other threads)
	Array<RenderBufferHandle> m_pending_release;
	// gpu::INVALID_BUFFER for free slots
	Array<gpu::BufferHandle> m_storage;
	Array<gpu::BufferHandle> m_pending_destroy;
	Array<ShaderEntry> m_shaders;
	Array<DebugSelection> m_debug_selection;
};

namespace Evox {
	LUMIX_RENDERER_API void registerRenderFunctions(EvoxRender::NativeFunctions& functions);
}

} // namespace Lumix
