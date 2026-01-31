#pragma once


#include "core/hash_map.h"
#include "core/math.h"
#include "core/stream.h"
#include "core/tag_allocator.h"
#include "engine/resource.h"
#include "engine/resource_manager.h"


namespace black
{


struct Font;
struct Renderer;
struct Texture;


struct Glyph {
	u32 codepoint;
	float u0, v0, u1, v1;
	float x0, y0, x1, y1;
	float advance_x;
};


BLACK_RENDERER_API Vec2 measureTextA(const Font& font, const char* str, const char* str_end);
BLACK_RENDERER_API const Glyph* findGlyph(const Font& font, u32 codepoint);
BLACK_RENDERER_API float getAdvanceY(const Font& font);
BLACK_RENDERER_API float getDescender(const Font& font);
BLACK_RENDERER_API float getAscender(const Font& font);


struct BLACK_RENDERER_API FontResource final : Resource
{
	FontResource(const Path& path, ResourceManager& manager, IAllocator& allocator);

	ResourceType getType() const override { return TYPE; }

	void unload() override { m_file_data.free(); }
	bool load(Span<const u8> mem) override;
	Font* addRef(int font_size);
	void removeRef(Font& font);

	TagAllocator m_allocator;
	OutputMemoryStream m_file_data;

	static const ResourceType TYPE;
};


struct BLACK_RENDERER_API FontManager final : ResourceManager
{
friend struct FontResource;
public:
	FontManager(Renderer& renderer, IAllocator& allocator);
	~FontManager();

	Texture* getAtlasTexture();

private:
	Resource* createResource(const Path& path) override;
	void destroyResource(Resource& resource) override;
	bool build();

private:
	TagAllocator m_allocator;
	Renderer& m_renderer;
	Texture* m_atlas_texture;
	Array<Font*> m_fonts;
	bool m_dirty = true;
};


} // namespace black