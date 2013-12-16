#include "atlas.h"
#include "core/crc32.h"
#include "core/map.h"


namespace Lux
{

	namespace UI
	{

		struct AtlasImpl
		{
			map<uint32_t, Atlas::Part*> m_parts;
		};
		

		bool Atlas::create()
		{
			m_impl = new AtlasImpl();
			return m_impl != 0;
		}


		void Atlas::destroy()
		{
			delete m_impl;
			m_impl = 0;
		}


		void Atlas::load(Lux::FS::FileSystem& file_system, const char* filename)
		{

		}


		const Atlas::Part* Atlas::getPart(const char* name)
		{
			Part* part = NULL;
			m_impl->m_parts.find(crc32(name), part);
			return part;
		}


	} // ~namespace Lux

} // ~namespace Lux