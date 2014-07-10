#pragma once


#include "core/lumix.h"
#include "core/vec3.h"
#include "gui/atlas.h"
#include "gui/decorator_base.h"


namespace Lumix
{
namespace UI
{

	class Gui;
	class IRenderer;
	class TextureBase;


	class LUMIX_GUI_API BoxDecorator : public DecoratorBase
	{
		public:
			BoxDecorator(const char* name) : DecoratorBase(name) {}

			bool create(Gui& gui, const char* atlas);
			virtual void render(IRenderer& renderer, Block& block) override;

		private:
			void setVertices(Vec3* verts, float left, float top, float right, float bottom, float z) const;

		private:
			Atlas* m_atlas;
			const Atlas::Part* m_parts[9];
			Vec3 m_vertices[108];
			float m_uvs[108];
	};

} // ~namespace UI
} // ~namespace Lumix
