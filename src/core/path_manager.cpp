#include "core/path_manager.h"
#include "core/mutex.h"
#include "core/map.h"
#include "core/path.h"


namespace Lux
{
namespace FS
{


	struct PathManagerImpl : public PathManager
	{
		PathManagerImpl()
		{
			m_mutex = MT::Mutex::create(false);
		}

		~PathManagerImpl()
		{
			MT::Mutex::destroy(m_mutex);
		}


		virtual PathString* addReference(const char* path, uint32_t hash) LUX_OVERRIDE
		{
			MT::Lock lock(*m_mutex);
			map<uint32_t, PathString>::iterator iter = m_strings.find(hash);
			if(iter != m_strings.end())
			{
				++iter.second().m_references;
				return &iter.second();
			}
			PathString& str = m_strings[hash];
			str.m_hash = hash;
			str.m_path = LUX_NEW_ARRAY(char, strlen(path) + 1);
			strcpy(str.m_path, path);
			str.m_references = 1;
			return &str;
		}


		virtual void removeReference(uint32_t hash) LUX_OVERRIDE
		{
			MT::Lock lock(*m_mutex);
			PathString& str = m_strings[hash];
			--str.m_references;
			if(str.m_references == 0)
			{
				LUX_DELETE_ARRAY(str.m_path);
				m_strings.erase(hash);
			}
		}

		map<uint32_t, PathString> m_strings;
		MT::Mutex* m_mutex;
	};


PathManager* PathManager::create()
{
	return LUX_NEW(PathManagerImpl);
}


void PathManager::destroy(PathManager& manager)
{
	LUX_DELETE(&manager);
}


} // ~namespace Path
} // ~namspace Lux
