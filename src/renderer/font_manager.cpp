#include "font_manager.h"
#include "renderer/texture.h"
#include "renderer/texture_manager.h"
#include "renderer/renderer.h"


namespace Lumix
{


const ResourceType FontResource::TYPE("font");


FontResource::FontResource(const Path& path, ResourceManagerBase& manager, IAllocator& allocator)
	: Resource(path, manager, allocator)
	, m_fonts(allocator)
	, m_file_data(allocator)
{
}


bool FontResource::load(FS::IFile& file)
{
	int size = (int)file.size();
	if (size <= 0) return false;
	
	m_file_data.resize(size);
	file.read(&m_file_data[0], m_file_data.size());
	return true;
}


Font* FontResource::addRef(int font_size)
{
	auto iter = m_fonts.find(font_size);
	if (iter.isValid())
	{
		++iter.value().ref_count;
		return iter.value().font;
	}

	auto& manager = (FontManager&)m_resource_manager;
	FontRef ref;
	ref.ref_count = 1;
	FontConfig cfg;
	cfg.FontDataOwnedByAtlas = false;
	ref.font = manager.m_font_atlas.AddFontFromMemoryTTF(&m_file_data[0], m_file_data.size(), (float)font_size, &cfg);
	manager.updateFontTexture();
	m_fonts.insert(font_size, ref);
	return ref.font;
}


void FontResource::removeRef(Font& font)
{
	auto iter = m_fonts.find((int)(font.FontSize + 0.5f));
	ASSERT(iter.isValid());
	--iter.value().ref_count;
	ASSERT(iter.value().ref_count >= 0);
}


FontManager::FontManager(Renderer& renderer, IAllocator& allocator)
	: ResourceManagerBase(allocator)
	, m_allocator(allocator)
	, m_renderer(renderer)
	, m_font_atlas(allocator)
	, m_atlas_texture(nullptr)
	, m_atlas_texture_changed(allocator)
{
	m_default_font = m_font_atlas.AddFontDefault();
	updateFontTexture();
}


FontManager::~FontManager()
{
	if (m_atlas_texture)
	{
		m_atlas_texture->destroy();
		LUMIX_DELETE(m_allocator, m_atlas_texture);
	}
}


void FontManager::updateFontTexture()
{
	u8* pixels;
	int w, h;
	m_font_atlas.GetTexDataAsRGBA32(&pixels, &w, &h);

	if (m_atlas_texture)
	{
		m_atlas_texture->destroy();
	}
	else
	{
		auto& texture_manager = m_renderer.getTextureManager();
		m_atlas_texture = LUMIX_NEW(m_allocator, Texture)(Path("draw2d_atlas"), texture_manager, m_allocator);
	}
	m_atlas_texture->create(w, h, pixels);

	m_font_atlas.TexID = &m_atlas_texture->handle;
	m_atlas_texture_changed.invoke();
}


Resource* FontManager::createResource(const Path& path)
{
	return LUMIX_NEW(m_allocator, FontResource)(path, *this, m_allocator);
}


void FontManager::destroyResource(Resource& resource)
{
	LUMIX_DELETE(m_allocator, static_cast<FontResource*>(&resource));
}


} // namespace Lumix