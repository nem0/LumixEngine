#pragma once


#include "engine/lumix.h"


struct lua_State;


namespace Lumix
{
namespace FS
{
class DiskFileDevice;
class ResourceFileDevice;
class FileSystem;
}

struct ComponentUID;
class InputBlob;
struct IAllocator;
class InputSystem;
class OutputBlob;
class Path;
class PathManager;
class PluginManager;
class ResourceManager;
class Universe;
template <typename T> class Array;


class LUMIX_ENGINE_API Engine
{
public:
	struct PlatformData
	{
		void* window_handle;
		void* display;
	};

public:
	virtual ~Engine() {}

	static Engine* create(const char* working_dir,
		FS::FileSystem* fs,
		IAllocator& allocator);
	static void destroy(Engine* engine, IAllocator& allocator);

	virtual const char* getWorkingDirectory() const = 0;
	virtual Universe& createUniverse(bool set_lua_globals) = 0;
	virtual void destroyUniverse(Universe& context) = 0;
	virtual void setPlatformData(const PlatformData& data) = 0;
	virtual const PlatformData& getPlatformData() = 0;

	virtual FS::FileSystem& getFileSystem() = 0;
	virtual FS::DiskFileDevice* getDiskFileDevice() = 0;
	virtual FS::ResourceFileDevice* getResourceFileDevice() = 0;
	virtual InputSystem& getInputSystem() = 0;
	virtual PluginManager& getPluginManager() = 0;
	virtual ResourceManager& getResourceManager() = 0;
	virtual IAllocator& getAllocator() = 0;

	virtual void startGame(Universe& context) = 0;
	virtual void stopGame(Universe& context) = 0;

	virtual void update(Universe& context) = 0;
	virtual u32 serialize(Universe& ctx, OutputBlob& serializer) = 0;
	virtual bool deserialize(Universe& ctx, InputBlob& serializer) = 0;
	virtual float getFPS() const = 0;
	virtual double getTime() const = 0;
	virtual float getLastTimeDelta() const = 0;
	virtual void setTimeMultiplier(float multiplier) = 0;
	virtual void pause(bool pause) = 0;
	virtual void nextFrame() = 0;
	virtual PathManager& getPathManager() = 0;
	virtual lua_State* getState() = 0;
	virtual void runScript(const char* src, int src_length, const char* path) = 0;
	virtual ComponentUID createComponent(Universe& universe, EntityRef entity, ComponentType type) = 0;
	virtual IAllocator& getLIFOAllocator() = 0;
	virtual class Resource* getLuaResource(int idx) const = 0;
	virtual int addLuaResource(const Path& path, struct ResourceType type) = 0;
	virtual void unloadLuaResource(int resource_idx) = 0;

protected:
	Engine() {}
};


} // namespace Lumix
