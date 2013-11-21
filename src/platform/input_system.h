#pragma once


#include "core/lux.h"


namespace Lux
{

	/* action type
		key pressed
		key down
		key down duration
	*/

	class LUX_PLATFORM_API InputSystem
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
			float getActionValue(unsigned int action);
			void injectMouseXMove(float value);
			void injectMouseYMove(float value);
			void addAction(unsigned int action, InputType type, int key);

		private:
			struct InputSystemImpl* m_impl;
	};



} // ~namespace Lux