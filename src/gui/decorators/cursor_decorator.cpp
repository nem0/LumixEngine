#include "gui/decorators/cursor_decorator.h"
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

	void CursorDecorator::setVertices(Vec3* verts, float left, float top, float right, float bottom, float z) const
	{
		verts[0].set(left, top, z);
		verts[1].set(left, bottom, z);
		verts[2].set(right, bottom, z);
		verts[3].set(left, top, z);
		verts[4].set(right, bottom, z);
		verts[5].set(right, top, z);
	}

		
	void CursorDecorator::render(IRenderer& renderer, Block& block)
	{
		m_part = m_part ? m_part : m_atlas->getPart("cursor");

		if(m_part)
		{
			setVertices(m_vertices, block.getGlobalLeft(), block.getGlobalTop(), block.getGlobalLeft() + m_part->m_pixel_width, block.getGlobalBottom(), block.getZ()); 
			m_part->getUvs(&m_uvs[0]);
			if(m_atlas->getTexture())
			{
				renderer.renderImage(m_atlas->getTexture(), &m_vertices[0].x, m_uvs, 6);
			}
		}
	}


	bool CursorDecorator::create(Gui& gui, const char* atlas)
	{
		m_part = NULL;
		m_atlas = gui.loadAtlas(FS::Path(atlas, gui.getEngine()->getFileSystem()));
		return m_atlas != NULL;
	}


} // ~namespace UI
} // ~namespace Lux