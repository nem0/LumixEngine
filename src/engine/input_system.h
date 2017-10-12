#pragma once


#include "engine/lumix.h"


namespace Lumix
{

struct IAllocator;
struct Vec2;

class LUMIX_ENGINE_API InputSystem
{
	public:
		struct Device
		{
			enum Type
			{
				MOUSE,
				KEYBOARD,
				CONTROLLER
			};

			Type type;
		};

		struct ButtonEvent
		{
			u32 key_id;
			float x_abs;
			float y_abs;
			enum
			{
				UP,
				DOWN
			} state;
		};

		struct AxisEvent
		{
			float x;
			float y;
			float x_abs;
			float y_abs;
		};

		struct Event
		{
			enum Type : u32
			{
				BUTTON,
				AXIS
			};

			Type type;
			Device* device;
			union EventData
			{
				ButtonEvent button;
				AxisEvent axis;
			} data;
		};

	public:
		static InputSystem* create(IAllocator& allocator);
		static void destroy(InputSystem& system);

		virtual ~InputSystem() {}
		virtual void enable(bool enabled) = 0;
		virtual void update(float dt) = 0;

		virtual void injectEvent(const Event& event) = 0;
		virtual int getEventsCount() const = 0;
		virtual const Event* getEvents() const = 0;

		virtual Device* getMouseDevice() = 0;
		virtual Device* getKeyboardDevice() = 0;

		virtual Vec2 getCursorPosition() const = 0;
		virtual void setCursorPosition(const Vec2& pos) = 0;
};



} // namespace Lumix
