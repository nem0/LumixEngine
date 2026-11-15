#pragma once


#include "core/array.h"
#include "core/hash_map.h"
#include "core/math.h"
#include "core/stream.h"
#include "core/tag_allocator.h"
#include "engine/resource.h"
#include "engine/resource_manager.h"
#include <stb/stb_rect_pack.h>


namespace Lumix
{


struct Font;
struct Draw2D;
struct Renderer;
struct Texture;


struct Glyph {
	enum State { EMPTY, MEASURED, RASTERIZED, MISSING, REPACK };
	u32 codepoint;
	float u0, v0, u1, v1;
	float x0, y0, x1, y1;
	float advance_x;
	State state = EMPTY;
};

struct SplitWord {
	StringView head;
	StringView tail;
	float head_width;
};

LUMIX_RENDERER_API Vec2 measureTextA(const Font& font, const char* str, const char* str_end);
LUMIX_RENDERER_API const Glyph* findGlyph(const Font& font, u32 codepoint);
LUMIX_RENDERER_API Texture* getAtlasTexture(const Font& font);
LUMIX_RENDERER_API u32 nextCodepoint(const char*& p, const char* end);
LUMIX_RENDERER_API void ensureGlyphs(const Font& font, const char* str, const char* end);
LUMIX_RENDERER_API float getAdvanceY(const Font& font);
LUMIX_RENDERER_API float getDescender(const Font& font);
LUMIX_RENDERER_API float getAscender(const Font& font);
LUMIX_RENDERER_API float getHeight(const Font& font);
LUMIX_RENDERER_API bool isBuilt(const Font& font);
LUMIX_RENDERER_API SplitWord splitFirstWord(const Font& font, StringView text);
LUMIX_RENDERER_API void release(Font& font);


struct LUMIX_RENDERER_API FontResource final : Resource
{
	FontResource(const Path& path, ResourceManager& manager, IAllocator& allocator);

	ResourceType getType() const override { return TYPE; }

	void unload() override;
	bool load(Span<const u8> mem) override;
	Font* addRef(int font_size);
	void removeRef(Font& font);
	void buildIfDirty();
	void addGlyph(Font& font, u32 codepoint);

	TagAllocator m_allocator;
	OutputMemoryStream m_file_data;

	static const ResourceType TYPE;
};


struct LUMIX_RENDERER_API FontManager final : ResourceManager {
friend struct FontResource;
public:
	FontManager(Renderer& renderer, IAllocator& allocator);
	~FontManager();

	Texture* getAtlasTexture();
	void registerDraw2D(Draw2D& draw2d);
	void unregisterDraw2D(Draw2D& draw2d);

private:
	Resource* createResource(const Path& path) override;
	void destroyResource(Resource& resource) override;
	bool build();

private:
	TagAllocator m_allocator;
	Renderer& m_renderer;
	Texture* m_atlas_texture;
	Array<Font*> m_fonts;
	Array<Draw2D*> m_draw2ds;
	u32 m_atlas_width = 0;
	u32 m_atlas_height = 0;
	stbrp_context m_ctx;
	Array<stbrp_node> m_nodes;
	bool m_dirty = true;
	bool m_repack = false; // atlas space was released (font unloaded/removed), repack from scratch on next build
};


} // namespace Lumix
