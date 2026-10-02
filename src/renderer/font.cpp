#include "core/job_system.h"
#include "core/profiler.h"
#include "core/array.h"
#include "core/log.h"
#include "core/stream.h"
#include "core/string.h"
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
	float height = 0;
	u32 ref = 0;
	bool is_built = false;
};

float getAdvanceY(const Font& font) { return float(font.font_size); }
float getDescender(const Font& font) { font.resource->buildIfDirty(); return font.descender; }
float getAscender(const Font& font) { font.resource->buildIfDirty(); return font.ascender; }
float getHeight(const Font& font) { font.resource->buildIfDirty(); return font.height; }
bool isBuilt(const Font& font) { font.resource->buildIfDirty(); return font.is_built; }
void release(Font& font) { font.resource->removeRef(font); }


const Glyph* findGlyph(const Font& font, u32 codepoint) {
	font.resource->buildIfDirty();
	auto iter = font.glyphs.find(codepoint);
	if (!iter.isValid()) return nullptr;
	return &iter.value();
}

SplitWord splitFirstWord(const Font& font, StringView text) {
	// trim leading whitespace
	const char* text_end = text.end();
	while (text.data < text_end && isWhitespace(*text.data)) { ++text.data; --text.length; }
	
	SplitWord res;
	res.head.data = text.data;
	res.head.length = 0;
	res.tail = text;
	// find the first whitespace
	while (res.head.end() != text_end && !isWhitespace(*res.head.end())) ++res.head.length;
	res.tail.data = res.head.end();
	res.tail.length = text_end - res.tail.data;

	// measure the word
	Vec2 size = measureTextA(font, res.head.data, res.head.end());
	res.head_width = size.x;
	return res;
}

