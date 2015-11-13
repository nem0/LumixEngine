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
				MOUSE_Y
			};

		public:
			InputSystem();

			bool create(IAllocator& allocator);
			void destroy();

			void enable(bool enabled);
			void update(float dt);
			float getActionValue(uint32 action);
			void injectMouseXMove(float value);
			void injectMouseYMove(float value);
			void addAction(uint32 action, InputType type, int key);

		private:
			struct InputSystemImpl* m_impl;
	};



} // ~namespace Lumix
