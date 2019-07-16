#include "draw2d.h"


namespace Lumix {


Draw2D::Draw2D(IAllocator& allocator) 
	: m_allocator(allocator)
	, m_cmds(allocator)
	, m_indices(allocator)
	, m_vertices(allocator)
	, m_clip_queue(allocator)
{
	clear();
}

void Draw2D::clear() {
	m_cmds.clear();
	m_indices.clear();
	m_vertices.clear();
	Cmd& cmd = m_cmds.emplace();
	cmd.indices_count = 0;
	cmd.index_offset = 0;
	cmd.clip_pos = { -1, -1 };
	cmd.clip_size = { -1, -1 };
	m_clip_queue.push({{-1, -1}, {-1, -1}});
}

void Draw2D::pushClipRect(const Vec2& from, const Vec2& to) {
	m_clip_queue.push({from, to});
	Cmd& cmd = m_cmds.emplace();
	cmd.clip_pos = from;
	cmd.clip_size = to;
	cmd.indices_count = 0;
	cmd.index_offset = m_indices.size();
}

void Draw2D::popClipRect() {
	m_clip_queue.pop();
	const Rect& r = m_clip_queue.back();
	Cmd& cmd = m_cmds.emplace();
	cmd.clip_pos = r.from;
	cmd.clip_size = r.to;
	cmd.indices_count = 0;
	cmd.index_offset = m_indices.size();
}

void Draw2D::addLine(const Vec2& from, const Vec2& to, Color color, float width) {
}

void Draw2D::addRect(const Vec2& from, const Vec2& to, Color color, float width) {
	addLine(from, {from.x, to.y}, color, width);
	addLine({from.x, to.y}, to, color, width);
	addLine(to, {to.x, from.y}, color, width);
	addLine({to.x, from.y}, from, color, width);
}

void Draw2D::addRectFilled(const Vec2& from, const Vec2& to, Color color) {
	Cmd& cmd = m_cmds.back();
	const u32 voff = m_vertices.size();
	m_indices.push(voff);
	m_indices.push(voff + 1);
	m_indices.push(voff + 2);

	m_indices.push(voff);
	m_indices.push(voff + 2);
	m_indices.push(voff + 3);

	m_vertices.push({from, {0, 0}, color});
	m_vertices.push({{from.x, to.y}, {0, 1}, color});
	m_vertices.push({to, {1, 1}, color});
	m_vertices.push({{to.x, from.y}, {1, 0}, color});
	
	cmd.indices_count += 6;
}

void Draw2D::addText(const Font& font, float font_size, const Vec2& pos, Color color, const char* text) {
}

} // namespace Lumix