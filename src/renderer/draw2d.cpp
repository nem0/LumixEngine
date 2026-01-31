#include "draw2d.h"
#include "font.h"


namespace black {


Draw2D::Draw2D(IAllocator& allocator) 
	: m_cmds(allocator)
	, m_indices(allocator)
	, m_vertices(allocator)
	, m_clip_queue(allocator)
{
	clear({1, 1});
}

void Draw2D::clear(Vec2 atlas_size) {
	m_cmds.clear();
	m_indices.clear();
	m_vertices.clear();
	m_atlas_size = atlas_size;
	Cmd& cmd = m_cmds.emplace();
	cmd.texture = nullptr;
	cmd.indices_count = 0;
	cmd.index_offset = 0;
	cmd.clip_pos = { -1, -1 };
	cmd.clip_size = { -1, -1 };
	m_clip_queue.clear();
	m_clip_queue.push({{-1, -1}, {-2, -2}});
}

void Draw2D::pushClipRect(const Vec2& from, const Vec2& to) {
	Rect r = {from, to};
	if (!m_clip_queue.empty()) {
		const Rect prev =  m_clip_queue.back();
		if (prev.to.x >= 0) {
			r.from.x = maximum(r.from.x, prev.from.x);
			r.from.y = maximum(r.from.y, prev.from.y);
			r.to.x = minimum(r.to.x, prev.to.x);
			r.to.y = minimum(r.to.y, prev.to.y);
		}
	}
	r.to.x = maximum(r.from.x, r.to.x);
	r.to.y = maximum(r.from.y, r.to.y);

	m_clip_queue.push({r.from, r.to});
	Cmd& cmd = m_cmds.emplace();
	cmd.texture = nullptr;
	cmd.clip_pos = r.from;
	cmd.clip_size = r.to - r.from;
	cmd.indices_count = 0;
	cmd.index_offset = m_indices.size();
}

void Draw2D::popClipRect() {
	m_clip_queue.pop();
	const Rect& r = m_clip_queue.back();
	Cmd& cmd = m_cmds.emplace();
	cmd.texture = nullptr;
	cmd.clip_pos = r.from;
	cmd.clip_size = r.to - r.from;
	cmd.indices_count = 0;
	cmd.index_offset = m_indices.size();
}

void Draw2D::addLine(const Vec2& p0, const Vec2& p1, Color color, float width) {
	Cmd* cmd = &m_cmds.back();

	if (cmd->texture != nullptr && cmd->indices_count != 0) {
		cmd = &m_cmds.emplace();
		const Rect& r = m_clip_queue.back();
		cmd->clip_pos = r.from;
		cmd->clip_size = r.to - r.from;
		cmd->indices_count = 0;
		cmd->index_offset = m_indices.size();
	}
	
	Vec2 from = p0 + Vec2(0.5f);
	Vec2 to = p1 + Vec2(0.5f);

	cmd->texture = nullptr;
	const Vec2 uv = Vec2(0.5f) / m_atlas_size;
	const Vec2 dir = normalize(to - from);
	const Vec2 n = Vec2(dir.y, -dir.x) * (width * 0.5f);
	const u32 voff = m_vertices.size();
	
	from = from - dir * width * 0.5f;
	to = to + dir * width * 0.5f;

	m_vertices.push({from + n, uv, color});
	m_vertices.push({from - n, uv, color});
	m_vertices.push({to - n, uv, color});
	m_vertices.push({to + n, uv, color});

	m_indices.push(voff);
	m_indices.push(voff + 1);
	m_indices.push(voff + 2);

	m_indices.push(voff);
	m_indices.push(voff + 2);
	m_indices.push(voff + 3);

	cmd->indices_count += 6;
}

void Draw2D::addRect(const Vec2& from, const Vec2& to, Color color, float width) {
	addLine(from, {from.x, to.y}, color, width);
	addLine({from.x, to.y}, to, color, width);
	addLine(to, {to.x, from.y}, color, width);
	addLine({to.x, from.y}, from, color, width);
}

void Draw2D::addRectFilled(const Vec2& from, const Vec2& to, Color color) {
	Cmd* cmd = &m_cmds.back();

	if (cmd->texture != nullptr && cmd->indices_count != 0) {
		cmd = &m_cmds.emplace();
		const Rect& r = m_clip_queue.back();
		cmd->clip_pos = r.from;
		cmd->clip_size = r.to - r.from;
		cmd->indices_count = 0;
		cmd->index_offset = m_indices.size();
	}

	cmd->texture = nullptr;
	const u32 voff = m_vertices.size();
	m_indices.push(voff);
	m_indices.push(voff + 1);
	m_indices.push(voff + 2);

	m_indices.push(voff);
	m_indices.push(voff + 2);
	m_indices.push(voff + 3);

	const Vec2 uv = Vec2(0.5f) / m_atlas_size;

	m_vertices.push({from, uv, color});
	m_vertices.push({{from.x, to.y}, uv, color});
	m_vertices.push({to, uv, color});
	m_vertices.push({{to.x, from.y}, uv, color});
	
	cmd->indices_count += 6;
}

void Draw2D::addImage(gpu::TextureHandle* tex, const Vec2& from, const Vec2& to, const Vec2& uv0, const Vec2& uv1, Color color) {
	Cmd* cmd = &m_cmds.back();

	if (cmd->texture != tex && cmd->indices_count != 0) {
		cmd = &m_cmds.emplace();
		const Rect& r = m_clip_queue.back();
		cmd->clip_pos = r.from;
		cmd->clip_size = r.to - r.from;
		cmd->indices_count = 0;
		cmd->index_offset = m_indices.size();
	}

	cmd->texture = tex;
	const u32 voff = m_vertices.size();
	m_indices.push(voff);
	m_indices.push(voff + 1);
	m_indices.push(voff + 2);

	m_indices.push(voff);
	m_indices.push(voff + 2);
	m_indices.push(voff + 3);

	m_vertices.push({from, uv0, color});
	m_vertices.push({{from.x, to.y}, {uv0.x, uv1.y}, color});
	m_vertices.push({to, uv1, color});
	m_vertices.push({{to.x, from.y}, {uv1.x, uv0.y}, color});
	
	cmd->indices_count += 6;
}

void Draw2D::addText(const Font& font, const Vec2& pos, Color color, const char* str) {
	if (!*str) return;
	Cmd* cmd = &m_cmds.back();

	if (cmd->texture != nullptr && cmd->indices_count != 0) {
		cmd = &m_cmds.emplace();
		const Rect& r = m_clip_queue.back();
		cmd->clip_pos = r.from;
		cmd->clip_size = r.to - r.from;
		cmd->indices_count = 0;
		cmd->index_offset = m_indices.size();
	}

	cmd->texture = nullptr;
	
	Vec2 p = pos;
	p.x = float(int(p.x));
	p.y = float(int(p.y));

	for (const char* c = str; *c; ++c) {
		if (*c == '\r') continue;
		if (*c == '\n') {
			p.x = float(int(pos.x));
			p.y += getAdvanceY(font);
			continue;
		}
		const Glyph* glyph = findGlyph(font, *c);
		if (!glyph) {
			p.x += 16;
			continue;
		}
	
		const u32 voff = m_vertices.size();
		m_indices.push(voff);
		m_indices.push(voff + 1);
		m_indices.push(voff + 2);

		m_indices.push(voff);
		m_indices.push(voff + 2);
		m_indices.push(voff + 3);

		m_vertices.push({ p + Vec2(glyph->x0, glyph->y0), { glyph->u0, glyph->v0 }, color });
		m_vertices.push({ p + Vec2(glyph->x1, glyph->y0), { glyph->u1, glyph->v0 }, color });
		m_vertices.push({ p + Vec2(glyph->x1, glyph->y1), { glyph->u1, glyph->v1 }, color });
		m_vertices.push({ p + Vec2(glyph->x0, glyph->y1), { glyph->u0, glyph->v1 }, color });
		
		p.x += glyph->advance_x;
		cmd->indices_count += 6;
	}
}

} // namespace black