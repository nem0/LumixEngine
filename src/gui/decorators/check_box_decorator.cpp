#include "gui/decorators/check_box_decorator.h"
#include "gui/atlas.h"
#include "gui/block.h"
#include "gui/controls/check_box.h"
#include "gui/gui.h"
#include "gui/irenderer.h"
#include "gui/texture_base.h"


namespace Lux
{
namespace UI
{

	void CheckBoxDecorator::setVertices(Vec3* verts, float left, float top, float right, float bottom, float z) const
	{
		verts[0].set(left, top, z);
		verts[1].set(left, bottom, z);
		verts[2].set(right, bottom, z);
		verts[3].set(left, top, z);
		verts[4].set(right, bottom, z);
		verts[5].set(right, top, z);
	}

		
	void CheckBoxDecorator::render(IRenderer& renderer, Block& block)
	{
		m_parts[0] = m_parts[0] ? m_parts[0] : m_atlas->getPart("checkbox");
		m_parts[1] = m_parts[1] ? m_parts[1] : m_atlas->getPart("checkbox_checked");

		int i = static_cast<CheckBox&>(block).isChecked() ? 1 : 0;
		if(m_parts[i])
		{
			setVertices(&m_vertices[0], block.getGlobalLeft(), block.getGlobalTop(), block.getGlobalRight(), block.getGlobalBottom(), block.getZ());
			m_parts[i]->getUvs(&m_uvs[0]);

			if(m_atlas->getTexture())
			{
				renderer.renderImage(m_atlas->getTexture(), &m_vertices[0].x, m_uvs, 54);
			}
		}
	}


	bool CheckBoxDecorator::create(Gui& gui, const char* atlas)
	{
		m_parts[0] = NULL;
		m_parts[1] = NULL;
		m_atlas = gui.loadAtlas(atlas);
		return m_atlas != NULL;
	}


} // ~namespace UI
} // ~namespace Lux