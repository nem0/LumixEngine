#pragma once

#include "engine/lumix.h"
#include "engine/allocator.h"

struct lua_State;

namespace Lumix {

namespace os { using WindowHandle = void*; }

struct LUMIX_ENGINE_API Engine {
	struct InitArgs {
		const char* working_dir = nullptr;
		Span<const char*> plugins;
		bool fullscreen = false;
		bool handle_file_drops = false;
		const char* window_title = "Lumix App";
		UniquePtr<struct FileSystem> file_system; 
	};

	using LuaResourceHandle = u32;

	virtual ~Engine() {}

	static UniquePtr<Engine> create(InitArgs&& init_data, struct IAllocator& allocator);

	virtual struct Universe& createUniverse(bool is_main_universe) = 0;
	virtual void destroyUniverse(Universe& context) = 0;
	virtual os::WindowHandle getWindowHandle() = 0;

	virtual struct FileSystem& getFileSystem() = 0;
	virtual struct InputSystem& getInputSystem() = 0;
	virtual struct PluginManager& getPluginManager() = 0;
	virtual struct ResourceManagerHub& getResourceManager() = 0;
	virtual struct PageAllocator& getPageAllocator() = 0;
	virtual IAllocator& getAllocator() = 0;
	virtual bool instantiatePrefab(Universe& universe,
		const struct PrefabResource& prefab,
		const struct DVec3& pos,
		const struct Quat& rot,
		float scale,
		struct EntityMap& entity_map) = 0;

	virtual void startGame(Universe& context) = 0;
	virtual void stopGame(Universe& context) = 0;

	virtual void update(Universe& context) = 0;
	virtual u32 serialize(Universe& ctx, struct OutputMemoryStream& serializer) = 0;
	virtual bool deserialize(Universe& ctx, struct InputMemoryStream& serializer, struct EntityMap& entity_map) = 0;
	virtual bool deserializeProject(InputMemoryStream& serializer) = 0;
	virtual void serializeProject(OutputMemoryStream& serializer) const = 0;
	virtual float getLastTimeDelta() const = 0;
	virtual void setTimeMultiplier(float multiplier) = 0;
	virtual void pause(bool pause) = 0;
	virtual bool isPaused() const = 0;
	virtual void nextFrame() = 0;
	virtual lua_State* getState() = 0;

	virtual struct Resource* getLuaResource(LuaResourceHandle idx) const = 0;
	virtual LuaResourceHandle addLuaResource(const struct Path& path, struct ResourceType type) = 0;
	virtual void unloadLuaResource(LuaResourceHandle resource_idx) = 0;

protected:
	Engine() {}
	Engine(const Engine&) = delete;
};

} // namespace Lumix
