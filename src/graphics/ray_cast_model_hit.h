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
			ComponentOld m_component;
	};

} // ~ namespace Lumix
