#include "script_system.h"
#include <Windows.h>
#include "core/crc32.h"
#include "core/fs/file_system.h"
#include "core/fs/ifile.h"
#include "core/json_serializer.h"
#include "core/log.h"
#include "core/array.h"
#include "editor/world_editor.h"
#include "engine/engine.h"
#include "universe/universe.h"
#include "base_script.h"
#include "save_script_visitor.h"


static const uint32_t SCRIPT_HASH = crc32("script");


namespace Lumix
{
	typedef BaseScript* (*CreateScriptFunction)();
	typedef void (*DestroyScriptFunction)(BaseScript* script);
	
	struct ScriptSystemImpl : public ScriptSystem
	{
		struct RunningScript
		{
			BaseScript* m_script_object;
			HMODULE m_lib;
			int m_entity_idx;
		};

		Array<RunningScript> m_running_scripts;
		Array<int> m_script_entities;
		Array<string> m_paths;
		Universe* m_universe;
		Engine* m_engine;


		ScriptSystemImpl()
		{
		}


		Universe* getUniverse() const override
		{
			return m_universe;
		}


		Engine* getEngine() const override
		{
			return m_engine;
		}


		void runAll()
		{
			char path[MAX_PATH];
			char full_path[MAX_PATH];
			for(int i = 0; i < m_script_entities.size(); ++i)
			{
				Entity e(m_universe, m_script_entities[i]);
				getDll(m_paths[i].c_str(), path, full_path, MAX_PATH);
				HMODULE h = LoadLibrary(full_path);
				if(h)
				{
					CreateScriptFunction f = (CreateScriptFunction)GetProcAddress(h, TEXT("createScript"));
					BaseScript* script = f();
					if(!f)
					{
						g_log_warning.log("script") << "failed to create script " << m_paths[i].c_str();
						FreeLibrary(h);
					}
					else
					{
						RunningScript running_script;
						running_script.m_entity_idx = e.index;
						running_script.m_lib = h;
						running_script.m_script_object = script;
						m_running_scripts.push(running_script);
						script->create(*this, e);
					}
				}
				else
				{
					g_log_warning.log("script") << "failed to load script " << m_paths[i].c_str();
				}
			}
		}


		void deserialize(ISerializer& serializer) override
		{
			stopAll();
			int count;
			serializer.deserialize("count", count);
			serializer.deserializeArrayBegin("scripts");
			m_script_entities.resize(count);
			m_paths.resize(count);
			for (int i = 0; i < m_script_entities.size(); ++i)
			{
				serializer.deserializeArrayItem(m_script_entities[i]);
				serializer.deserializeArrayItem(m_paths[i]);
				Entity entity(m_universe, m_script_entities[i]);
				m_universe->addComponent(entity, SCRIPT_HASH, this, i);
			}
			serializer.deserializeArrayEnd();
			runAll();
		}


		void serialize(ISerializer& serializer) override
		{
			serializer.serialize("count", m_script_entities.size());
			serializer.beginArray("scripts");
			for (int i = 0; i < m_script_entities.size(); ++i)
			{
				serializer.serializeArrayItem(m_script_entities[i]);
				serializer.serializeArrayItem(m_paths[i]);
			}
			serializer.endArray();
		}


		void stopAll()
		{
			for(int i = 0; i < m_running_scripts.size(); ++i)
			{
				DestroyScriptFunction f = (DestroyScriptFunction)GetProcAddress(m_running_scripts[i].m_lib, "destroyScript");
				f(m_running_scripts[i].m_script_object);
				BOOL b = FreeLibrary(m_running_scripts[i].m_lib);
				ASSERT(b == TRUE);
			}
			m_running_scripts.clear();
		}


		void update(float dt) override
		{
			for(int i = 0; i < m_running_scripts.size(); ++i)
			{
				m_running_scripts[i].m_script_object->update(dt);
			}
		}


		void getScriptPath(Component cmp, string& str) override
		{
			str = m_paths[cmp.index];
		}


		void setScriptPath(Component cmp, const string& str) override
		{
			m_paths[cmp.index] = str;
			stopScript(cmp.index);
			if (!runScript(cmp.index, cmp.entity))
			{
				g_log_warning.log("script") << "Could not run script " << str;
			}
		}
	

