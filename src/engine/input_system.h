#pragma once


#include "engine/lumix.h"


namespace Lumix
{

class Engine;
struct IAllocator;
struct Vec2;

class LUMIX_ENGINE_API InputSystem
{
	public:
		struct Device
		{
			enum Type : u32
			{
				MOUSE,
				KEYBOARD,
				CONTROLLER
			};

			Type type;
			int index = 0;

			virtual ~Device() {}
			virtual void update(float dt) = 0;
			virtual const char* getName() const = 0;
		};

		struct ButtonEvent
		{
			u32 key_id;
			u32 scancode;
			float x_abs;
			float y_abs;
			enum : u32
			{
				UP,
				DOWN
			} state;
		};

		struct AxisEvent
		{
			enum Axis
			{
				LTRIGGER,
				RTRIGGER,
				LTHUMB,
				RTHUMB
			};

			float x;
			float y;
			float x_abs;
			float y_abs;
			Axis axis;
		};

		struct Event
		{
			enum Type : u32
			{
				BUTTON,
				AXIS,
				DEVICE_ADDED,
				DEVICE_REMOVED
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
		static InputSystem* create(Engine& engine);
		static void destroy(InputSystem& system);

		virtual ~InputSystem() {}
		virtual IAllocator& getAllocator() = 0;
		virtual void enable(bool enabled) = 0;
		virtual void update(float dt) = 0;

		virtual void injectEvent(const Event& event) = 0;
		virtual int getEventsCount() const = 0;
		virtual const Event* getEvents() const = 0;

		virtual void addDevice(Device* device) = 0;
		virtual void removeDevice(Device* device) = 0;
		virtual Device* getMouseDevice() = 0;
		virtual Device* getKeyboardDevice() = 0;
		virtual int getDevicesCount() const = 0;
		virtual Device* getDevice(int index) = 0;

		virtual Vec2 getCursorPosition() const = 0;
		virtual void setCursorPosition(const Vec2& pos) = 0;
};



} // namespace Lumix
