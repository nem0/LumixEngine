#pragma once


#include "lumix.h"
#include "core/vec.h"


namespace Lumix
{

	class LUMIX_RENDERER_API RayCastModelHit
	{
		public:
			bool m_is_hit;
			float m_t;
			Vec3 m_origin;
			Vec3 m_dir;
			class Mesh* m_mesh;
			ComponentIndex m_component;
			Entity m_entity;
			uint32 m_component_type;
	};

} // ~ namespace Lumix
