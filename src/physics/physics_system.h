#pragma once


#include "core/lux.h"


namespace Lux
{


class LUX_ENGINE_API PhysicsSystem
{
	friend class PhysicsScene;
	friend struct PhysicsSceneImpl;
	public:
		PhysicsSystem() { m_impl = 0; }
		
		bool create();
		void destroy();

	private:
		struct PhysicsSystemImpl* m_impl;
};


} // !namespace Lux
