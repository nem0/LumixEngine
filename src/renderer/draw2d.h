#pragma once

#include "core/color.h"
#include "core/array.h"
#include "core/math.h"
#include "renderer/gpu/gpu.h"


namespace Lumix
{

struct Font;

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

	Draw2D(IAllocator& allocator);

	void clear(Vec2 atlas_size);
	void pushClipRect(const Vec2& from, const Vec2& to);
	void popClipRect();
	void addLine(const Vec2& from, const Vec2& to, Color color, float width);
	void addRect(const Vec2& from, const Vec2& to, Color color, float width);
	void addRectFilled(const Vec2& from, const Vec2& to, Color color);
	void addText(const Font& font, const Vec2& pos, Color color, const char* text);
	void addImage(gpu::TextureHandle tex, const Vec2& from, const Vec2& to, const Vec2& uv0, const Vec2& uv1, Color color);
	const Array<Vertex>& getVertices() const { return m_vertices; }
	const Array<u32>& getIndices() const { return m_indices; }
	const Array<Cmd>& getCmds() const { return m_cmds; }

private:
	struct Rect {
		Vec2 from;
		Vec2 to;
	};

	Vec2 m_atlas_size;
	Array<Cmd> m_cmds;
	Array<u32> m_indices;
	Array<Vertex> m_vertices;
	Array<Rect> m_clip_queue;
};

} //namespace Lumix