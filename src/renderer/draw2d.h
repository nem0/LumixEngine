#pragma once

#include "core/color.h"
#include "core/array.h"
#include "core/math.h"
#include "renderer/gpu/gpu.h"


namespace Lumix
{

struct Font;
struct FontManager;

struct LUMIX_RENDERER_API Draw2D {
	struct Cmd {
		gpu::TextureHandle texture;
		u32 indices_count;
		u32 index_offset;
		Vec2 clip_pos;
		Vec2 clip_size;
	};

	struct Vertex {
		Vec2 pos;
		Vec2 uv;
		Color color; 
	};

	// if font_manager is given, atlas used for shapes and text is kept up to date, even if it's rebuilt in the middle of a frame
	Draw2D(IAllocator& allocator, FontManager* font_manager = nullptr);
	Draw2D(const Draw2D&) = delete;
	void operator=(const Draw2D&) = delete;
	~Draw2D();

	void clear();
	void setAtlas(gpu::TextureHandle atlas, Vec2 size) { m_atlas_handle = atlas; m_atlas_size = size; }
	void pushClipRect(const Vec2& from, const Vec2& to);
	void popClipRect();
	void addLine(const Vec2& from, const Vec2& to, Color color, float width);
	void addRect(const Vec2& from, const Vec2& to, Color color, float width);
	void addRectFilled(const Vec2& from, const Vec2& to, Color color);
	void addMesh(gpu::TextureHandle texture, Span<const Vertex> vertices, Span<const u32> indices, const Vec2& offset, float scale);
	void addText(const Font& font, const Vec2& pos, Color color, const char* text);
	void addText(const Font& font, const Vec2& pos, Color color, StringView text);
	void addImage(gpu::TextureHandle tex, const Vec2& from, const Vec2& to, const Vec2& uv0, const Vec2& uv1, Color color);
	const Array<Vertex>& getVertices() const { return m_vertices; }
	const Array<u32>& getIndices() const { return m_indices; }
	const Array<Cmd>& getCmds() const { return m_cmds; }

private:
	struct Rect {
		Vec2 from;
		Vec2 to;
	};

	// atlas shapes are recorded against
	gpu::TextureHandle m_atlas_handle = gpu::INVALID_TEXTURE;
	Vec2 m_atlas_size = Vec2(1, 1);
	FontManager* m_font_manager = nullptr;
	friend struct FontManager;
	Array<Cmd> m_cmds;
	Array<u32> m_indices;
	Array<Vertex> m_vertices;
	Array<Rect> m_clip_queue;
};

} //namespace Lumix
