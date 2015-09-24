#pragma once


#include "lumix.h"
#include "core/array.h"


namespace Lumix
{
namespace FS
{
class FileSystem;
}

class Hierarchy;
namespace MTJD
{
class Manager;
}

class InputBlob;
class EditorServer;
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

	IScene* getScene(uint32_t hash) const;

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
	virtual IPlugin* loadPlugin(const char* name) = 0;
	virtual MTJD::Manager& getMTJDManager() = 0;
	virtual ResourceManager& getResourceManager() = 0;
	virtual IAllocator& getAllocator() = 0;

	virtual void startGame(UniverseContext& context) = 0;
	virtual void stopGame(UniverseContext& context) = 0;

	virtual void update(UniverseContext& context) = 0;
	virtual uint32_t serialize(UniverseContext& ctx, OutputBlob& serializer) = 0;
	virtual bool deserialize(UniverseContext& ctx, InputBlob& serializer) = 0;
	virtual float getFPS() const = 0;
	virtual float getLastTimeDelta() = 0;
	
	virtual const IPropertyDescriptor&
		getPropertyDescriptor(uint32_t type, uint32_t name_hash) = 0;
	virtual Array<IPropertyDescriptor*>&
		getPropertyDescriptors(uint32_t type) = 0;
	virtual void registerProperty(const char* name, IPropertyDescriptor* desc) = 0;
	virtual IPropertyDescriptor* getProperty(const char* component_type,
											 const char* property_name) = 0;
	virtual void registerComponentType(const char* name, const char* title) = 0;
	virtual int getComponentTypesCount() const = 0;
	virtual const char* getComponentTypeName(int index) = 0;
	virtual const char* getComponentTypeID(int index) = 0;

protected:
	Engine() {}
};


} // ~namespace Lumix
