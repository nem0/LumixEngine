#include "script_system.h"
#include <Windows.h>
#include <cstdio>
#include "core/crc32.h"
#include "universe/universe.h"
#include "universe/component_event.h"
#include "universe/entity_names_map.h"
#include "base_script.h"
#include "save_script_visitor.h"
#include "core/json_serializer.h"
#include "core/log.h"


static const uint32_t script_type = crc32("script");


namespace Lux
{
	struct ScriptSystemImpl
	{
		void postDeserialize();
		void compile();
		void getDll(const char* script_path, char* dll_path, int max_length);
		void getScriptDefaultPath(Entity e, char* path, int max_path, const char* ext);

		vector<int> m_scripts;
		vector<BaseScript*> m_script_objs;
		vector<HMODULE> m_libs;
		vector<string> m_paths;
		Universe* m_universe;
		bool m_is_running;
		Renderer* m_renderer;
		InputSystem* m_input_system;
		EntityNamesMap* m_entity_names_map;
		ScriptSystem* m_owner;
	};


	typedef BaseScript* (*CreateScriptFunction)();
	typedef void (*DestroyScriptFunction)(BaseScript* script);


	ScriptSystem::ScriptSystem()
	{
		m_impl = new ScriptSystemImpl;
		m_impl->m_owner = this;
		m_impl->m_is_running = false;
	}


	ScriptSystem::~ScriptSystem()
	{
		delete m_impl;
	}


	void ScriptSystem::setInputSystem(InputSystem* m_input_system)
	{
		m_impl->m_input_system = m_input_system;
	}


	InputSystem* ScriptSystem::getInputSystem() const
	{
		return m_impl->m_input_system;
	}


	Renderer* ScriptSystem::getRenderer() const
	{
		return m_impl->m_renderer;
	}


	void ScriptSystem::setRenderer(Renderer* renderer)
	{
		m_impl->m_renderer = renderer;
	}

	Entity ScriptSystem::getEntityByName(const char* entity_name) const
	{
		return m_impl->m_entity_names_map->getEntityByName(entity_name);
	}

	void ScriptSystem::setEntityNamesMap(EntityNamesMap* names_map)
	{
		m_impl->m_entity_names_map = names_map;
	}

	Universe* ScriptSystem::getUniverse() const
	{
		return m_impl->m_universe;
	}


	void ScriptSystem::setUniverse(Universe* universe)
	{
		m_impl->m_universe = universe;
	}


	void ScriptSystem::start()
	{
		char path[MAX_PATH];
		for(int i = 0; i < m_impl->m_scripts.size(); ++i)
		{
			Entity e(m_impl->m_universe, m_impl->m_scripts[i]);
			m_impl->getDll(m_impl->m_paths[i].c_str(), path, MAX_PATH);
			HMODULE h = LoadLibrary(path);
			m_impl->m_libs.push_back(h);
			if(h)
			{
				CreateScriptFunction f = (CreateScriptFunction)GetProcAddress(h, TEXT("createScript"));
				BaseScript* script = f();
				m_impl->m_script_objs.push_back(script);
				script->create(*this, e);
			}
			else
			{
				g_log_warning.log("script", "failed to load script %s", m_impl->m_paths[i].c_str());
				m_impl->m_script_objs.push_back(0);
			}
		}
		m_impl->m_is_running = true;
	}

	void ScriptSystem::deserialize(ISerializer& serializer)
	{
		int count;
		serializer.deserialize("count", count);
		m_impl->m_scripts.resize(count);
		m_impl->m_paths.resize(count);
		serializer.deserializeArrayBegin("scripts");
		for(int i = 0; i < m_impl->m_scripts.size(); ++i)
		{
			serializer.deserializeArrayItem(m_impl->m_scripts[i]);
			serializer.deserializeArrayItem(m_impl->m_paths[i]);
		}
		serializer.deserializeArrayEnd();		
		m_impl->postDeserialize();
	}

	void ScriptSystem::serialize(ISerializer& serializer)
	{
		serializer.serialize("count", m_impl->m_scripts.size());
		serializer.beginArray("scripts");
		for(int i = 0; i < m_impl->m_scripts.size(); ++i)
		{
			serializer.serializeArrayItem(m_impl->m_scripts[i]);
			serializer.serializeArrayItem(m_impl->m_paths[i]);
		}
		serializer.endArray();
	}

	void ScriptSystem::stop()
	{
		m_impl->m_is_running = false;
		for(int i = 0; i < m_impl->m_scripts.size(); ++i)
		{
			DestroyScriptFunction f = (DestroyScriptFunction)GetProcAddress(m_impl->m_libs[i], "destroyScript");
			f(m_impl->m_script_objs[i]);
			FreeLibrary(m_impl->m_libs[i]);
		}
		m_impl->m_libs.clear();
		m_impl->m_script_objs.clear();
	}

	void ScriptSystem::update(float dt)
	{
		for(int i = 0; i < m_impl->m_scripts.size(); ++i)
		{
			m_impl->m_script_objs[i]->update(dt);
		}
	}

	void ScriptSystem::getScriptPath(Component cmp, string& str)
	{
		str = m_impl->m_paths[cmp.index];
	}

	void ScriptSystem::setScriptPath(Component cmp, const string& str)
	{
		m_impl->m_paths[cmp.index] = str;
	}
	
	void ScriptSystemImpl::getDll(const char* script_path, char* dll_path, int max_length)
	{
		strcpy_s(dll_path, max_length, script_path);
		int len = strlen(script_path);
		if(len > 4)
		{
			strcpy_s(dll_path + len - 4, 5, ".dll"); 
		}
	}

	void ScriptSystemImpl::getScriptDefaultPath(Entity e, char* path, int max_path, const char* ext)
	{
		sprintf_s(path, max_path, "scripts\\e%d.%s", e.index, ext);
	}

	void ScriptSystemImpl::postDeserialize()
	{
		for(int i = 0; i < m_scripts.size(); ++i)
		{
			Entity e(m_universe, m_scripts[i]);
			m_universe->getEventManager()->emitEvent(ComponentEvent(Component(e, script_type, m_owner, i)));
		}
	}

	Component ScriptSystem::createScript(Entity entity)
	{
		char path[MAX_PATH];
		m_impl->getScriptDefaultPath(entity, path, MAX_PATH, "cpp");
		FILE* fp;
		fopen_s(&fp, path, "r");
		if(!fp)
		{
			fopen_s(&fp, path, "w");
		}
		fclose(fp);
		m_impl->m_scripts.push_back(entity.index);
		m_impl->m_paths.push_back(path);
		Component cmp(entity, script_type, this, m_impl->m_scripts.size() - 1);
		m_impl->m_universe->getEventManager()->emitEvent(ComponentEvent(cmp));
		return cmp;
	}

} // ~namespace Lux