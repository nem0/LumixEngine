#pragma once

#include "engine/array.h"
#include "engine/math.h"


namespace Lumix
{

#pragma pack(1)
struct Color {
	u8 r;
	u8 g;
	u8 b;
	u8 a;
};
#pragma pack()

struct Font;

struct Draw2D {
	struct Cmd {
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
	
	struct Rect {
		Vec2 from;
		Vec2 to;
	};

	Draw2D(IAllocator& allocator);

	void clear();
	void pushClipRect(const Vec2& from, const Vec2& to);
	void popClipRect();
	void addLine(const Vec2& from, const Vec2& to, Color color, float width);
	void addRect(const Vec2& from, const Vec2& to, Color color, float width);
	void addRectFilled(const Vec2& from, const Vec2& to, Color color);
	void addText(const Font& font, float font_size, const Vec2& pos, Color color, const char* text);

	Array<Rect> m_clip_queue;
	Array<Cmd> m_cmds;
	Array<u32> m_indices;
	Array<Vertex> m_vertices;
	IAllocator& m_allocator;
};

} //namespace Lumix