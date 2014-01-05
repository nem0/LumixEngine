#include "gui/decorators/box_decorator.h"
#include "core/path.h"
#include "engine/engine.h"
#include "gui/atlas.h"
#include "gui/block.h"
#include "gui/gui.h"
#include "gui/irenderer.h"
#include "gui/texture_base.h"


namespace Lux
{
namespace UI
{

	void BoxDecorator::setVertices(Vec3* verts, float left, float top, float right, float bottom, float z) const
	{
		verts[0].set(left, top, z);
		verts[1].set(left, bottom, z);
		verts[2].set(right, bottom, z);
		verts[3].set(left, top, z);
		verts[4].set(right, bottom, z);
		verts[5].set(right, top, z);
	}

		
	void BoxDecorator::render(IRenderer& renderer, Block& block)
	{
		m_parts[0] = m_parts[0] ? m_parts[0] : m_atlas->getPart("box_topleft");
		m_parts[1] = m_parts[1] ? m_parts[1] : m_atlas->getPart("box_topcenter");
		m_parts[2] = m_parts[2] ? m_parts[2] : m_atlas->getPart("box_topright");
		m_parts[3] = m_parts[3] ? m_parts[3] : m_atlas->getPart("box_middleleft");
		m_parts[4] = m_parts[4] ? m_parts[4] : m_atlas->getPart("box_middlecenter");
		m_parts[5] = m_parts[5] ? m_parts[5] : m_atlas->getPart("box_middleright");
		m_parts[6] = m_parts[6] ? m_parts[6] : m_atlas->getPart("box_bottomleft");
		m_parts[7] = m_parts[7] ? m_parts[7] : m_atlas->getPart("box_bottomcenter");
		m_parts[8] = m_parts[8] ? m_parts[8] : m_atlas->getPart("box_bottomright");

		if(m_parts[0])
		{
			float w = m_atlas->getTexture()->getWidth();
			float l = block.getGlobalLeft() + m_parts[0]->m_pixel_width;
			float r = block.getGlobalRight() - m_parts[2]->m_pixel_width;
			float t = block.getGlobalTop() + m_parts[0]->m_pixel_height;
			float b = block.getGlobalBottom() - m_parts[8]->m_pixel_height;

			setVertices(&m_vertices[0], block.getGlobalLeft(), block.getGlobalTop(), l, t, block.getZ());
			setVertices(&m_vertices[6], l, block.getGlobalTop(), r, t, block.getZ());
			setVertices(&m_vertices[12], r, block.getGlobalTop(), block.getGlobalRight(), t, block.getZ());

			setVertices(&m_vertices[18], block.getGlobalLeft(), t, l, b, block.getZ());
			setVertices(&m_vertices[24], l, t, r, b, block.getZ());
			setVertices(&m_vertices[30], r, t, block.getGlobalRight(), b, block.getZ());

			setVertices(&m_vertices[36], block.getGlobalLeft(), b, l, block.getGlobalBottom(), block.getZ());
			setVertices(&m_vertices[42], l, b, r, block.getGlobalBottom(), block.getZ());
			setVertices(&m_vertices[48], r, b, block.getGlobalRight(), block.getGlobalBottom(), block.getZ());

			// uvs
			for(int i = 0; i < 9; ++i)
			{
				m_parts[i]->getUvs(&m_uvs[12 * i]);
			}

			if(m_atlas->getTexture())
			{
				renderer.renderImage(m_atlas->getTexture(), &m_vertices[0].x, m_uvs, 54);
			}
		}
	}


	bool BoxDecorator::create(Gui& gui, const char* atlas)
	{
		for(int i = 0; i < 9; ++i)
		{
			m_parts[i] = NULL;
		}
		m_atlas = gui.loadAtlas(FS::Path(atlas, gui.getEngine()->getFileSystem()));
		return m_atlas != NULL;
	}


} // ~namespace UI
} // ~namespace Lux