#pragma once


#include "lumix.h"
#include "core/vec.h"


namespace Lumix
{

class Model;


struct LUMIX_RENDERER_API RayCastModelHit
{
	bool m_is_hit;
	float m_t;
	Vec3 m_origin;
	Vec3 m_dir;
	Model* m_mesh;
	//ComponentIndex m_component;
	//Entity m_entity;
	//uint32 m_component_type;
};


} // namespace Lumix
