#include "atlas.h"
#include <cstring>
#include "core/crc32.h"
#include "core/fs/file_system.h"
#include "core/fs/ifile.h"
#include "core/json_serializer.h"
#include "core/map.h"
#include "gui/irenderer.h"
#include "gui/texture_base.h"

namespace Lux
{
	namespace UI
	{
		struct AtlasImpl
		{
			void imageLoaded(TextureBase& img);
			void atlasLoaded(Lux::FS::IFile* file, bool success, FS::FileSystem& fs);

			map<uint32_t, Atlas::Part*> m_parts;
			TextureBase* m_texture;
			string m_path;
			IRenderer* m_renderer;
			Lux::FS::FileSystem* m_filesystem;
		};
		
		void AtlasImpl::atlasLoaded(Lux::FS::IFile* file, bool success, FS::FileSystem& fs)
		{
			if(success)
			{
				JsonSerializer serializer(*file, JsonSerializer::READ);
				char tmp[260];
				serializer.deserialize("image", tmp, 260);
				m_texture = m_renderer->loadImage(tmp, *m_filesystem);
				ASSERT(m_texture);
				m_texture->onLoaded().bind<AtlasImpl, &AtlasImpl::imageLoaded>(this);
				int count;
				serializer.deserialize("part_count", count);
				serializer.deserializeArrayBegin("parts");
				for(int i = 0; i < count; ++i)
				{
					serializer.deserializeArrayItem(tmp, 260);
					Atlas::Part* part = LUX_NEW(Atlas::Part)();
					serializer.deserializeArrayItem(part->m_left);
					serializer.deserializeArrayItem(part->m_top);
					serializer.deserializeArrayItem(part->m_right);
					serializer.deserializeArrayItem(part->m_bottom);
					part->m_pixel_width = part->m_right - part->m_left;
					part->m_pixel_height = part->m_bottom - part->m_top;
					part->name = tmp;
					m_parts.insert(crc32(tmp), part);
				}
				serializer.deserializeArrayEnd();
			}

			m_filesystem->close(file);
		}

		void Atlas::Part::getUvs(float* uvs) const
		{
			uvs[0] = m_left;
			uvs[1] = m_top;

			uvs[2] = m_left;
			uvs[3] = m_bottom;

			uvs[4] = m_right;
			uvs[5] = m_bottom;

			uvs[6] = m_left;
			uvs[7] = m_top;

			uvs[8] = m_right;
			uvs[9] = m_bottom;

			uvs[10] = m_right;
			uvs[11] = m_top;
		}

		bool Atlas::create()
		{
			m_impl = LUX_NEW(AtlasImpl)();
			m_impl->m_texture = NULL;
			m_impl->m_renderer = NULL;

			return m_impl != NULL;
		}

		void Atlas::destroy()
		{
			LUX_DELETE(m_impl);
			m_impl = NULL;
		}

		void AtlasImpl::imageLoaded(TextureBase& img)
		{
			for(map<uint32_t, Atlas::Part*>::iterator iter = m_parts.begin(), end = m_parts.end(); iter != end; ++iter)
			{
				iter.second()->m_left /= img.getWidth(); 
				iter.second()->m_right /= img.getWidth(); 
				iter.second()->m_top /= img.getHeight(); 
				iter.second()->m_bottom /= img.getHeight(); 
			}
		}

		void Atlas::load(IRenderer& renderer, Lux::FS::FileSystem& file_system, const char* filename)
		{
			m_impl->m_path = filename;
			m_impl->m_renderer = &renderer;
			m_impl->m_filesystem = &file_system;

			FS::ReadCallback atlas_loaded_cb;
			atlas_loaded_cb.bind<AtlasImpl, &AtlasImpl::atlasLoaded>(m_impl);
			file_system.openAsync(file_system.getDefaultDevice(), filename, Lux::FS::Mode::OPEN | Lux::FS::Mode::READ, atlas_loaded_cb);
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