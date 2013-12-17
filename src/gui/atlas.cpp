#include "atlas.h"
#include <cstring>
#include "core/crc32.h"
#include "core/file_system.h"
#include "core/map.h"
#include "gui/irenderer.h"


namespace Lux
{

	namespace UI
	{

		struct AtlasImpl
		{
			map<uint32_t, Atlas::Part*> m_parts;
			TextureBase* m_texture;
			string m_path;
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


		void atlasLoaded(Lux::FS::IFile* file, bool success, void* user_data)
		{
		}


		void Atlas::load(IRenderer& renderer, Lux::FS::FileSystem& file_system, const char* filename)
		{
			m_impl->m_path = filename;
			file_system.openAsync(file_system.getDefaultDevice(), filename, Lux::FS::Mode::OPEN | Lux::FS::Mode::READ, &atlasLoaded, this);
			char image_path[260]; // TODO MAX_PATH
			strcpy_s(image_path, filename);
			size_t size = strlen(filename);
			strcpy_s(image_path + size, 260 - size, ".tga");
			m_impl->m_texture = renderer.loadImage(filename);
		}


		TextureBase* Atlas::getTexture() const
		{
			return m_impl->m_texture;
		}


		const string& Atlas::getPath() const
		{
			return m_impl->m_path;
		}


		const Atlas::Part* Atlas::getPart(const char* name)
		{
			Part* part = NULL;
			m_impl->m_parts.find(crc32(name), part);
			return part;
		}


	} // ~namespace Lux

} // ~namespace Lux