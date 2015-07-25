#pragma once


#include "core/vec3.h"
#include "universe/universe.h"


namespace Lumix
{

	class  RayCastModelHit
	{
		public:
			bool m_is_hit;
			float m_t;
			Vec3 m_origin;
			Vec3 m_dir;
			class Mesh* m_mesh;
			ComponentIndex m_component;
			Entity m_entity;
			uint32_t m_component_type;
	};

} // ~ namespace Lumix
