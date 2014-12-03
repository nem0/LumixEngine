#include "bitmap_font.h"
#include "core/fs/file_system.h"
#include "core/json_serializer.h"
#include "core/resource_manager.h"
#include "core/resource_manager_base.h"
#include "material.h"
#include "texture.h"


namespace Lumix
{


	Resource* BitmapFontManager::createResource(const Path& path)
	{
		return m_allocator.newObject<BitmapFont>(path, getOwner(), m_allocator);
	}


	void BitmapFontManager::destroyResource(Resource& resource)
	{
		m_allocator.deleteObject(static_cast<BitmapFont*>(&resource));
	}

	
	BitmapFont::BitmapFont(const Path& path, ResourceManager& resource_manager, IAllocator& allocator)
		: Resource(path, resource_manager, allocator)
		, m_allocator(allocator)
		, m_characters(allocator)
	{

	}


	void BitmapFont::doUnload(void)
	{
		m_material->getObserverCb().unbind<BitmapFont, &BitmapFont::materialLoaded>(this);
		removeDependency(*m_material);
		onEmpty();
	}


	const BitmapFont::Character* BitmapFont::getCharacter(char character) const
	{
		int index = m_characters.find(character);
		if (index < 0)
		{
			return NULL;
		}
		return &m_characters.at(index);
	}


	static bool readLine(FS::IFile* file, char buffer[], int max_size)
	{
		int i = 0;
		while (file->read(buffer + i, 1) && buffer[i] != '\n' && buffer[i] != '\0' && i < max_size - 1)
		{
			++i;
		}
		buffer[i + 1] = 0;
		return buffer[i] == '\n' || buffer[i] == '\0' || i == max_size - 1;
	}


	static const char* getFirstNumberPos(const char* str)
	{
		const char* c = str;
		while (*c != 0 && (*c < '0' || *c > '9'))
		{
			++c;
		}
		return c;
	}


	static const char* getNextNumberPos(const char* str)
	{
		const char* c = str;
		while (*c != 0 && *c >= '0' && *c <= '9')
		{
			++c;
		}
		return getFirstNumberPos(c);
	}


	void BitmapFont::materialLoaded(Resource::State, Resource::State new_state)
	{
		if (new_state == Resource::State::READY)
		{
			for (int i = 0; i < m_characters.size(); ++i)
			{
				Character& character = m_characters.at(i);
				character.m_left = character.m_left_px / m_material->getTexture(0)->getWidth();
				character.m_right = (character.m_left_px + character.m_pixel_w) / m_material->getTexture(0)->getWidth();
				character.m_top = character.m_top_px / m_material->getTexture(0)->getHeight();
				character.m_bottom = (character.m_top_px + character.m_pixel_h) / m_material->getTexture(0)->getHeight();
			}
		}
	}


	void BitmapFont::loaded(FS::IFile* file, bool success, FS::FileSystem& fs)
	{
		if (success)
		{
			char line[255];
			readLine(file, line, sizeof(line));
			int i = 0;
			while (line[i] != 0 && line[i] != '\n' && line[i] != '\r')
			{
				++i;
			}
			line[i] = 0;
			m_material = static_cast<Material*>(getResourceManager().get(ResourceManager::MATERIAL)->load(Path(line)));
			addDependency(*m_material);
			m_material->onLoaded<BitmapFont, &BitmapFont::materialLoaded>(this);

			while (readLine(file, line, sizeof(line)) && strncmp(line, "chars count", 11) != 0);
			if (strncmp(line, "chars count", 11) == 0)
			{
				int count;
				const char* c = getFirstNumberPos(line);
				fromCString(c, sizeof(line) - (c - line), &count);
				for (int i = 0; i < count; ++i)
				{
					readLine(file, line, sizeof(line));
					c = getFirstNumberPos(line);
					int id;
					fromCString(c, sizeof(line) - (c - line), &id);
					Character character;
					int tmp;
					c = getNextNumberPos(c);
					fromCString(c, sizeof(line) - (c - line), &tmp);
					character.m_left_px = (float)tmp;
					c = getNextNumberPos(c);
					fromCString(c, sizeof(line) - (c - line), &tmp);
					character.m_top_px = (float)tmp;
					c = getNextNumberPos(c);
					fromCString(c, sizeof(line) - (c - line), &tmp);
					character.m_pixel_w = (float)tmp;
					c = getNextNumberPos(c);
					fromCString(c, sizeof(line) - (c - line), &tmp);
					character.m_pixel_h = (float)tmp;
					c = getNextNumberPos(c);
					fromCString(c, sizeof(line) - (c - line), &tmp);
					character.m_x_offset = (float)tmp;
					c = getNextNumberPos(c);
					fromCString(c, sizeof(line) - (c - line), &tmp);
					character.m_y_offset = (float)tmp;
					c = getNextNumberPos(c);
					fromCString(c, sizeof(line) - (c - line), &tmp);
					character.m_x_advance = (float)tmp;
					m_characters.insert((char)id, character);
				}
				decrementDepCount();
			}
			else
			{
				g_log_error.log("renderer") << m_path.c_str() << " has invalid format.";
				onFailure();
			}
		}
		else
		{
			g_log_error.log("renderer") << "Could not load bitmap font " << m_path.c_str();
			onFailure();
		}
		fs.close(file);
	}


	FS::ReadCallback BitmapFont::getReadCallback(void)
	{
		FS::ReadCallback cb;
		cb.bind<BitmapFont, &BitmapFont::loaded>(this);
		return cb;
	}



} // namespace Lumix