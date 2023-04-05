#pragma once


#include "engine/lumix.h"


namespace Lumix
{

template <typename T> struct Array;
template <typename T> struct DelegateList;
template <typename T> struct UniquePtr;

struct LUMIX_ENGINE_API PluginManager
{
	virtual ~PluginManager() {}

	static UniquePtr<PluginManager> create(struct Engine& engine);
	static void createAllStatic(Engine& engine);
	
	virtual void initPlugins() = 0;
	virtual void unload(struct IPlugin* plugin) = 0;
	virtual IPlugin* load(const char* path) = 0;
	virtual void addPlugin(IPlugin* plugin, void* library) = 0;
	virtual void update(float dt) = 0;
	virtual IPlugin* getPlugin(const char* name) = 0;
	virtual const Array<IPlugin*>& getPlugins() const = 0;
	virtual const Array<void*>& getLibraries() const = 0;
	virtual void* getLibrary(IPlugin* plugin) const = 0;
	virtual DelegateList<void(void*)>& libraryLoaded() = 0;
};

struct LUMIX_ENGINE_API IScene
{
	virtual ~IScene() {}

	virtual void init() {}
	virtual void serialize(struct OutputMemoryStream& serializer) = 0;
	virtual void deserialize(struct InputMemoryStream& serialize, const struct EntityMap& entity_map, i32 version) = 0;
	virtual void beforeReload(OutputMemoryStream& serializer) {}
	virtual void afterReload(InputMemoryStream& serializer) {}
	virtual IPlugin& getPlugin() const = 0;
	virtual void update(float time_delta) = 0;
	virtual void lateUpdate(float time_delta) {}
	virtual struct World& getWorld() = 0;
	virtual void startGame() {}
	virtual void stopGame() {}
	virtual i32 getVersion() const { return -1; }
	virtual void clear() = 0;
};


struct LUMIX_ENGINE_API IPlugin
{
	virtual ~IPlugin();

	virtual void init() {}
	virtual void update(float) {}
	virtual const char* getName() const = 0;
	virtual u32 getVersion() const = 0;
	virtual void serialize(OutputMemoryStream& serializer) const = 0;
	virtual bool deserialize(u32 version, InputMemoryStream& serializer) = 0;
	virtual void pluginAdded(IPlugin& plugin) {}

	virtual void createScenes(World&) {}
	virtual void startGame() {}
	virtual void stopGame() {}
};


} // namespace Lumix


#ifdef STATIC_PLUGINS
	#define LUMIX_PLUGIN_ENTRY(plugin_name) extern "C" IPlugin* createPlugin_##plugin_name(Engine& engine)
#else
	#define LUMIX_PLUGIN_ENTRY(plugin_name) extern "C" LUMIX_LIBRARY_EXPORT IPlugin* createPlugin(Engine& engine)
#endif

