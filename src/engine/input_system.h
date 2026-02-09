#pragma once

#include "core/span.h"
#include "engine/lumix.h"


namespace Lumix {

template <typename T> struct UniquePtr;
namespace os { struct Event; }

struct LUMIX_ENGINE_API InputSystem {
	struct Device {
		enum Type : u32 {
			MOUSE,
			KEYBOARD,
			GAMEPAD
		};

		Type type;
		u32 index = 0;

		virtual ~Device() {}
		virtual void update(float dt) = 0;
		virtual const char* getName() const = 0;
	};

	struct ButtonEvent {
		u32 key_id;
		float x;
		float y;
		bool down;
		bool is_repeat;
	};

	struct AxisEvent {
		enum Axis : u32 {
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

	struct TextEvent {
		u32 utf8;
	};

	struct Event {
		enum Type : u32 {
			BUTTON,
			AXIS,
			TEXT_INPUT,
			DEVICE_ADDED,
			DEVICE_REMOVED
		};

		Type type;
		Device* device;
		union EventData {
			ButtonEvent button;
			AxisEvent axis;
			TextEvent text;
		} data;
	};

	static UniquePtr<InputSystem> create(struct Engine& engine);

	virtual ~InputSystem() {}
	virtual struct IAllocator& getAllocator() = 0;
	virtual void update(float dt) = 0;

	virtual void injectEvent(const Event& event) = 0;
	virtual void injectEvent(const os::Event& event, int mouse_base_x, int mouse_base_y) = 0;
	virtual Span<const Event> getEvents() const = 0;
	// emit up "fake" events for every key that's down, useful for when the window loses focus
	virtual void resetDownKeys() = 0;

	virtual void addDevice(Device* device) = 0;
	virtual void removeDevice(Device* device) = 0;
	virtual Span<Device*> getDevices() = 0;
};

} // namespace Lumix
