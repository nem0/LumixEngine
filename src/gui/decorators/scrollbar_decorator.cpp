#include "gui/decorators/scrollbar_decorator.h"
#include "gui/atlas.h"
#include "gui/block.h"
#include "gui/controls/scrollbar.h"
#include "gui/gui.h"
#include "gui/irenderer.h"
#include "gui/texture_base.h"


namespace Lumix
{
namespace UI
{

	void ScrollbarDecorator::setVertices(Vec3* verts, float left, float top, float right, float bottom, float z) const
	{
		verts[0].set(left, top, z);
		verts[1].set(left, bottom, z);
		verts[2].set(right, bottom, z);
		verts[3].set(left, top, z);
		verts[4].set(right, bottom, z);
		verts[5].set(right, top, z);
	}

		
	void ScrollbarDecorator::render(IRenderer& renderer, Block& block)
	{
		m_parts[HORIZONTAL_BEGIN] = m_parts[HORIZONTAL_BEGIN] ? m_parts[HORIZONTAL_BEGIN] : m_atlas->getPart("scrollbar_hbegin");
		m_parts[HORIZONTAL_CENTER] = m_parts[HORIZONTAL_CENTER] ? m_parts[HORIZONTAL_CENTER] : m_atlas->getPart("scrollbar_hcenter");
		m_parts[HORIZONTAL_END] = m_parts[HORIZONTAL_END] ? m_parts[HORIZONTAL_END] : m_atlas->getPart("scrollbar_hend");
		m_parts[VERTICAL_BEGIN] = m_parts[VERTICAL_BEGIN] ? m_parts[VERTICAL_BEGIN] : m_atlas->getPart("scrollbar_vbegin");
		m_parts[VERTICAL_CENTER] = m_parts[VERTICAL_CENTER] ? m_parts[VERTICAL_CENTER] : m_atlas->getPart("scrollbar_vcenter");
		m_parts[VERTICAL_END] = m_parts[VERTICAL_END] ? m_parts[VERTICAL_END] : m_atlas->getPart("scrollbar_vend");
		m_parts[SLIDER] = m_parts[SLIDER] ? m_parts[SLIDER] : m_atlas->getPart("scrollbar_slider");

		Scrollbar& scrollbar = static_cast<Scrollbar&>(block);

		if(m_parts[HORIZONTAL_BEGIN])
		{
			if(scrollbar.getScrollbarType() == Scrollbar::HORIZONTAL)
			{
				float w = m_atlas->getTexture()->getWidth();
				float l = block.getGlobalLeft() + m_parts[HORIZONTAL_BEGIN]->m_pixel_width;
				float r = block.getGlobalRight() - m_parts[HORIZONTAL_END]->m_pixel_width;

				setVertices(&m_vertices[0], block.getGlobalLeft(), block.getGlobalTop(), l, block.getGlobalBottom(), block.getZ());
				setVertices(&m_vertices[6], l, block.getGlobalTop(), r, block.getGlobalBottom(), block.getZ());
				setVertices(&m_vertices[12], r, block.getGlobalTop(), block.getGlobalRight(), block.getGlobalBottom(), block.getZ());

				m_parts[HORIZONTAL_BEGIN]->getUvs(&m_uvs[0]);
				m_parts[HORIZONTAL_CENTER]->getUvs(&m_uvs[12]);
				m_parts[HORIZONTAL_END]->getUvs(&m_uvs[24]);
			}
			else 
			{
				float w = m_atlas->getTexture()->getWidth();
				float t = block.getGlobalTop() + m_parts[VERTICAL_BEGIN]->m_pixel_height;
				float b = block.getGlobalBottom() - m_parts[VERTICAL_END]->m_pixel_height;

				setVertices(&m_vertices[0], block.getGlobalLeft(), block.getGlobalTop(), block.getGlobalRight(), t, block.getZ());
				setVertices(&m_vertices[6], block.getGlobalLeft(), t, block.getGlobalRight(), b, block.getZ());
				setVertices(&m_vertices[12], block.getGlobalLeft(), b, block.getGlobalRight(), block.getGlobalBottom(), block.getZ());

				m_parts[VERTICAL_BEGIN]->getUvs(&m_uvs[0]);
				m_parts[VERTICAL_CENTER]->getUvs(&m_uvs[12]);
				m_parts[VERTICAL_END]->getUvs(&m_uvs[24]);

			}
			if(m_parts[SLIDER])
			{
				Block::Area& area = scrollbar.getSliderUI().getGlobalArea();
				setVertices(&m_vertices[18], area.left, area.top, area.right, area.bottom, scrollbar.getSliderUI().getZ());
				m_parts[SLIDER]->getUvs(&m_uvs[36]);
				if(m_atlas->getTexture())
				{
					renderer.renderImage(m_atlas->getTexture(), &m_vertices[0].x, m_uvs, 24);
				}
			}
		}
	}


	bool ScrollbarDecorator::create(Gui& gui, const char* atlas)
	{
		for(int i = 0; i < PARTS_COUNT; ++i)
		{
			m_parts[i] = NULL;
		}
		m_atlas = gui.loadAtlas(atlas);
		return m_atlas != NULL;
	}


} // ~namespace UI
} // ~namespace Lumix
