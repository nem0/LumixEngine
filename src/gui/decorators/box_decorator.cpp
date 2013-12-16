#include "gui/decorators/box_decorator.h"
#include "gui/irenderer.h"
#include "gui/block.h"

namespace Lux
{
namespace UI
{

	
	void BoxDecorator::render(IRenderer& renderer, Block& block)
	{
		renderer.setScissorArea(block.getGlobalLeft(), block.getGlobalTop(), block.getGlobalRight(), block.getGlobalBottom());
		float z = 0;
		m_vertices[0].set((float)block.getGlobalLeft(), (float)block.getGlobalTop(), z);
		m_vertices[1].set((float)block.getGlobalLeft(), (float)block.getGlobalBottom(), z);
		m_vertices[2].set((float)block.getGlobalRight(), (float)block.getGlobalBottom(), z);
		
		m_vertices[3] = m_vertices[0];
		m_vertices[4] = m_vertices[2];
		m_vertices[5].set((float)block.getGlobalRight(), (float)block.getGlobalTop(), z);

		memset(m_uvs, 0, sizeof(int) * 108);

		m_uvs[0] = m_parts[0].m_x;
		m_uvs[1] = m_parts[0].m_y;

		m_uvs[2] = m_parts[0].m_x;
		m_uvs[3] = m_parts[0].m_y + m_parts[0].m_h;

		m_uvs[4] = m_parts[0].m_x + m_parts[0].m_w;
		m_uvs[5] = m_parts[0].m_y + m_parts[0].m_h;

		m_uvs[6] = m_parts[0].m_x;
		m_uvs[7] = m_parts[0].m_y;

		m_uvs[8] = m_parts[0].m_x + m_parts[0].m_w;
		m_uvs[9] = m_parts[0].m_y + m_parts[0].m_h;

		m_uvs[10] = m_parts[0].m_x + m_parts[0].m_w;
		m_uvs[11] = m_parts[0].m_y;

		renderer.renderImage(m_image, &m_vertices[0].x, m_uvs, 6);
	}


	bool BoxDecorator::create(IRenderer& renderer, const char* image)
	{
		for(int i = 0; i < 9; ++i)
		{
			m_parts[i].m_x = 0;
			m_parts[i].m_y = 0;
			m_parts[i].m_w = 0;
			m_parts[i].m_h = 0;
		}
		renderer.loadImage(image);
		return m_image >= 0;
	}


	void BoxDecorator::setPart(int part, float x, float y, float w, float h)
	{
		m_parts[part].m_x = x;
		m_parts[part].m_y = y;
		m_parts[part].m_w = w;
		m_parts[part].m_h = h;
	}


} // ~namespace UI
} // ~namespace Lux