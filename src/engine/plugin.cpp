#include "foundation/array.h"
#include "foundation/debug.h"
#include "foundation/delegate_list.h"
#include "foundation/log.h"
#include "foundation/os.h"
#include "foundation/path.h"
#include "foundation/profiler.h"
#include "foundation/stream.h"
#include "foundation/string.h"

#include "engine/engine.h"
#include "engine/plugin.h"

namespace Lumix
{
	#define LUMIX_PLUGIN_DECLS
	#include "plugins.inl"
	#undef LUMIX_PLUGIN_DECLS

	ISystem::~ISystem() = default;

	struct SystemManagerImpl final : SystemManager
	{
		SystemManagerImpl(Engine& engine, IAllocator& allocator)
			: m_systems(allocator)
			, m_libraries(allocator)
			, m_allocator(allocator)
			, m_engine(engine)
			, m_library_loaded(allocator)
		{}


		~SystemManagerImpl()
		{
			while (!m_systems.empty())
			{
				LUMIX_DELETE(m_engine.getAllocator(), m_systems.back());
				m_systems.pop();
			}

			for (void* lib : m_libraries)
			{
				os::unloadLibrary(lib);
			}
		}


		void initSystems() override
		{
			PROFILE_FUNCTION();
			for (i32 i = 0, c = m_systems.size(); i < c; ++i) {
				m_systems[i]->initBegin();
			}
			for (i32 i = 0, c = m_systems.size(); i < c; ++i) {
				m_systems[i]->initEnd();
			}
		}


		void update(float dt) override
		{
			PROFILE_FUNCTION();
			for (int i = 0, c = m_systems.size(); i < c; ++i)
			{
				m_systems[i]->update(dt);
			}
		}


		void* getLibrary(ISystem* system) const override
		{
			int idx = m_systems.indexOf(system);
			if (idx < 0) return nullptr;

			return m_libraries[idx];
		}


		const Array<void*>& getLibraries() const override
		{
			return m_libraries;
		}


		const Array<ISystem*>& getSystems() const override
		{
			return m_systems;
		}


		ISystem* getSystem(const char* name) override
		{
			for (ISystem* system : m_systems)
			{
				if (equalStrings(system->getName(), name))
				{
					return system;
				}
			}
			return nullptr;
		}


		DelegateList<void(void*)>& libraryLoaded() override
		{
			return m_library_loaded;
		}
		

		void unload(ISystem* system) override
		{
			int idx = m_systems.indexOf(system);
			ASSERT(idx >= 0);
			LUMIX_DELETE(m_engine.getAllocator(), m_systems[idx]);
			os::unloadLibrary(m_libraries[idx]);
			m_libraries.erase(idx);
			m_systems.erase(idx);
		}


		ISystem* load(const char* path) override
		{
			char path_with_ext[MAX_PATH];
			copyString(path_with_ext, path);
			const char* ext =
			#ifdef _WIN32
				".dll";
			#elif defined __linux__
				".so";
			#else 
				#error Unknown platform
			#endif
			if (!Path::hasExtension(path, ext + 1)) catString(path_with_ext, ext);
			logInfo("loading system ", path_with_ext);
			using PluginCreator = ISystem* (*)(Engine&);
			auto* lib = os::loadLibrary(path_with_ext);
			if (lib)
			{
				PluginCreator creator = (PluginCreator)os::getLibrarySymbol(lib, "createPlugin");
				if (creator)
				{
					ISystem* system = creator(m_engine);
					if (!system)
					{
						logError("createPlugin failed.");
						LUMIX_DELETE(m_engine.getAllocator(), system);
						ASSERT(false);
					}
					else
					{
						addSystem(system, lib);
						m_library_loaded.invoke(lib);
						logInfo("Plugin loaded.");
						debug::StackTree::refreshModuleList();
						return system;
					}
				}
				else
				{
					logError("No createPlugin function in system.");
				}
				os::unloadLibrary(lib);
			}
			else {
				logWarning("Failed to load system.");
			}
			return nullptr;
		}


		IAllocator& getAllocator() { return m_allocator; }


		void addSystem(ISystem* system, void* library) override
		{
			m_systems.push(system);
			m_libraries.push(library);
			for (auto* i : m_systems)
			{
				i->systemAdded(*system);
				system->systemAdded(*i);
			}
		}


	private:
		Engine& m_engine;
		DelegateList<void(void*)> m_library_loaded;
		Array<void*> m_libraries;
		Array<ISystem*> m_systems;
		IAllocator& m_allocator;
	};
		

	UniquePtr<SystemManager> SystemManager::create(Engine& engine) {
		return UniquePtr<SystemManagerImpl>::create(engine.getAllocator(), engine, engine.getAllocator());
	}

	void SystemManager::createAllStatic(Engine& engine) {
		PROFILE_FUNCTION();
		#include "plugins.inl"
		for (ISystem* system : engine.getSystemManager().getSystems()) {
			logInfo("Plugin ", system->getName(), " loaded");
		}
	}
}
