#include "script_system.h"
#include <Windows.h>
#include "core/crc32.h"
#include "core/fs/file_system.h"
#include "core/fs/ifile.h"
#include "core/json_serializer.h"
#include "core/log.h"
#include "core/array.h"
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
		Array<int> m_scripts;
		Array<BaseScript*> m_script_objs;
		Array<HMODULE> m_libs;
		Array<string> m_paths;
		Universe* m_universe;
		Engine* m_engine;
		bool m_is_running;


		ScriptSystemImpl()
		{
			m_is_running = false;
		}


		void setEngine(Engine& engine) override
		{
			m_engine = &engine;
		}


		Universe* getUniverse() const override
		{
			return m_universe;
		}


		Engine* getEngine() const override
		{
			return m_engine;
		}


		void setUniverse(Universe* universe) override
		{
			m_universe = universe;
		}


		void start() override
		{
			char path[MAX_PATH];
			char full_path[MAX_PATH];
			for(int i = 0; i < m_scripts.size(); ++i)
			{
				Entity e(m_universe, m_scripts[i]);
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
						m_libs.push(h);
						m_script_objs.push(script);
						script->create(*this, e);
					}
				}
				else
				{
					g_log_warning.log("script") << "failed to load script " << m_paths[i].c_str();
				}
			}
			m_is_running = true;
		}

		void deserialize(ISerializer& serializer) override
		{
			int count;
			serializer.deserialize("count", count);
			serializer.deserializeArrayBegin("scripts");
			m_scripts.resize(count);
			m_paths.resize(count);
			for (int i = 0; i < m_scripts.size(); ++i)
			{
				serializer.deserializeArrayItem(m_scripts[i]);
				serializer.deserializeArrayItem(m_paths[i]);
				Entity entity(m_universe, m_scripts[i]);
				m_universe->addComponent(entity, SCRIPT_HASH, this, i);
			}
			serializer.deserializeArrayEnd();		
		}

		void serialize(ISerializer& serializer) override
		{
			serializer.serialize("count", m_scripts.size());
			serializer.beginArray("scripts");
			for(int i = 0; i < m_scripts.size(); ++i)
			{
				serializer.serializeArrayItem(m_scripts[i]);
				serializer.serializeArrayItem(m_paths[i]);
			}
			serializer.endArray();
		}

		void stop() override
		{
			m_is_running = false;
			for(int i = 0; i < m_script_objs.size(); ++i)
			{
				DestroyScriptFunction f = (DestroyScriptFunction)GetProcAddress(m_libs[i], "destroyScript");
				f(m_script_objs[i]);
				BOOL b = FreeLibrary(m_libs[i]);
				ASSERT(b == TRUE);
			}
			m_libs.clear();
			m_script_objs.clear();
		}

		void update(float dt) override
		{
			if(m_is_running)
			{
				for(int i = 0; i < m_script_objs.size(); ++i)
				{
					m_script_objs[i]->update(dt);
				}
			}
		}

		void getScriptPath(Component cmp, string& str) override
		{
			str = m_paths[cmp.index];
		}

		void setScriptPath(Component cmp, const string& str) override
		{
			m_paths[cmp.index] = str;
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

		Component createScript(Entity entity) override
		{
			char path[MAX_PATH];
			char full_path[MAX_PATH];
			getScriptDefaultPath(entity, path, full_path, MAX_PATH, "cpp");

			FS::FileSystem& fs = m_engine->getFileSystem();
			FS::IFile* file = fs.open(fs.getDefaultDevice(), full_path, FS::Mode::OPEN_OR_CREATE | FS::Mode::WRITE);
			if (file)
			{
				fs.close(file);
			}

			m_scripts.push(entity.index);
			m_paths.push(string(path));

			Component cmp = m_universe->addComponent(entity, SCRIPT_HASH, this, m_scripts.size() - 1);
			m_universe->componentCreated().invoke(cmp);
			return cmp;
		}

	}; // ScriptSystemImpl


	ScriptSystem* ScriptSystem::create()
	{
		return LUMIX_NEW(ScriptSystemImpl);
	}


	void ScriptSystem::destroy(ScriptSystem* instance)
	{
		LUMIX_DELETE(instance);
	}




} // ~namespace Lumix
