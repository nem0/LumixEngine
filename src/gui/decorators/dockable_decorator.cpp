#include "gui/decorators/dockable_decorator.h"
#include "core/crc32.h"
#include "core/path.h"
#include "engine/engine.h"
#include "gui/atlas.h"
#include "gui/block.h"
#include "gui/controls/dockable.h"
#include "gui/gui.h"
#include "gui/irenderer.h"
#include "gui/texture_base.h"


namespace Lux
{
namespace UI
{

	static const uint32_t dockable_hash = crc32("dockable");

	void DockableDecorator::setVertices(Vec3* verts, float left, float top, float right, float bottom, float z) const
	{
		verts[0].set(left, top, z);
		verts[1].set(left, bottom, z);
		verts[2].set(right, bottom, z);
		verts[3].set(left, top, z);
		verts[4].set(right, bottom, z);
		verts[5].set(right, top, z);
	}


	void DockableDecorator::renderSlots(IRenderer& renderer, Dockable& destination)
	{
		float w = m_atlas->getTexture()->getWidth();
		float h_center = (float)(int)((destination.getGlobalLeft() + destination.getGlobalRight() - m_parts[0]->m_pixel_width) / 2);
		float v_center = (float)(int)((destination.getGlobalTop() + destination.getGlobalBottom() - m_parts[0]->m_pixel_height) / 2);

		setVertices(&m_vertices[0]
			, h_center
			, destination.getGlobalTop()
			, h_center + m_parts[0]->m_pixel_width
			, destination.getGlobalTop() + m_parts[0]->m_pixel_height
			, destination.getZ() + 0.1f
		);
		setVertices(&m_vertices[6]
			, h_center
			, destination.getGlobalBottom() - m_parts[0]->m_pixel_height
			, h_center + m_parts[0]->m_pixel_width
			, destination.getGlobalBottom() 
			, destination.getZ() + 0.1f
		);
		setVertices(&m_vertices[12]
			, destination.getGlobalLeft()
			, v_center
			, destination.getGlobalLeft() + m_parts[0]->m_pixel_width
			, v_center + m_parts[0]->m_pixel_height
			, destination.getZ() + 0.1f
		);
		setVertices(&m_vertices[18]
			, destination.getGlobalRight() - m_parts[0]->m_pixel_width
			, v_center
			, destination.getGlobalRight()
			, v_center + m_parts[0]->m_pixel_height
			, destination.getZ() + 0.1f
		);

		m_parts[0]->getUvs(&m_uvs[0]);
		m_parts[0]->getUvs(&m_uvs[12]);
		m_parts[0]->getUvs(&m_uvs[24]);
		m_parts[0]->getUvs(&m_uvs[36]);

		if(m_atlas->getTexture())
		{
			renderer.renderImage(m_atlas->getTexture(), &m_vertices[0].x, m_uvs, 24);
		}
	}

		
	void DockableDecorator::render(IRenderer& renderer, Block& block)
	{
		m_parts[0] = m_parts[0] ? m_parts[0] : m_atlas->getPart("dock_dest");

		if(m_parts[0])
		{
			Dockable& dockable = static_cast<Dockable&>(block);
			if(dockable.isDragged())
			{
				Lux::UI::Block* dest = dockable.getGui().getBlock(dockable.getDragX(), dockable.getDragY());
				while(dest)
				{
					if(dest->getType() == dockable_hash && dest != &dockable && dest != dockable.getContainingDockable())
					{
						renderSlots(renderer, static_cast<Dockable&>(*dest));
					}
					dest = dest->getParent();
				}
			}
		}
	}


	bool DockableDecorator::create(Gui& gui, const char* atlas)
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