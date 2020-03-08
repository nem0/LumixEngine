#pragma once


#include "engine/lumix.h"
#include "engine/os.h"


namespace Lumix
{

struct LUMIX_ENGINE_API InputSystem
{
	struct Device
	{
		enum Type : u32
		{
			MOUSE,
			KEYBOARD,
			CONTROLLER
		};

		Type type;
		u32 index = 0;

		virtual ~Device() {}
		virtual void update(float dt) = 0;
		virtual const char* getName() const = 0;
	};

	struct ButtonEvent
	{
		u32 key_id;
		float x;
		float y;
		bool down;
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

	struct TextEvent
	{
		u32 utf8;
	};

	struct Event
	{
		enum Type : u32
		{
			BUTTON,
			AXIS,
			TEXT_INPUT,
			DEVICE_ADDED,
			DEVICE_REMOVED
		};

		Type type;
		Device* device;
		union EventData
		{
			ButtonEvent button;
			AxisEvent axis;
			TextEvent text;
		} data;
	};

	static InputSystem* create(struct Engine& engine);
	static void destroy(InputSystem& system);

	virtual ~InputSystem() {}
	virtual struct IAllocator& getAllocator() = 0;
	virtual void update(float dt) = 0;

	virtual void injectEvent(const Event& event) = 0;
	virtual void injectEvent(const OS::Event& event, int mouse_base_x, int mouse_base_y) = 0;
	virtual int getEventsCount() const = 0;
	virtual const Event* getEvents() const = 0;

	virtual void addDevice(Device* device) = 0;
	virtual void removeDevice(Device* device) = 0;
	virtual int getDevicesCount() const = 0;
	virtual Device* getDevice(int index) = 0;
};



} // namespace Lumix
