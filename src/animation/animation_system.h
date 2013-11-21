#pragma once


#include "core/lux.h"
#include "universe/universe.h"


namespace Lux
{

	struct Entity;
	class Universe;
	class ISerializer;

	class LUX_ENGINE_API AnimationSystem
	{
		public:
			AnimationSystem() { m_impl = 0; }

			bool create();
			void destroy();

			void setUniverse(Universe* universe);

			Component createAnimable(const Entity& entity);
			void playAnimation(const Component& cmp, const char* path);
			void setAnimationTime(const Component& cmp, float time);
			void update(float time_delta);
		
			void serialize(ISerializer& serializer);
			void deserialize(ISerializer& serializer);

		private:
			struct AnimationSystemImpl* m_impl;
	};


}// ~ namespace Lux 