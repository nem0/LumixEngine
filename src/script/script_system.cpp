#include "script_system.h"
#include <Windows.h>
#include "core/iallocator.h"
#include "core/crc32.h"
#include "core/fs/file_system.h"
#include "core/fs/ifile.h"
#include "core/json_serializer.h"
#include "core/log.h"
#include "core/array.h"
#include "editor/world_editor.h"
#include "engine/engine.h"
#include "universe/universe.h"


static const uint32_t SCRIPT_HASH = crc32("script");


namespace Lumix
{
	class ScriptSystemImpl;

	class ScriptSceneImpl : public ScriptScene
	{
		public:
			typedef void (*InitFunction)(ScriptScene*);
			typedef void (*UpdateFunction)(float);

		public:
			ScriptSceneImpl(ScriptSystemImpl& system, Engine& engine, Universe& universe)
				: m_universe(universe)
				, m_engine(engine)
				, m_system(system)
				, m_allocator(engine.getAllocator())
				, m_paths(m_allocator)
				, m_script_entities(m_allocator)
				, m_script_renamed(m_allocator)
				, m_module(NULL)
			{
			}


			~ScriptSceneImpl()
			{
			}


			virtual bool ownComponentType(uint32_t type) const override
			{
				return type == SCRIPT_HASH;
			}


			virtual IPlugin& getPlugin() const;


			void deserialize(InputBlob& serializer) override
			{
				int32_t count;
				serializer.read(count);
				m_script_entities.resize(count);
				m_paths.clear();
				m_paths.reserve(count);
				for (int i = 0; i < m_script_entities.size(); ++i)
				{
					serializer.read(m_script_entities[i]);
					char path[LUMIX_MAX_PATH];
					serializer.readString(path, sizeof(path));
					m_paths.push(Path(path));
					Entity entity(&m_universe, m_script_entities[i]);
					if(m_script_entities[i] != -1)
					{
						m_universe.addComponent(entity, SCRIPT_HASH, this, i);
					}
				}
			}


			void update(float time_delta) override
			{
				if (!m_module)
				{
					TODO("some init function");
					const char* library_path = "scripts/Debug/main.dll";
					m_module = LoadLibrary(library_path);
					if (!m_module)
					{
						g_log_error.log("script") << "Could not load " << library_path;
						return;
					}
					m_update_function = (UpdateFunction)GetProcAddress(m_module, "update");
					InitFunction init_function = (InitFunction)GetProcAddress(m_module, "init");
					if (!m_update_function)
					{
						g_log_error.log("script") << "Could not find function update in " << library_path;
					}
					if (!init_function)
					{
						g_log_error.log("script") << "Could not find function init in " << library_path;
					}
					if (init_function)
					{
						init_function(this);
					}
				}
				if (m_update_function)
				{
					m_update_function(time_delta);
				}
			}


			virtual const Lumix::Path& getScriptPath(Component cmp) override
			{
				return m_paths[cmp.index];
			}

			
			void getScriptPath(Component cmp, string& str) override
			{
				str = m_paths[cmp.index];
			}


			virtual DelegateList<void(const Path&, const Path&)>& scriptRenamed() override
			{
				return m_script_renamed;
			}


			void setScriptPath(Component cmp, const string& str) override
			{
				Lumix::Path old_path = m_paths[cmp.index];
				m_paths[cmp.index] = str.c_str();
				m_script_renamed.invoke(old_path, m_paths[cmp.index]);
			}


			virtual Component getNextScript(const Component& cmp) override
			{
				for (int i = cmp.index + 1; i < m_script_entities.size(); ++i)
				{
					if (m_script_entities[i] != -1)
					{
						return Component(Entity(&m_universe, m_script_entities[i]), SCRIPT_HASH, this, i);
					}
				}
				return Component::INVALID;
			}
			

