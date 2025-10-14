#pragma once

#include "engine/lumix.h"

#include "core/allocator.h"
#include "core/span.h"

namespace Lumix {

struct Path;

namespace os { using WindowHandle = void*; }

enum class DeserializeProjectResult {
	SUCCESS,
	CORRUPTED_FILE,
	VERSION_NOT_SUPPORTED,
	PLUGIN_NOT_FOUND,
	PLUGIN_DESERIALIZATION_FAILED,
	PLUGIN_VERSION_NOT_SUPPORTED
};

struct LUMIX_ENGINE_API Engine {
	struct InitArgs {
		const char* log_path = "engine/lumix.log";
		Span<const char*> plugins;
		UniquePtr<struct FileSystem> file_system;
		const char* engine_data_dir = nullptr;
	};

	virtual ~Engine() {}

	static UniquePtr<Engine> create(InitArgs&& init_data, struct IAllocator& allocator);

	virtual void init() = 0;
	virtual struct World& createWorld() = 0;
	virtual void destroyWorld(World& world) = 0;
	virtual void setMainWindow(os::WindowHandle win) = 0;
	virtual os::WindowHandle getMainWindow() = 0;

	virtual struct FileSystem& getFileSystem() = 0;
	virtual struct InputSystem& getInputSystem() = 0;
	virtual struct SystemManager& getSystemManager() = 0;
	virtual struct ResourceManagerHub& getResourceManager() = 0;
	virtual struct PageAllocator& getPageAllocator() = 0;
	virtual IAllocator& getAllocator() = 0;
	virtual EntityPtr instantiatePrefab(World& world,
		const struct PrefabResource& prefab,
		const struct DVec3& pos,
		const struct Quat& rot,
		const struct Vec3& scale,
		struct EntityMap& entity_map) = 0;

	virtual void startGame(World& world) = 0;
	virtual void stopGame(World& world) = 0;

	virtual void update(World& world) = 0;
	[[nodiscard]] virtual DeserializeProjectResult deserializeProject(struct InputMemoryStream& serializer, Path& startup_world) = 0;
	virtual void serializeProject(struct OutputMemoryStream& serializer, const Path& startup_world) const = 0;
	virtual float getLastTimeDelta() const = 0;
	virtual void setTimeMultiplier(float multiplier) = 0;
	virtual void pause(bool pause) = 0;
	virtual bool isPaused() const = 0;
	virtual void nextFrame() = 0;
	virtual bool decompress(Span<const u8> src, Span<u8> dst) = 0;
	virtual bool compress(Span<const u8> src, OutputMemoryStream& dst) = 0;

protected:
	Engine() {}
	Engine(const Engine&) = delete;
};

} // namespace Lumix
