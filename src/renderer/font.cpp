#include "core/job_system.h"
#include "core/profiler.h"
#include "core/array.h"
#include "core/log.h"
#include "core/stream.h"
#include "core/string.h"
#include "font.h"
#include "renderer/draw2d.h"
#include "renderer/texture.h"
#include "renderer/renderer.h"

#define STB_RECT_PACK_IMPLEMENTATION
#include <stb/stb_rect_pack.h>
#include <ft2build.h>
#include FT_FREETYPE_H
#include FT_MODULE_H            // <freetype/ftmodapi.h>
#include FT_GLYPH_H             // <freetype/ftglyph.h>
#include FT_SYNTHESIS_H         // <freetype/ftsynth.h>
#include FT_OUTLINE_H           // <freetype/ftoutln.h>


namespace Lumix
{

struct Font {
	Font(IAllocator& allocator) : glyphs(allocator) {
		memory_rec.user = &allocator;
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
	}
	~Font() { closeFace(); }

	// releases the FreeType library and face, they are recreated on demand
	void closeFace() {
		if (library) FT_Done_Library(library); // releases the face too
		library = nullptr;
		face = nullptr;
	}

	// every font has its own library, so fonts can be processed in parallel (a library is not thread safe)
	// the library and face are kept between builds, so adding a glyph does not need to parse the font again
	// they reference resource->m_file_data, so they have to be closed when it is unloaded
	FT_MemoryRec_ memory_rec = {};
	FT_Library library = nullptr;
	FT_Face face = nullptr; // owned by library, sized and with unicode charmap
	FontResource* resource;
	HashMap<u32, Glyph> glyphs;
	u32 font_size = 0;
	float descender = 0;
	float ascender = 0;
	float height = 0;
	u32 ref = 0;
};

float getAdvanceY(const Font& font) { return float(font.font_size); }
float getDescender(const Font& font) { font.resource->buildIfDirty(); return font.descender; }
float getAscender(const Font& font) { font.resource->buildIfDirty(); return font.ascender; }
float getHeight(const Font& font) { font.resource->buildIfDirty(); return font.height; }
bool isBuilt(const Font& font) { font.resource->buildIfDirty(); return font.face != nullptr; }
void release(Font& font) { font.resource->removeRef(font); }


const Glyph* findGlyph(const Font& font, u32 codepoint) {
	font.resource->buildIfDirty();
	auto iter = font.glyphs.find(codepoint);
	if (!iter.isValid()) {
		if (codepoint < 0x20) return nullptr;
		// first use of this glyph, normally ensureGlyphs already added it
		font.resource->addGlyph(const_cast<Font&>(font), codepoint);
		font.resource->buildIfDirty();
		iter = font.glyphs.find(codepoint);
		if (!iter.isValid()) return nullptr;
	}
	return &iter.value();
}

Texture* getAtlasTexture(const Font& font) {
	return static_cast<FontManager&>(font.resource->getResourceManager()).getAtlasTexture();
}

u32 nextCodepoint(const char*& p, const char* end) {
	const u8 c = (u8)*p++;
	if (c < 0x80) return c;
	const u32 extra = c >= 0xF0 ? 3 : (c >= 0xE0 ? 2 : (c >= 0xC0 ? 1 : 0));
	if (extra == 0) return 0xFFFD;
	u32 cp = c & (0x3F >> extra);
	for (u32 i = 0; i < extra; ++i) {
		if (p == end || ((u8)*p & 0xC0) != 0x80) return 0xFFFD;
		cp = (cp << 6) | ((u8)*p++ & 0x3F);
	}
	return cp;
}

void ensureGlyphs(const Font& font, const char* str, const char* end) {
	for (const char* c = str; c != end && *c;) {
		const u32 cp = nextCodepoint(c, end);
		if (cp < 0x20 || font.glyphs.find(cp).isValid()) continue;
		font.resource->addGlyph(const_cast<Font&>(font), cp);
	}
	font.resource->buildIfDirty();
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
	ensureGlyphs(font, str, str_end);
	Vec2 res;
	res.x = 0;
	res.y = font.height;
	const char* c = str;
	bool prev_was_space = false;
	while (c != str_end && *c) {
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
		auto iter = font.glyphs.find(nextCodepoint(c, str_end));
		if (iter.isValid()) {
			const Glyph& glyph = iter.value();
			res.x += glyph.advance_x;
		}
	}
	return res;
}

struct ToChar {
	Font* font;
	u32 codepoint;
	bool packed;
};

// everything rasterizing one font produces, so fonts can be rasterized in parallel and merged afterwards
struct FontRasterResult {
	FontRasterResult(IAllocator& allocator) : rects(allocator), to_char(allocator) {}
	Array<stbrp_rect> rects;
	Array<ToChar> to_char;
};

static void blit(const FT_Bitmap& bitmap, const Glyph& c, const IVec2& size, u32* out) {
	ASSERT(bitmap.pixel_mode == FT_PIXEL_MODE_GRAY);
	if (bitmap.width == 0 || bitmap.rows == 0) return;
	const u32 u0 = u32(c.u0 * size.x + 0.5f);
	const u32 v0 = u32(c.v0 * size.y + 0.5f);
	const u32 w = minimum(bitmap.width, u32(c.u1 * size.x + 0.5f) - u0);
	const u32 h = minimum(bitmap.rows, u32(c.v1 * size.y + 0.5f) - v0);
	const u8* src = bitmap.buffer;
	u32* dst = &out[u0 + v0 * size.x];
	for (u32 y = 0; y < h; ++y, dst += size.x, src += bitmap.pitch) {
		for (u32 x = 0; x < w; ++x) {
			dst[x] = 0x00ffFFff | ((u32)src[x] << 24);
		}
	}
}

void FontManager::registerDraw2D(Draw2D& draw2d) {
	m_draw2ds.push(&draw2d);
	if (m_atlas_texture && m_atlas_texture->isReady()) draw2d.setAtlas(m_atlas_texture->handle, Vec2((float)m_atlas_texture->width, (float)m_atlas_texture->height));
}
void FontManager::unregisterDraw2D(Draw2D& draw2d) { m_draw2ds.eraseItem(&draw2d); }

Texture* FontManager::getAtlasTexture() {
	if (m_dirty) build();
	return m_atlas_texture;
}

// opens the library and face of a font if it is not open yet
static bool openFace(Font& font) {
	if (font.face) return true;

	FT_Error error = FT_New_Library(&font.memory_rec, &font.library);
	if (error != 0) {
		font.library = nullptr;
		return false;
	}
	FT_Add_Default_Modules(font.library);

	error = FT_New_Memory_Face(font.library, font.resource->m_file_data.data(), (u32)font.resource->m_file_data.size(), 0, &font.face);
	if (error != 0) {
		logError("Failed to create font ", font.resource->getPath());
		font.closeFace();
		return false;
	}

	FT_Size_RequestRec size_req = {};
	size_req.type = FT_SIZE_REQUEST_TYPE_REAL_DIM;
	size_req.height = (u32)font.font_size * 64;
	error = FT_Request_Size(font.face, &size_req);
	if (error != 0) {
		logError("Failed to request font size ", font.font_size, " for ", font.resource->getPath());
		font.closeFace();
		return false;
	}

	error = FT_Select_Charmap(font.face, FT_ENCODING_UNICODE);
	if (error != 0) {
		logError("Failed to select unicode charmap of font ", font.resource->getPath());
		font.closeFace();
		return false;
	}

	FT_Face face = font.face;
	font.descender = face->size->metrics.descender / 64.f;
	font.ascender = face->size->metrics.ascender / 64.f;
	font.height = face->size->metrics.height / 64.f;
	return true;
}

// measures every glyph that is not measured and creates rects for them
// on success font.face is open (sized, unicode charmap) for rasterize
static bool computeGlyphSizes(Font& font, FontRasterResult& out) {
	PROFILE_FUNCTION();
	profiler::pushString(font.resource->getPath().c_str());
	profiler::pushInt("font size", font.font_size);
	constexpr u32 PADDING = 1;
	bool any_empty = false;
	for (const Glyph& g : font.glyphs) {
		if (g.state == Glyph::EMPTY || g.state == Glyph::REPACK) {
			any_empty = true;
			break;
		}
	}
	if (!any_empty) return false;
	if (!openFace(font)) return false;

	FT_Face face = font.face;
	FT_Error error;

	for (Glyph& g : font.glyphs) {
		if (g.state == Glyph::REPACK) {
			// metrics are already known, only a new rect is needed
			g.state = Glyph::MEASURED;
			stbrp_rect& r = out.rects.emplace();
			r.w = u32(g.x1 - g.x0) + 2 * PADDING;
			r.h = u32(g.y1 - g.y0) + 2 * PADDING;
			out.to_char.push({&font, g.codepoint, false});
			continue;
		}
		if (g.state != Glyph::EMPTY) continue;

		g.u0 = g.v0 = 0;
		g.u1 = g.v1 = 1;
		g.x0 = g.y0 = g.x1 = g.y1 = 0;
		g.advance_x = 0;

		const u32 glyph_index = FT_Get_Char_Index(face, g.codepoint);
		if (glyph_index == 0) {
			g.state = Glyph::MISSING;
			continue;
		}

		error = FT_Load_Glyph(face, glyph_index, FT_LOAD_NO_BITMAP);
		if (error != 0) {
			g.state = Glyph::MISSING;
			continue;
		}

		FT_GlyphSlot slot = face->glyph;
		g.advance_x = float(((slot->advance.x + 63) & -64) / 64);

		// the same pixel bounds FT_Render_Glyph would produce, without rendering
		if (slot->format == FT_GLYPH_FORMAT_OUTLINE) {
			FT_BBox box;
			FT_Outline_Get_CBox(&slot->outline, &box);
			const long x_min = box.xMin & -64;
			const long x_max = (box.xMax + 63) & -64;
			const long y_min = box.yMin & -64;
			const long y_max = (box.yMax + 63) & -64;
			g.x0 = float(x_min / 64);
			g.x1 = float(x_max / 64);
			g.y0 = float(-y_max / 64);
			g.y1 = float(-y_min / 64);
		}
		g.state = Glyph::MEASURED;

		// the bitmap size is known now, so the atlas rect can be created
		stbrp_rect& r = out.rects.emplace();
		r.w = u32(g.x1 - g.x0) + 2 * PADDING;
		r.h = u32(g.y1 - g.y0) + 2 * PADDING;
		out.to_char.push({&font, g.codepoint, false});
	}

	return true;
}

// expects the face opened by computeGlyphSizes
static void rasterize(Font& font, const IVec2& size, Array<u32>& pixels) {
	FT_Error error;
	FT_Face face = font.face;

	for (Glyph& g : font.glyphs) {
		if (g.state != Glyph::MEASURED && g.state != Glyph::RASTERIZED) continue;
		g.state = Glyph::RASTERIZED;
		const u32 glyph_index = FT_Get_Char_Index(face, g.codepoint);
		if (glyph_index == 0) continue;

		error = FT_Load_Glyph(face, glyph_index, FT_LOAD_NO_BITMAP);
		if (error != 0) continue;

		FT_GlyphSlot slot = face->glyph;
		error = FT_Render_Glyph(slot, FT_RENDER_MODE_NORMAL);
		if (error != 0) continue;

		blit(slot->bitmap, g, size, pixels.begin());
	}
}

// Pack and upload new glyphs incrementally; rebuild when the atlas needs to grow.
bool FontManager::build() {
	PROFILE_FUNCTION();
	ASSERT(m_dirty);
	for(Font* font : m_fonts) {
		if (!font->resource->isReady()) return false;
	}
	m_dirty = false;
	constexpr u32 PADDING = 1;

	// atlas space of unloaded/removed fonts is never released by the packer, so start over
	const bool reset_atlas = m_repack;
	if (m_repack) {
		m_repack = false;
		for (Font* f : m_fonts) {
			for (Glyph& g : f->glyphs) {
				if (g.state == Glyph::MEASURED || g.state == Glyph::RASTERIZED) g.state = Glyph::REPACK;
			}
		}
		m_atlas_height = 0; // forces full rebuild
	}

	// fonts are independent: each one uses its own FT_Library/FT_Face and writes only to its own Font and result,
	// so they can run in parallel (FreeType libraries are not thread safe, but separate libraries are fine)
	// ToChar points into the results, so they must not move: reserve first and never grow the array afterwards
	Array<FontRasterResult> results(m_allocator);
	results.reserve(m_fonts.size());
	for (u32 i = 0, c = m_fonts.size(); i < c; ++i) results.emplace(m_allocator);

	// pass 1: measure all fonts, pass 2: rasterize them. The face of each font stays open in the font.
	jobs::forEach(m_fonts.size(), 1, [&](u32 idx, u32) {
		computeGlyphSizes(*m_fonts[idx], results[idx]);
	});

	// merge the rects in font order, the same order a serial build produces
	// rects[i] belongs to the i-th ToChar when walking the results in order (rects and to_char of a result are parallel)
	Array<stbrp_rect> rects(m_allocator);
	for (const FontRasterResult& result : results) {
		for (const stbrp_rect& r : result.rects) rects.push(r);
	}

	bool full_rebuild = false;
	constexpr u32 MAX_ATLAS_SIZE = 16384;

	// resets the packer and reserves the white pixel at (0, 0) so no glyph can take it
	auto reset_packer = [&]() {
		m_nodes.resize(m_atlas_width);
		stbrp_init_target(&m_ctx, m_atlas_width, MAX_ATLAS_SIZE, m_nodes.begin(), m_nodes.size());
		stbrp_rect white = {};
		white.w = white.h = 1;
		stbrp_pack_rects(&m_ctx, &white, 1);
		ASSERT(white.was_packed && white.x == 0 && white.y == 0);
	};

	if (m_atlas_height == 0) {
		full_rebuild = true;
		m_atlas_height = MAX_ATLAS_SIZE;
	}
	if (m_atlas_width == 0) {
		m_atlas_width = 2048;
		reset_packer();
	}
	else if (reset_atlas) {
		reset_packer();
	}

	{
		profiler::pushInt("rects", rects.size());
		// keep the atlas as narrow as it can be, but never taller than a texture can be (16384 on D3D12): widen it until everything fits
		for (;;) {
			{
				PROFILE_BLOCK("pack font");
				if (stbrp_pack_rects(&m_ctx, rects.begin(), rects.size())) break;
			}
			if (m_atlas_width >= MAX_ATLAS_SIZE && m_atlas_height >= MAX_ATLAS_SIZE) {
				logError("Fonts do not fit into a ", MAX_ATLAS_SIZE, "x", MAX_ATLAS_SIZE, " atlas, some glyphs are missing");
				for (stbrp_rect& r : rects) {
					if (r.was_packed) continue;
					r.x = r.y = 0;
					r.w = r.h = 0;
				}
				break;
			}

			// we have to resize the atlas, so all already rasterized glyphs are lost
			for (Font* f : m_fonts) {
				for (Glyph& g : f->glyphs) {
					if (g.state == Glyph::MEASURED || g.state == Glyph::RASTERIZED) g.state = Glyph::REPACK;
				}
			}

			if (m_atlas_height == MAX_ATLAS_SIZE) {
				m_atlas_width *= 2;
			}
			m_atlas_height = 0;
			m_dirty = true;
			reset_packer();
			return build();
		}
	}

	const u32 old_height = m_atlas_height;
	m_atlas_height = full_rebuild ? 1 : old_height;
	for (const stbrp_rect& r : rects) {
		ASSERT(u32(r.x + r.w) <= m_atlas_width);
		m_atlas_height = maximum(m_atlas_height, r.y + r.h);
	}
	m_atlas_height = nextPow2(m_atlas_height);
	if (!full_rebuild && m_atlas_height != old_height) {
		for (Font* f : m_fonts) {
			for (Glyph& g : f->glyphs) {
				if (g.state != Glyph::RASTERIZED) continue;
				g.v0 *= float(old_height) / m_atlas_height;
				g.v1 *= float(old_height) / m_atlas_height;
			}
		}
		full_rebuild = true;
	}

	const stbrp_rect* packed = rects.begin();
	for (FontRasterResult& result : results) {
		for (ToChar& tc : result.to_char) {
			const stbrp_rect& r = *packed++;
			tc.packed = r.was_packed;
			Glyph& c = tc.font->glyphs[tc.codepoint];
			if (!tc.packed) {
				// does not fit into the atlas, make it empty instead of sampling stale/whole-atlas uvs
				c.u0 = c.v0 = c.u1 = c.v1 = 0;
				c.state = Glyph::MISSING;
				continue;
			}
			c.u0 = (r.x + PADDING) / (float)m_atlas_width;
			c.v0 = (r.y + PADDING) / (float)m_atlas_height;
			c.u1 = float(r.x + r.w - PADDING) / m_atlas_width;
			c.v1 = float(r.y + r.h - PADDING) / m_atlas_height;
		}
	}

	if (full_rebuild) {
		Array<u32> pixels(m_allocator);
		pixels.resize(m_atlas_width * m_atlas_height);
		for (u32& p : pixels) p = 0;

		jobs::forEach(m_fonts.size(), 1, [&](u32 idx, u32) {
			PROFILE_BLOCK("rasterize font");
			if (m_fonts[idx]->face) rasterize(*m_fonts[idx], IVec2(m_atlas_width, m_atlas_height), pixels);
		});

		pixels[0] = 0xffFFffFF;
		if (m_atlas_texture) {
			m_atlas_texture->destroy();
		}
		else {
			auto& texture_manager = m_renderer.getTextureManager();
			m_atlas_texture = LUMIX_NEW(m_allocator, Texture)(Path("draw2d_atlas"), texture_manager, m_renderer, m_allocator);
		}
		m_atlas_texture->create(m_atlas_width, m_atlas_height, gpu::TextureFormat::RGBA8, pixels.begin(), pixels.byte_size());
		for (Draw2D* draw2d : m_draw2ds) draw2d->setAtlas(m_atlas_texture->handle, Vec2((float)m_atlas_width, (float)m_atlas_height));

		return true;
	}
	
	packed = rects.begin();
	for (FontRasterResult& result : results) {
		for (ToChar& tc : result.to_char) {
			const stbrp_rect& r = *packed++;
			if (!tc.packed || !tc.font->face) continue;
			FT_Face face = tc.font->face;
			const u32 glyph_index = FT_Get_Char_Index(face, tc.codepoint);
			if (!glyph_index || FT_Load_Glyph(face, glyph_index, FT_LOAD_NO_BITMAP) != 0) continue;
			if (FT_Render_Glyph(face->glyph, FT_RENDER_MODE_NORMAL) != 0) continue;
			const Renderer::MemRef memory = m_renderer.allocate(r.w * r.h * sizeof(u32));
			memset(memory.data, 0, memory.size); // blit writes only the glyph interior, padding must be transparent
			Glyph local = tc.font->glyphs[tc.codepoint];
			local.u0 = float(PADDING) / r.w;
			local.v0 = float(PADDING) / r.h;
			local.u1 = float(r.w - PADDING) / r.w;
			local.v1 = float(r.h - PADDING) / r.h;
			blit(face->glyph->bitmap, local, IVec2(r.w, r.h), (u32*)memory.data);
			DrawStream& stream = m_renderer.getDrawStream();
			stream.update(m_atlas_texture->handle, 0, r.x, r.y, 0, r.w, r.h, gpu::TextureFormat::RGBA8, memory.data, memory.size);
			stream.freeMemory(memory.data, m_renderer.getAllocator());
		}
	}
	for (Font* font : m_fonts) {
		for (Glyph& g : font->glyphs) if (g.state == Glyph::MEASURED) g.state = Glyph::RASTERIZED;
	}
	return true;
}


const ResourceType FontResource::TYPE("font");


void FontResource::unload() {
	// faces reference m_file_data, glyphs have to be measured again after a reload
	auto& manager = (FontManager&)m_resource_manager;
	for (Font* f : manager.m_fonts) {
		if (f->resource != this) continue;
		f->closeFace();
		for (Glyph& g : f->glyphs) g.state = Glyph::EMPTY;
	}
	manager.m_dirty = true;
	manager.m_repack = true;
	m_file_data.free();
}


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
	font->ref = 1;
	font->resource = this;
	font->font_size = font_size;
	for(u32 cp = 0x20; cp < 0xff; ++cp) {
		Glyph c = {};
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

void FontResource::addGlyph(Font& font, u32 codepoint) {
	Glyph c = {};
	c.codepoint = codepoint;
	font.glyphs.insert(codepoint, c);
	((FontManager&)m_resource_manager).m_dirty = true;
}


void FontResource::removeRef(Font& font)
{
	ASSERT(font.ref > 0);
	--font.ref;
	if(font.ref == 0) {
		auto& manager = (FontManager&)m_resource_manager;
		manager.m_fonts.eraseItem(&font);
		LUMIX_DELETE(manager.m_allocator, &font);
		manager.m_dirty = true;
		manager.m_repack = true;
	}
}


FontManager::FontManager(Renderer& renderer, IAllocator& allocator)
	: ResourceManager(allocator)
	, m_allocator(allocator, "fonts")
	, m_renderer(renderer)
	, m_atlas_texture(nullptr)
	, m_fonts(allocator)
	, m_draw2ds(allocator)
	, m_nodes(allocator)
{
	build();
}


FontManager::~FontManager()
{
	for (Draw2D* draw2d : m_draw2ds) draw2d->m_font_manager = nullptr;
	for (Font* font : m_fonts) {
		LUMIX_DELETE(m_allocator, font);
	}
	// FontResource::unload iterates m_fonts, do not leave dangling pointers there
	m_fonts.clear();

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