		void getDll(const char* script_path, char* dll_path, char* full_path, int max_length)
		{
			copyString(dll_path, max_length, script_path);
			int32_t len = (int32_t)strlen(script_path);
			if(len > 4)
			{
				copyString(dll_path + len - 4, 5, ".dll");
			}
			copyString(full_path, max_length, m_engine->getBasePath());
			catCString(full_path, max_length, "\\");
			catCString(full_path, max_length, dll_path);
		}


		void getScriptDefaultPath(Entity e, char* path, char* full_path, int length, const char* ext)
		{
			char tmp[30];
			toCString(e.index, tmp, 30);

			copyString(full_path, length, m_engine->getBasePath());
			catCString(full_path, length, "\\scripts\\e");
			catCString(full_path, length, tmp);
			catCString(full_path, length, ".");
			catCString(full_path, length, ext);

			copyString(path, length, "scripts\\e");
			catCString(path, length, tmp);
			catCString(path, length, ".");
			catCString(path, length, ext);
		}


		void stopScript(int index)
		{
			int entity_idx = m_script_entities[index];
			for (int i = 0; i < m_running_scripts.size(); ++i)
			{
				if (m_running_scripts[i].m_entity_idx == entity_idx)
				{
					DestroyScriptFunction f = (DestroyScriptFunction)GetProcAddress(m_running_scripts[i].m_lib, "destroyScript");
					f(m_running_scripts[i].m_script_object);
					BOOL b = FreeLibrary(m_running_scripts[i].m_lib);
					ASSERT(b == TRUE);
					m_running_scripts.eraseFast(i);
					return;
				}
			}
		}


		bool runScript(int i, Entity entity)
		{
			char path[LUMIX_MAX_PATH];
			char full_path[LUMIX_MAX_PATH];
			getDll(m_paths[i].c_str(), path, full_path, LUMIX_MAX_PATH);
			HMODULE h = LoadLibrary(full_path);
			if (h)
			{
				CreateScriptFunction f = (CreateScriptFunction)GetProcAddress(h, TEXT("createScript"));
				BaseScript* script = f();
				if (!f)
				{
					g_log_warning.log("script") << "failed to create script " << m_paths[i].c_str();
					FreeLibrary(h);
				}
				else
				{
					RunningScript running_script;
					running_script.m_script_object = script;
					running_script.m_entity_idx = entity.index;
					running_script.m_lib = h;
					m_running_scripts.push(running_script);
					script->create(*this, entity);
					return true;
				}
			}
			return false;
		}


		Component createScript(Entity entity) override
		{
			char path[LUMIX_MAX_PATH];
			char full_path[LUMIX_MAX_PATH];
			getScriptDefaultPath(entity, path, full_path, LUMIX_MAX_PATH, "cpp");

			m_script_entities.push(entity.index);
			m_paths.push(string(path));

			Component cmp = m_universe->addComponent(entity, SCRIPT_HASH, this, m_script_entities.size() - 1);
			m_universe->componentCreated().invoke(cmp);

			runScript(m_paths.size() - 1, entity);

			return cmp;
		}


		virtual bool create(Engine& engine) override
		{
			m_engine = &engine;
			engine.getWorldEditor()->registerProperty("script", LUMIX_NEW(PropertyDescriptor<ScriptSystem>)(crc32("source"), &ScriptSystem::getScriptPath, &ScriptSystem::setScriptPath, IPropertyDescriptor::FILE)); 
			engine.getWorldEditor()->registerCreator(SCRIPT_HASH, *this);
			return true;
		}


		virtual void destroy() override
		{
			stopAll();
		}


		virtual Component createComponent(uint32_t type, const Entity& entity) override
		{
			if (type == SCRIPT_HASH)
			{
				return createScript(entity);
			}
			return Component::INVALID;
		}


		virtual const char* getName() const override
		{
			return "script";
		}


		virtual void onCreateUniverse(Universe& universe) override
		{
			m_universe = &universe;
		}


		virtual void onDestroyUniverse(Universe& universe) override
		{
			m_universe = NULL;
		}


	}; // ScriptSystemImpl


	extern "C" IPlugin* createPlugin()
	{
		return LUMIX_NEW(ScriptSystemImpl)();
	}

} // ~namespace Lumix

