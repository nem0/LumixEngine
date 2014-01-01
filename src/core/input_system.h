#pragma once


#include "core/lux.h"


namespace Lux
{

	/* action type
		key pressed
		key down
		key down duration
	*/

	class LUX_CORE_API InputSystem
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
			bool create();
			void destroy();

			void update(float dt);
			float getActionValue(uint32_t action);
			void injectMouseXMove(float value);
			void injectMouseYMove(float value);
			void addAction(uint32_t action, InputType type, int key);

		private:
			struct InputSystemImpl* m_impl;
	};



} // ~namespace Lux