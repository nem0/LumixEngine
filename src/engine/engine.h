#pragma once


#include "lumix.h"


struct lua_State;


namespace Lumix
{
namespace FS
{
class DiskFileDevice;
class FileSystem;
}

namespace MTJD
{
class Manager;
}

class InputBlob;
class IAllocator;
class InputSystem;
class OutputBlob;
class PathManager;
class PluginManager;
class ResourceManager;
class Universe;


class LUMIX_ENGINE_API Engine
{
public:
	struct PlatformData
	{
		void* window_handle;
	};

public:
	virtual ~Engine() {}

	static Engine* create(const char* base_path0,
		const char* base_path1,
		FS::FileSystem* fs,
		IAllocator& allocator);
	static void destroy(Engine* engine, IAllocator& allocator);

	virtual Universe& createUniverse() = 0;
	virtual void destroyUniverse(Universe& context) = 0;
	virtual void setPlatformData(const PlatformData& data) = 0;
	virtual const PlatformData& getPlatformData() = 0;

	virtual FS::FileSystem& getFileSystem() = 0;
	virtual FS::DiskFileDevice* getDiskFileDevice() = 0;
	virtual FS::DiskFileDevice* getPatchFileDevice() = 0;
	virtual InputSystem& getInputSystem() = 0;
	virtual PluginManager& getPluginManager() = 0;
	virtual MTJD::Manager& getMTJDManager() = 0;
	virtual ResourceManager& getResourceManager() = 0;
	virtual IAllocator& getAllocator() = 0;

	virtual void startGame(Universe& context) = 0;
	virtual void stopGame(Universe& context) = 0;

	virtual void update(Universe& context) = 0;
	virtual uint32 serialize(Universe& ctx, OutputBlob& serializer) = 0;
	virtual bool deserialize(Universe& ctx, InputBlob& serializer) = 0;
	virtual float getFPS() const = 0;
	virtual float getLastTimeDelta() = 0;
	virtual void setTimeMultiplier(float multiplier) = 0;
	virtual void pause(bool pause) = 0;
	virtual void nextFrame() = 0;
	virtual PathManager& getPathManager() = 0;
	virtual lua_State* getState() = 0;

protected:
	Engine() {}
};


} // ~namespace Lumix
