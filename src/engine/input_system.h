#pragma once


#include "engine/lumix.h"


namespace Lumix
{

	class IAllocator;
	struct Vec2;
	template <typename T> class DelegateList;

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

			enum MouseButton
			{
				LEFT,
				MIDDLE,
				RIGHT
			};

		public:

			static InputSystem* create(IAllocator& allocator);
			static void destroy(InputSystem& system);

			virtual ~InputSystem() {}
			virtual void enable(bool enabled) = 0;
			virtual void update(float dt) = 0;
			virtual float getActionValue(u32 action) = 0;
			virtual void injectMouseXMove(float rel, float abs) = 0;
			virtual void injectMouseYMove(float rel, float abs) = 0;
			virtual float getMouseXMove() const = 0;
			virtual float getMouseYMove() const = 0;
			virtual bool  isMouseDown(MouseButton button) = 0;
			virtual Vec2 getMousePos() const = 0;
			virtual void addAction(u32 action, InputType type, int key, int controller_id) = 0;
	};



} // ~namespace Lumix
