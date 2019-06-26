#pragma once


#include "engine/hash_map.h"
#include "engine/delegate_list.h"
#include "engine/resource.h"
#include "engine/resource_manager.h"
#include "renderer/draw2d.h"


namespace Lumix
{


class Renderer;
class Texture;


class LUMIX_RENDERER_API FontResource final : public Resource
{
public:
	FontResource(const Path& path, ResourceManager& manager, IAllocator& allocator);

	ResourceType getType() const override { return TYPE; }

	void unload() override { m_file_data.free(); }
	bool load(u64 size, const u8* mem) override;
	Font* addRef(int font_size);
	void removeRef(Font& font);

	static const ResourceType TYPE;

private:
	struct FontRef
	{
		Font* font;
		int ref_count;
	};

	HashMap<int, FontRef> m_fonts;
	Array<u8> m_file_data;
};


class LUMIX_RENDERER_API FontManager final : public ResourceManager
{
friend class FontResource;
public:
	FontManager(Renderer& renderer, IAllocator& allocator);
	~FontManager();

	FontAtlas& getFontAtlas() { return m_font_atlas; }
	Font* getDefaultFont() const { return m_default_font; }
	Texture* getAtlasTexture() const { return m_atlas_texture; }
	DelegateList<void()>& onAtlasTextureChanged() { return m_atlas_texture_changed; }

private:
	Resource* createResource(const Path& path) override;
	void destroyResource(Resource& resource) override;
	void updateFontTexture();

private:
	IAllocator& m_allocator;
	Renderer& m_renderer;
	FontAtlas m_font_atlas;
	Font* m_default_font;
	Texture* m_atlas_texture;
	DelegateList<void()> m_atlas_texture_changed;
};


} // namespace Lumix