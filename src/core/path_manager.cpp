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
			ASSERT(!m_is_created); // singleton
			m_mutex = MT::Mutex::create(false);
			m_is_created = true;
		}

		~PathManagerImpl()
		{
			MT::Mutex::destroy(m_mutex);
			m_is_created = false;
		}

		virtual PathString* addReference(PathString& path_string) LUX_OVERRIDE
		{
			MT::Lock lock(*m_mutex);
			++path_string.m_references;
			return &path_string;
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
			str.m_path_manager = this;
			str.m_hash = hash;
			str.m_path = LUX_NEW_ARRAY(char, strlen(path) + 1);
			strcpy(str.m_path, path);
			str.m_references = 1;
			return &str;
		}


		virtual void removeReference(PathString& path_string) LUX_OVERRIDE
		{
			MT::Lock lock(*m_mutex);
			--path_string.m_references;
			if(path_string.m_references == 0)
			{
				LUX_DELETE_ARRAY(path_string.m_path);
				m_strings.erase(path_string.m_hash);
			}
		}

		map<uint32_t, PathString> m_strings;
		MT::Mutex* m_mutex;
		static bool m_is_created;
	};

	bool PathManagerImpl::m_is_created = false;


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
