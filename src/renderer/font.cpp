#include "engine/log.h"
#include "engine/os.h"
#include "engine/stream.h"
#include "font.h"
#include "renderer/texture.h"
#include "renderer/renderer.h"

#define STB_RECT_PACK_IMPLEMENTATION
#include <stb/stb_rect_pack.h>
#include <ft2build.h>
#include FT_FREETYPE_H
#include FT_MODULE_H            // <freetype/ftmodapi.h>
#include FT_GLYPH_H             // <freetype/ftglyph.h>
#include FT_SYNTHESIS_H         // <freetype/ftsynth.h>


namespace Lumix
{

struct Font {
	Font(IAllocator& allocator) : glyphs(allocator) {}
	FontResource* resource;
	HashMap<u32, Glyph> glyphs;
	u32 font_size = 0;
	float descender = 0;
	float ascender = 0;
	u32 ref = 0;
};

float getAdvanceY(const Font& font) { return float(font.font_size); }
float getDescender(const Font& font) { return font.descender; }
float getAscender(const Font& font) { return font.ascender; }

const Glyph* findGlyph(const Font& font, u32 codepoint) {
	auto iter = font.glyphs.find(codepoint);
	if (!iter.isValid()) return nullptr;
	return &iter.value();
}

Vec2 measureTextA(const Font& font, const char* str, const char* str_end) {
	Vec2 res;
	res.x = 0;
	res.y = (float)font.font_size;
	const char* c = str;
	while (*c && c != str_end) {
		auto iter = font.glyphs.find(*c);
		if (iter.isValid()) {
			const Glyph& glyph = iter.value();
			res.x += glyph.advance_x;
		}
		++c;
	}
	return res;
}

struct ToChar {
	Font* font;
	u32 codepoint;
	u32 bmp_offset;
	u32 advance_x;
};

static void blit(FT_Bitmap* bitmap,  Array<u8>* out) {
	ASSERT(bitmap->pixel_mode == FT_PIXEL_MODE_GRAY);
	const u32 offset = out->size();
	const u32 src_pitch = bitmap->pitch;
	const u8* src = bitmap->buffer;
	u8* dst = out->begin() + offset;
	out->resize(out->size() + bitmap->width * bitmap->rows);
	for (u32 y = 0; y < bitmap->rows; ++y, src += src_pitch, dst += bitmap->width) {
		memcpy(dst, src, bitmap->width);
	}
}

static void blit(const ToChar& tc, const Array<u8>& src_bmp, const IVec2& size, Array<u32>* out) {
	const Glyph& c = tc.font->glyphs[tc.codepoint];
	const u8* src = &src_bmp[tc.bmp_offset];
	const u32 u0 = u32(c.u0 * size.x + 0.5f);
	const u32 v0 = u32(c.v0 * size.y + 0.5f);
	const u32 u1 = u32(c.u1 * size.x + 0.5f);
	const u32 v1 = u32(c.v1 * size.y + 0.5f);
	const u32 w = u1 - u0;
	const u32 h = v1 - v0;
	u32* dst = &(*out)[u0 + v0 * size.x];
	for (u32 y = 0; y < h; ++y, dst += size.x, src += w) {
		for (u32 x = 0; x < w; ++x) {
			dst[x] = 0x00ffFFff | ((u32)src[x] << 24);
		}
	}
}

Texture* FontManager::getAtlasTexture() {
	if (m_dirty) build();
	return m_atlas_texture;
}

bool FontManager::build()
{
	ASSERT(m_dirty);
	for(Font* font : m_fonts) {
		if (!font->resource->isReady()) return false;
	}
	m_dirty = false;
	FT_MemoryRec_ memory_rec = {};
	memory_rec.user = &m_allocator;
	memory_rec.alloc = [](FT_Memory memory, long size) -> void* { 
		IAllocator* alloc = (IAllocator*)memory->user;
		return alloc->allocate(size);
	};
	memory_rec.free = [](FT_Memory memory, void* block) -> void { 
		IAllocator* alloc = (IAllocator*)memory->user;
		alloc->deallocate(block);
	};
	memory_rec.realloc = [](FT_Memory memory, long cur_size, long new_size, void* block) -> void* {
		IAllocator* alloc = (IAllocator*)memory->user;
		return alloc->reallocate(block, new_size);
	};

	FT_Library ft_library;
	FT_Error error = FT_New_Library(&memory_rec, &ft_library);
	if (error != 0) return false;

	FT_Add_Default_Modules(ft_library);

	Array<u8> tmp_bmp(m_allocator);
	tmp_bmp.reserve(1024 * 1024);
	Array<stbrp_rect> rects(m_allocator);
	Array<ToChar> to_char(m_allocator);
	constexpr u32 PADDING = 1;

	for(Font* font : m_fonts) {
		FT_Face face;
		error = FT_New_Memory_Face(ft_library, font->resource->file_data.data(), (u32)font->resource->file_data.size(), 0, &face);
		if (error != 0) {
			logError("Failed to create font ", font->resource->getPath());
			continue;
		}
	
		FT_Size_RequestRec size_req;
		size_req.type = FT_SIZE_REQUEST_TYPE_REAL_DIM;
		size_req.width = 0;
		size_req.height = (u32)font->font_size * 64;
		size_req.horiResolution = 0;
		size_req.vertResolution = 0;
		error = FT_Request_Size(face, &size_req);
		if (error != 0) {
			logError("Failed to request font size ", font->font_size, " for ", font->resource->getPath());
			continue;
		}

		error = FT_Select_Charmap(face, FT_ENCODING_UNICODE);
		if (error != 0) {
			logError("Failed to select unicode charmap of font ", font->resource->getPath());
			continue;
		}
		
		font->descender = face->descender / 64.f;
		font->ascender = face->ascender / 64.f;
		for (Glyph& c : font->glyphs) {
			c.u0 = c.v0 = 0;
			c.u1 = c.v1 = 1;

			const u32 glyph_index = FT_Get_Char_Index(face, c.codepoint);
			if (glyph_index == 0) continue;

			error = FT_Load_Glyph(face, glyph_index, FT_LOAD_NO_BITMAP);
			if (error) continue;

			FT_GlyphSlot slot = face->glyph;
			error = FT_Render_Glyph(slot, FT_RENDER_MODE_NORMAL);
			if (error != 0) continue;

			FT_Bitmap* ft_bitmap = &face->glyph->bitmap;
			stbrp_rect& r = rects.emplace();
			r.w = ft_bitmap->width + 2 * PADDING;
			r.h = ft_bitmap->rows + 2 * PADDING;
			to_char.push({font, c.codepoint, (u32)tmp_bmp.size(), static_cast<u32>(slot->advance.x)});
			blit(ft_bitmap, &tmp_bmp);
			c.x0 = float(slot->bitmap_left);
			c.y0 = float(-slot->bitmap_top);
			c.x1 = float(c.x0 + r.w - 2 * PADDING);
			c.y1 = float(c.y0 + r.h - 2 * PADDING);
		}
	}

	stbrp_context ctx;
	Array<stbrp_node> nodes(m_allocator);
	nodes.resize(2048);
	stbrp_init_target(&ctx, 2048, 32 * 1024, nodes.begin(), nodes.size());
	stbrp_pack_rects(&ctx, rects.begin(), rects.size());

	u32 w = 2048;
	u32 h = 1;
	for (const stbrp_rect& r : rects) {
		ASSERT(u32(r.x + r.w) <= w);
		h = maximum(h, r.y + r.h);
	}

	for (const stbrp_rect& r : rects) {
		const ToChar& tc = to_char[int(&r - rects.begin())];
		Glyph& c = tc.font->glyphs[tc.codepoint];
		c.advance_x = float(((tc.advance_x + 63) & -64) / 64);
		c.u0 = (r.x + PADDING) / (float)w;
		c.v0 = (r.y + PADDING) / (float)h;
		c.u1 = float(r.x + r.w - PADDING) / w;
		c.v1 = float(r.y + r.h - PADDING) / h;
	}

	Array<u32> pixels(m_allocator);
	pixels.resize(w * h);
	for (u32& p : pixels) p = 0;
	for (const ToChar& tc : to_char) {
		blit(tc, tmp_bmp, IVec2(w, h), &pixels);
	}

	pixels[0] = 0xffFFffFF;
	if (m_atlas_texture) {
		m_atlas_texture->destroy();
	}
	else {
		auto& texture_manager = m_renderer.getTextureManager();
		m_atlas_texture = LUMIX_NEW(m_allocator, Texture)(Path("draw2d_atlas"), texture_manager, m_renderer, m_allocator);
	}
	m_atlas_texture->create(w, h, gpu::TextureFormat::RGBA8, pixels.begin(), pixels.byte_size());

	FT_Done_Library(ft_library);
	return true;
}


const ResourceType FontResource::TYPE("font");


FontResource::FontResource(const Path& path, ResourceManager& manager, IAllocator& allocator)
	: Resource(path, manager, allocator)
	, file_data(allocator)
{
}


bool FontResource::load(u64 size, const u8* mem)
{
	if (size <= 0) return false;
	
	file_data.resize((int)size);
	memcpy(file_data.getMutableData(), mem, size);
	return true;
}


Font* FontResource::addRef(int font_size)
{
	auto& manager = (FontManager&)m_resource_manager;
	for (Font* f : manager.m_fonts) {
		if (f->resource == this && f->font_size == font_size) {
			++f->ref;
			return f;
		}
	}
	Font* font = LUMIX_NEW(manager.m_allocator, Font)(manager.m_allocator);
	font->ref = 1;
	font->resource = this;
	font->font_size = font_size;
	for(u32 cp = 0x20; cp < 0xff; ++cp) {
		Glyph c;
		c.codepoint = cp;
		font->glyphs.insert(cp, c);
	}
	manager.m_fonts.push(font);
	manager.m_dirty = true;
	if (isReady()) manager.build();
	return font;
}


void FontResource::removeRef(Font& font)
{
	ASSERT(font.ref > 0);
	--font.ref;
	if(font.ref == 0) {
		auto& manager = (FontManager&)m_resource_manager;
		LUMIX_DELETE(manager.m_allocator, &font);
		manager.m_fonts.eraseItem(&font);
		manager.m_dirty = true;
		if (isReady()) manager.build();
	}
}


FontManager::FontManager(Renderer& renderer, IAllocator& allocator)
	: ResourceManager(allocator)
	, m_allocator(allocator)
	, m_renderer(renderer)
	, m_atlas_texture(nullptr)
	, m_fonts(allocator)
{
	build();
}


FontManager::~FontManager()
{
	for (Font* font : m_fonts) {
		LUMIX_DELETE(m_allocator, font);
	}

	if (m_atlas_texture) {
		m_atlas_texture->destroy();
		LUMIX_DELETE(m_allocator, m_atlas_texture);
	}
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