#pragma once


#include "lumix.h"
#include "core/array.h"


namespace Lumix
{
namespace FS
{
class FileSystem;
}

namespace MTJD
{
class Manager;
}

class InputBlob;
class EditorServer;
class Hierarchy;
class InputSystem;
class IPlugin;
class IPropertyDescriptor;
class IScene;
class JsonSerializer;
class OutputBlob;
class PluginManager;
class ResourceManager;
class Universe;
class WorldEditor;


struct LUMIX_ENGINE_API UniverseContext
{
	UniverseContext(IAllocator& allocator)
		: m_scenes(allocator)
	{
	}

	IScene* getScene(uint32 hash) const;

	Universe* m_universe;
	Hierarchy* m_hierarchy;
	Array<IScene*> m_scenes;
};


class LUMIX_ENGINE_API Engine
{
public:
	virtual ~Engine() {}

	static Engine* create(FS::FileSystem* fs, IAllocator& allocator);
	static void destroy(Engine* engine, IAllocator& allocator);

	virtual UniverseContext& createUniverse() = 0;
	virtual void destroyUniverse(UniverseContext& context) = 0;

	virtual FS::FileSystem& getFileSystem() = 0;
	virtual InputSystem& getInputSystem() = 0;
	virtual PluginManager& getPluginManager() = 0;
	virtual MTJD::Manager& getMTJDManager() = 0;
	virtual ResourceManager& getResourceManager() = 0;
	virtual IAllocator& getAllocator() = 0;

	virtual void startGame(UniverseContext& context) = 0;
	virtual void stopGame(UniverseContext& context) = 0;

	virtual void update(UniverseContext& context) = 0;
	virtual uint32 serialize(UniverseContext& ctx, OutputBlob& serializer) = 0;
	virtual bool deserialize(UniverseContext& ctx, InputBlob& serializer) = 0;
	virtual float getFPS() const = 0;
	virtual float getLastTimeDelta() = 0;

protected:
	Engine() {}
};


} // ~namespace Lumix
