#pragma once


#include "lumix.h"


namespace Lumix
{

	class IAllocator;

	class LUMIX_ENGINE_API InputSystem
	{
		public:
			enum InputType 
			{
				PRESSED,
				DOWN,
				MOUSE_X,
				MOUSE_Y,
				LTHUMB_X,
				LTHUMB_Y,
				RTHUMB_X,
				RTHUMB_Y,
				RTRIGGER,
				LTRIGGER
			};

		public:

			static InputSystem* create(IAllocator& allocator);
			static void destroy(InputSystem& system);

			virtual ~InputSystem() {}
			virtual void enable(bool enabled) = 0;
			virtual void update(float dt) = 0;
			virtual float getActionValue(uint32 action) = 0;
			virtual void injectMouseXMove(float value) = 0;
			virtual void injectMouseYMove(float value) = 0;
			virtual void addAction(uint32 action, InputType type, int key, int controller_id) = 0;
	};



} // ~namespace Lumix
