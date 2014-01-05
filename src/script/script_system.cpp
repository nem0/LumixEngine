#include "script_system.h"
#include <Windows.h>
#include <cstdio>
#include "core/crc32.h"
#include "core/file_system.h"
#include "core/ifile.h"
#include "core/json_serializer.h"
#include "core/log.h"
#include "core/path.h"
#include "core/pod_array.h"
#include "engine/engine.h"
#include "universe/universe.h"
#include "universe/component_event.h"
#include "base_script.h"
#include "save_script_visitor.h"


static const uint32_t script_type = crc32("script");


namespace Lux
{
	struct ScriptSystemImpl
	{
		void postDeserialize();
		void compile();
		void getDll(const FS::Path& path, char* dll_path, int max_length);
		void getScriptDefaultPath(Entity e, char* path, int max_path, const char* ext);

		PODArray<int> m_scripts;
		PODArray<BaseScript*> m_script_objs;
		PODArray<HMODULE> m_libs;
		Array<FS::Path> m_paths;
		Universe* m_universe;
		Engine* m_engine;
		bool m_is_running;
		ScriptSystem* m_owner;
	};


	typedef BaseScript* (*CreateScriptFunction)();
	typedef void (*DestroyScriptFunction)(BaseScript* script);


	ScriptSystem::ScriptSystem()
	{
		m_impl = LUX_NEW(ScriptSystemImpl);
		m_impl->m_owner = this;
		m_impl->m_is_running = false;
	}


	ScriptSystem::~ScriptSystem()
	{
		LUX_DELETE(m_impl);
	}


	void ScriptSystem::setEngine(Engine& engine)
	{
		m_impl->m_engine = &engine;
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
			m_impl->getDll(m_impl->m_paths[i], path, MAX_PATH);
			HMODULE h = LoadLibrary(path);
			m_impl->m_libs.push(h);
			if(h)
			{
				CreateScriptFunction f = (CreateScriptFunction)GetProcAddress(h, TEXT("createScript"));
				BaseScript* script = f();
				if(!f)
				{
					g_log_warning.log("script", "failed to create script %s", m_impl->m_paths[i].getCString());
					m_impl->m_script_objs.push(0);
				}
				else
				{
					m_impl->m_script_objs.push(script);
					script->create(*this, e);
				}
			}
			else
			{
				g_log_warning.log("script", "failed to load script %s", m_impl->m_paths[i].getCString());
				m_impl->m_script_objs.push(0);
			}
		}
		m_impl->m_is_running = true;
	}

	void ScriptSystem::deserialize(ISerializer& serializer)
	{
		int count;
		serializer.deserialize("count", count);
		m_impl->m_scripts.resize(count);
		m_impl->m_paths.clear();
		m_impl->m_paths.reserve(count);
		serializer.deserializeArrayBegin("scripts");
		for(int i = 0; i < m_impl->m_scripts.size(); ++i)
		{
			serializer.deserializeArrayItem(m_impl->m_scripts[i]);
			char tmp[FS::Path::MAX_LENGTH];
			serializer.deserializeArrayItem(tmp, FS::Path::MAX_LENGTH);
			m_impl->m_paths.push(FS::Path(tmp, m_impl->m_engine->getFileSystem()));
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
			serializer.serializeArrayItem(m_impl->m_paths[i].getCString());
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
		str = m_impl->m_paths[cmp.index].getCString();
	}

	void ScriptSystem::setScriptPath(Component cmp, const string& str)
	{
		m_impl->m_paths[cmp.index] = FS::Path(str.c_str(), m_impl->m_engine->getFileSystem());
	}
	
	void ScriptSystemImpl::getDll(const FS::Path& script_path, char* dll_path, int max_length)
	{
		strcpy_s(dll_path, max_length, script_path.getCString());
		int len = strlen(script_path.getCString());
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

		FS::FileSystem& fs = m_impl->m_engine->getFileSystem();
		FS::IFile* file = fs.open(fs.getDefaultDevice(), FS::Path(path, m_impl->m_engine->getFileSystem()), FS::Mode::OPEN_OR_CREATE);
		fs.close(file);

		m_impl->m_scripts.push(entity.index);
		m_impl->m_paths.push(FS::Path(path, m_impl->m_engine->getFileSystem()));

		Component cmp(entity, script_type, this, m_impl->m_scripts.size() - 1);
		m_impl->m_universe->getEventManager()->emitEvent(ComponentEvent(cmp));

		return cmp;
	}

} // ~namespace Lux