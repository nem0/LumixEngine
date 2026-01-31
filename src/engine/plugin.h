#pragma once


#include "engine/black.h.h"


namespace black
{

template <typename T> struct Array;
template <typename T> struct DelegateList;
template <typename T> struct UniquePtr;

// manages engine systems/plugins
struct BLACK_ENGINE_API SystemManager
{
	virtual ~SystemManager() {}

	static UniquePtr<SystemManager> create(struct Engine& engine);
	static void createAllStatic(Engine& engine);
	
	virtual void initSystems() = 0;
	virtual void unload(struct ISystem* system) = 0;
	virtual ISystem* load(const char* path) = 0;
	virtual void addSystem(ISystem* system, void* library) = 0;
	virtual void update(float dt) = 0;
	virtual ISystem* getSystem(const char* name) = 0;
	virtual const Array<ISystem*>& getSystems() const = 0;
	virtual const Array<void*>& getLibraries() const = 0;
	virtual void* getLibrary(ISystem* system) const = 0;
	virtual DelegateList<void(void*)>& libraryLoaded() = 0;
};

// Modules inherited from IModule manage components of certain types in single world,
// e.g. RenderModule manages all render components - models, lights, ... 
// Each world has its own instance of every type of module, e.g. RenderModule, AnimationModule, ...
struct BLACK_ENGINE_API IModule
{
	virtual ~IModule() {}

	virtual void init() {}
	virtual void serialize(struct OutputMemoryStream& serializer) = 0;
	virtual void deserialize(struct InputMemoryStream& serialize, const struct EntityMap& entity_map, i32 version) = 0;
	virtual void beforeReload(OutputMemoryStream& serializer) {}
	virtual void afterReload(InputMemoryStream& serializer) {}
	virtual const char* getName() const = 0;
	virtual ISystem& getSystem() const = 0;
	
	// called for all modules at once, i.e. all modules are updated in parallel
	virtual void updateParallel(float time_delta) {}
	// called after all updateParallel calls are finished, called on "main thread"
	virtual void update(float time_delta) = 0;
	// called after all update calls are finished, called on "main thread"
	virtual void lateUpdate(float time_delta) {}
	
	virtual void endFrame() {}
	virtual struct World& getWorld() = 0;
	virtual void startGame() {}
	virtual void stopGame() {}
	virtual i32 getVersion() const { return -1; }
};

// There should be single instance in whole app of every system inherited from ISystem, e.g. only one renderer, one animation system, ...
struct BLACK_ENGINE_API ISystem
{
	virtual ~ISystem();

	// can start async stuff, called for all systems at once
	virtual void initBegin() {}
	// wait for all async stuff to finish, called after initBegin is called on all system
	virtual void initEnd() {}

	// shutdown is in progress, other systems still exists
	virtual void shutdownStarted() {}

	virtual void update(float) {}
	virtual const char* getName() const = 0;
	virtual i32 getVersion() const { return 0; }
	virtual void serialize(OutputMemoryStream& serializer) const = 0;
	virtual bool deserialize(i32 version, InputMemoryStream& serializer) = 0;
	virtual void systemAdded(ISystem& system) {}

	virtual void createModules(World&) {}
	virtual void startGame() {}
	virtual void stopGame() {}
};


} // namespace black


#ifdef STATIC_PLUGINS
	#define BLACK_PLUGIN_ENTRY(plugin_name) extern "C" black.h::ISystem* createPlugin_##plugin_name(black.h::Engine& engine)
#else
	#define BLACK_PLUGIN_ENTRY(plugin_name) extern "C" BLACK_LIBRARY_EXPORT black.h::ISystem* createPlugin(black.h::Engine& engine)
#endif

