#pragma once


#include "core/lux.h"
#include "core/vec3.h"
#include "gui/atlas.h"
#include "gui/decorator_base.h"


namespace Lux
{
namespace UI
{

	class Gui;
	class IRenderer;
	class TextureBase;


	class LUX_GUI_API CheckBoxDecorator : public DecoratorBase
	{
		public:
			CheckBoxDecorator(const char* name) : DecoratorBase(name) {}

			bool create(Gui& gui, const char* atlas);
			virtual void render(IRenderer& renderer, Block& block) override;

		private:
			void setVertices(Vec3* verts, float left, float top, float right, float bottom, float z) const;

		private:
			Atlas* m_atlas;
			const Atlas::Part* m_parts[2];
			Vec3 m_vertices[6];
			float m_uvs[6];
	};

} // ~namespace UI
} // ~namespace Lux