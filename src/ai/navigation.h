#pragma once


#include "universe\universe.h"
#include "core/lux.h"


namespace Lux
{
	class LUX_ENGINE_API Navigation
	{
		public:
			Navigation();
			~Navigation();

			void navigate(Entity e, const Vec3& dest, float speed);
			bool load(const char path[]);
			void draw();
			void update(float dt);

		private:
			struct NavigationImpl* m_impl;
	};


}