			virtual Component getFirstScript() override
			{
				for (int i = 0; i < m_script_entities.size(); ++i)
				{
					if (m_script_entities[i] != -1)
					{
						return Component(Entity(&m_universe, m_script_entities[i]), SCRIPT_HASH, this, i);
					}
				}
				return Component::INVALID;
			}

			
			void getScriptDefaultPath(Entity e, char* path, char* full_path, int length, const char* ext)
			{
				char tmp[30];
				toCString(e.index, tmp, 30);

				copyString(full_path, length, m_engine.getBasePath());
				catCString(full_path, length, "e");
				catCString(full_path, length, tmp);
				catCString(full_path, length, ".");
				catCString(full_path, length, ext);

				copyString(path, length, "e");
				catCString(path, length, tmp);
				catCString(path, length, ".");
				catCString(path, length, ext);
			}


			virtual void beforeScriptCompiled() override
			{
			}


			virtual void afterScriptCompiled() override
			{
			}


			Component createScript(Entity entity)
			{
				char path[LUMIX_MAX_PATH];
				char full_path[LUMIX_MAX_PATH];
				getScriptDefaultPath(entity, path, full_path, LUMIX_MAX_PATH, "cpp");

				m_script_entities.push(entity.index);
				m_paths.push(Path(path));

				Component cmp = m_universe.addComponent(entity, SCRIPT_HASH, this, m_script_entities.size() - 1);
				m_universe.componentCreated().invoke(cmp);

				return cmp;
			}


		
			void serialize(OutputBlob& serializer) override
			{
				serializer.write((int32_t)m_script_entities.size());
				for (int i = 0; i < m_script_entities.size(); ++i)
				{
					serializer.write((int32_t)m_script_entities[i]);
					serializer.writeString(m_paths[i].c_str());
				}
			}


			virtual Component createComponent(uint32_t type, const Entity& entity)
			{
				if (type == SCRIPT_HASH)
				{
					return createScript(entity);
				}
				return Component::INVALID;
			}


			virtual void destroyComponent(const Component& cmp)
			{
				m_script_entities[cmp.index] = -1;
				m_universe.destroyComponent(cmp);
			}


			virtual Engine& getEngine() override
			{
				return m_engine;
			}

			IAllocator& m_allocator;
			Array<int32_t> m_script_entities;
			Array<Path> m_paths;
			Universe& m_universe;
			Engine& m_engine;
			ScriptSystemImpl& m_system;
			HMODULE m_module;
			UpdateFunction m_update_function;
			DelegateList<void(const Path&, const Path&)> m_script_renamed;
	};

	class ScriptSystemImpl : public IPlugin
	{
		public:
			Engine& m_engine;
			BaseProxyAllocator m_allocator;


			ScriptSystemImpl(Engine& engine)
				: m_engine(engine)
				, m_allocator(engine.getAllocator())
			{
			}


			virtual IScene* createScene(Universe& universe) override
			{
				return m_allocator.newObject<ScriptSceneImpl>(*this, m_engine, universe);
			}


			virtual void destroyScene(IScene* scene) override
			{
				m_allocator.deleteObject(scene);
			}


			virtual bool create() override
			{
				if (m_engine.getWorldEditor())
				{
					IAllocator& allocator = m_engine.getWorldEditor()->getAllocator();
					m_engine.getWorldEditor()->registerProperty("script", allocator.newObject<FilePropertyDescriptor<ScriptSceneImpl> >("source", (void (ScriptSceneImpl::*)(Component, string&))&ScriptSceneImpl::getScriptPath, &ScriptSceneImpl::setScriptPath, "Script (*.cpp)", allocator));
				}
				return true;
			}


			virtual void destroy() override
			{
			}


			virtual const char* getName() const override
			{
				return "script";
			}

	}; // ScriptSystemImpl


	IPlugin& ScriptSceneImpl::getPlugin() const
	{
		return m_system;
	}
	

	extern "C" IPlugin* createPlugin(Engine& engine)
	{
		return engine.getAllocator().newObject<ScriptSystemImpl>(engine);
	}

} // ~namespace Lumix