Vec2 measureTextA(const Font& font, const char* str, const char* str_end) {
	font.resource->buildIfDirty();
	Vec2 res;
	res.x = 0;
	res.y = font.height;
	const char* c = str;
	bool prev_was_space = false;
	while (*c && c != str_end) {
		if (*c == '\r') {
			++c;
			continue;
		}
		if (isWhitespace(*c)) {
			if (!prev_was_space) {
				auto iter = font.glyphs.find(' ');
				if (iter.isValid()) {
					const Glyph& glyph = iter.value();
					res.x += glyph.advance_x;
				}
				prev_was_space = true;
			}
			++c;
			continue;
		}
		prev_was_space = false;
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
	const Array<u8>* bmp; // glyph bitmaps of the font, bmp_offset points into it
};

// everything rasterizing one font produces, so fonts can be rasterized in parallel and merged afterwards
struct FontRasterResult {
	FontRasterResult(IAllocator& allocator) : rects(allocator), to_char(allocator), bmp(allocator) {}
	Array<stbrp_rect> rects;
	Array<ToChar> to_char;
	Array<u8> bmp;
};

static void blit(FT_Bitmap* bitmap,  Array<u8>* out) {
	ASSERT(bitmap->pixel_mode == FT_PIXEL_MODE_GRAY);
	const u32 offset = out->size();
	const u32 src_pitch = bitmap->pitch;
	const u8* src = bitmap->buffer;
	const u32 new_size = out->size() + bitmap->width * bitmap->rows;
	// Array::resize reserves the exact size, grow geometrically or every glyph would copy the whole array
	if (new_size > out->capacity()) out->reserve(maximum(new_size, out->capacity() * 2));
	out->resize(new_size);
	u8* dst = out->begin() + offset;
	for (u32 y = 0; y < bitmap->rows; ++y, src += src_pitch, dst += bitmap->width) {
		memcpy(dst, src, bitmap->width);
	}
}

static void blit(const ToChar& tc, const IVec2& size, Array<u32>* out) {
	const Glyph& c = tc.font->glyphs[tc.codepoint];
	const u8* src = tc.bmp->begin() + tc.bmp_offset;
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

bool FontManager::build() {
	PROFILE_FUNCTION();
	ASSERT(m_dirty);
	for(Font* font : m_fonts) {
		if (!font->resource->isReady()) return false;
	}
	m_dirty = false;
	FT_MemoryRec_ memory_rec = {};
	memory_rec.user = &m_allocator;
	memory_rec.alloc = [](FT_Memory memory, long size) -> void* { 
		IAllocator* alloc = (IAllocator*)memory->user;
		return alloc->allocate(size, 8);
	};
	memory_rec.free = [](FT_Memory memory, void* block) -> void { 
		IAllocator* alloc = (IAllocator*)memory->user;
		alloc->deallocate(block);
	};
	memory_rec.realloc = [](FT_Memory memory, long cur_size, long new_size, void* block) -> void* {
		IAllocator* alloc = (IAllocator*)memory->user;
		return alloc->reallocate(block, new_size, cur_size, 8);
	};

	constexpr u32 PADDING = 1;

	// fonts are independent: each one is rasterized with its own FT_Library/FT_Face and writes only to its own Font and result,
	// so they can run in parallel (FreeType libraries are not thread safe, but separate libraries are fine)
	// ToChar points into the results, so they must not move: reserve first and never grow the array afterwards
	Array<FontRasterResult> results(m_allocator);
	results.reserve(m_fonts.size());
	for (u32 i = 0, c = m_fonts.size(); i < c; ++i) results.emplace(m_allocator);

	auto rasterize = [&](u32 font_idx) {
		PROFILE_BLOCK("rasterize font");
		Font* font = m_fonts[font_idx];
		FontRasterResult& out = results[font_idx];

		FT_Library ft_library;
		FT_Error error = FT_New_Library(&memory_rec, &ft_library);
		if (error != 0) return;
		FT_Add_Default_Modules(ft_library);

		FT_Face face;
		error = FT_New_Memory_Face(ft_library, font->resource->m_file_data.data(), (u32)font->resource->m_file_data.size(), 0, &face);
		if (error != 0) {
			logError("Failed to create font ", font->resource->getPath());
			FT_Done_Library(ft_library);
			return;
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
			FT_Done_Library(ft_library);
			return;
		}

		error = FT_Select_Charmap(face, FT_ENCODING_UNICODE);
		if (error != 0) {
			logError("Failed to select unicode charmap of font ", font->resource->getPath());
			FT_Done_Library(ft_library);
			return;
		}
		
		font->descender = face->size->metrics.descender / 64.f;
		font->ascender = face->size->metrics.ascender / 64.f;
		font->height = face->size->metrics.height / 64.f;
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
			stbrp_rect& r = out.rects.emplace();
			r.w = ft_bitmap->width + 2 * PADDING;
			r.h = ft_bitmap->rows + 2 * PADDING;
			out.to_char.push({font, c.codepoint, (u32)out.bmp.size(), static_cast<u32>(slot->advance.x), &out.bmp});
			blit(ft_bitmap, &out.bmp);
			c.x0 = float(slot->bitmap_left);
			c.y0 = float(-slot->bitmap_top);
			c.x1 = float(c.x0 + r.w - 2 * PADDING);
			c.y1 = float(c.y0 + r.h - 2 * PADDING);
		}
		font->is_built = true;
		FT_Done_Library(ft_library);
	};

	jobs::forEach(m_fonts.size(), 1, [&](u32 idx, u32) { rasterize(idx); });

	// merge the rects in font order, the same order a serial build produces
	// rects[i] belongs to the i-th ToChar when walking the results in order (rects and to_char of a result are parallel)
	Array<stbrp_rect> rects(m_allocator);
	for (const FontRasterResult& result : results) {
		for (const stbrp_rect& r : result.rects) rects.push(r);
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

	const stbrp_rect* packed = rects.begin();
	for (const FontRasterResult& result : results) {
		for (const ToChar& tc : result.to_char) {
			const stbrp_rect& r = *packed++;
			Glyph& c = tc.font->glyphs[tc.codepoint];
			c.advance_x = float(((tc.advance_x + 63) & -64) / 64);
			c.u0 = (r.x + PADDING) / (float)w;
			c.v0 = (r.y + PADDING) / (float)h;
			c.u1 = float(r.x + r.w - PADDING) / w;
			c.v1 = float(r.y + r.h - PADDING) / h;
		}
	}

	Array<u32> pixels(m_allocator);
	pixels.resize(w * h);
	for (u32& p : pixels) p = 0;
	for (const FontRasterResult& result : results) {
		for (const ToChar& tc : result.to_char) blit(tc, IVec2(w, h), &pixels);
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

	return true;
}


const ResourceType FontResource::TYPE("font");


FontResource::FontResource(const Path& path, ResourceManager& manager, IAllocator& allocator)
	: Resource(path, manager, allocator)
	, m_allocator(allocator, m_path.c_str())
	, m_file_data(m_allocator)
{
}


bool FontResource::load(Span<const u8> mem) {
	if (mem.length() == 0) return false;
	
	m_file_data.resize(mem.length());
	memcpy(m_file_data.getMutableData(), mem.begin(), mem.length());
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
	font->is_built = false;
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
	return font;
}

void FontResource::buildIfDirty() {
	auto& manager = (FontManager&)m_resource_manager;
	if (manager.m_dirty) manager.build();
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
	}
}


FontManager::FontManager(Renderer& renderer, IAllocator& allocator)
	: ResourceManager(allocator)
	, m_allocator(allocator, "fonts")
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
