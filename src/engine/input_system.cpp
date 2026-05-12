#include "engine/input_system.h"
#include "core/array.h"
#include "core/delegate.h"
#include "core/delegate_list.h"
#include "core/log.h"
#include "core/math.h"
#include "core/os.h"
#include "core/profiler.h"
#include "core/string.h"
#include "engine/engine.h"

namespace Lumix {


struct MouseDevice : InputSystem::Device {
	void update(float dt) override {}
	const char* getName() const override { return "mouse"; }
};


struct KeyboardDevice : InputSystem::Device {
	void update(float dt) override {}
	const char* getName() const override { return "keyboard"; }
};


struct GamepadDevice : InputSystem::Device {
	GamepadDevice(os::GamepadUID uid) : uid(uid) {}

	void update(float dt) override {}

	const char* getName() const override { return "gamepad"; }

	os::GamepadUID uid;
};


struct InputSystemImpl final : InputSystem {
	explicit InputSystemImpl(Engine& engine)
		: m_engine(engine)
		, m_allocator(engine.getAllocator())
		, m_events(m_allocator)
		, m_devices(m_allocator)
		, m_to_remove(m_allocator)
		, m_down_keys(m_allocator)
		, m_gamepad_devices(m_allocator) {
		m_mouse_device = LUMIX_NEW(m_allocator, MouseDevice);
		m_mouse_device->type = Device::MOUSE;
		m_keyboard_device = LUMIX_NEW(m_allocator, KeyboardDevice);
		m_keyboard_device->type = Device::KEYBOARD;
		m_devices.push(m_keyboard_device);
		m_devices.push(m_mouse_device);
	}


	~InputSystemImpl() {
		for (Device* device : m_devices) {
			LUMIX_DELETE(m_allocator, device);
		}
	}


	IAllocator& getAllocator() override { return m_allocator; }


	void addDevice(Device* device) override {
		m_devices.push(device);
		Event event;
		event.type = Event::DEVICE_ADDED;
		event.device = device;
		injectEvent(event);
	}


	void removeDevice(Device* device) override {
		ASSERT(device != m_keyboard_device);
		ASSERT(device != m_mouse_device);
		m_to_remove.push(device);

		Event event;
		event.type = Event::DEVICE_REMOVED;
		event.device = device;
		injectEvent(event);
	}


	void update(float dt) override {
		PROFILE_FUNCTION();

		for (Device* device : m_to_remove) {
			m_devices.eraseItem(device);
			LUMIX_DELETE(m_allocator, device);
		}
		m_to_remove.clear();

		m_events.clear();

		for (Device* device : m_devices) device->update(dt);
	}

	void injectEvent(const os::Event& event, int mouse_base_x, int mouse_base_y) override {
		switch (event.type) {
			case os::Event::Type::GAMEPAD_ADDED: {
				getOrCreateGamepadDevice(event.gamepad_added.gamepad);
				break;
			}
			case os::Event::Type::GAMEPAD_REMOVED: {
				GamepadDevice* device = findGamepadDevice(event.gamepad_removed.gamepad);
				if (device) {
					removeDevice(device);
					m_gamepad_devices.eraseItem(device);
				}
				break;
			}
			case os::Event::Type::GAMEPAD_AXIS: {
				// Arrival events are not guaranteed to be seen before the first axis/button event.
				GamepadDevice* device = getOrCreateGamepadDevice(event.gamepad_axis.gamepad);

				Event input_event;
				input_event.type = Event::AXIS;
				input_event.device = device;
				input_event.data.axis.x_abs = 0;
				input_event.data.axis.y_abs = 0;
				input_event.data.axis.x = event.gamepad_axis.x;
				input_event.data.axis.y = event.gamepad_axis.y;
				switch (event.gamepad_axis.axis) {
					case os::GamepadAxis::LTRIGGER: input_event.data.axis.axis = AxisEvent::LTRIGGER; break;
					case os::GamepadAxis::RTRIGGER: input_event.data.axis.axis = AxisEvent::RTRIGGER; break;
					case os::GamepadAxis::LTHUMB: input_event.data.axis.axis = AxisEvent::LTHUMB; break;
					case os::GamepadAxis::RTHUMB: input_event.data.axis.axis = AxisEvent::RTHUMB; break;
				}
				injectEvent(input_event);
				break;
			}
			case os::Event::Type::GAMEPAD_BUTTON: {
				// Arrival events are not guaranteed to be seen before the first axis/button event.
				GamepadDevice* device = getOrCreateGamepadDevice(event.gamepad_button.gamepad);

				Event input_event;
				input_event.type = Event::BUTTON;
				input_event.device = device;
				input_event.data.button.key_id = (int)event.gamepad_button.button;
				input_event.data.button.is_repeat = false;
				input_event.data.button.down = event.gamepad_button.down;
				input_event.data.button.x = 0;
				input_event.data.button.y = 0;
				injectEvent(input_event);
				break;
			}
			case os::Event::Type::MOUSE_BUTTON: {
				Event input_event;
				input_event.type = Event::BUTTON;
				input_event.device = m_mouse_device;
				input_event.data.button.key_id = (int)event.mouse_button.button;
				input_event.data.button.is_repeat = false;
				input_event.data.button.down = event.mouse_button.down;
				const os::Point cp = os::getMouseScreenPos();
				input_event.data.button.x = (float)cp.x - mouse_base_x;
				input_event.data.button.y = (float)cp.y - mouse_base_y;
				injectEvent(input_event);
				break;
			}
			case os::Event::Type::MOUSE_MOVE: {
				Event input_event;
				input_event.type = Event::AXIS;
				input_event.device = m_mouse_device;
				const os::Point cp = os::getMouseScreenPos();
				input_event.data.axis.x_abs = (float)cp.x - mouse_base_x;
				input_event.data.axis.y_abs = (float)cp.y - mouse_base_y;
				input_event.data.axis.x = (float)event.mouse_move.xrel;
				input_event.data.axis.y = (float)event.mouse_move.yrel;
				injectEvent(input_event);
				break;
			}
			case os::Event::Type::MOUSE_WHEEL: {
				Event input_event;
				input_event.type = Event::MOUSE_WHEEL;
				input_event.device = m_mouse_device;
				input_event.data.mouse_wheel.x = 0; // TODO
				input_event.data.mouse_wheel.y = (float)event.mouse_wheel.amount;
				injectEvent(input_event);
				break;
			}
			case os::Event::Type::KEY: {
				Event input_event;
				input_event.type = Event::BUTTON;
				input_event.device = m_keyboard_device;
				input_event.data.button.down = event.key.down;
				input_event.data.button.key_id = (int)event.key.keycode;
				input_event.data.button.is_repeat = (int)event.key.is_repeat;
				injectEvent(input_event);
				if (event.key.down) {
					m_down_keys.push(input_event.data.button);
				} else {
					for (i32 i = m_down_keys.size() - 1; i >= 0; --i) {
						if (m_down_keys[i].key_id == input_event.data.button.key_id) {
							m_down_keys.swapAndPop(i);
						}
					}
				}
				break;
			}
			case os::Event::Type::CHAR: {
				Event input_event;
				input_event.type = Event::TEXT_INPUT;
				input_event.device = m_keyboard_device;
				input_event.data.text.utf8 = event.text_input.utf8;
				injectEvent(input_event);
				break;
			}
			default: break;
		}
	}

	void resetDownKeys() override {
		for (const ButtonEvent& e : m_down_keys) {
			Event event;
			event.type = Event::BUTTON;
			event.device = m_keyboard_device;
			event.data.button.down = false;
			event.data.button.x = e.x;
			event.data.button.y = e.y;
			event.data.button.key_id = e.key_id;
			event.data.button.is_repeat = false;
			injectEvent(event);
		}
	}

	void injectEvent(const Event& event) override { m_events.push(event); }

	Span<const Event> getEvents() const override { return m_events; }

	Span<Device*> getDevices() override { return m_devices; }

private:
	Engine& m_engine;
	IAllocator& m_allocator;
	MouseDevice* m_mouse_device;
	KeyboardDevice* m_keyboard_device;
	Array<Event> m_events;
	Array<Device*> m_devices;
	Array<Device*> m_to_remove;
	Array<ButtonEvent> m_down_keys;
	Array<GamepadDevice*> m_gamepad_devices;

	GamepadDevice* findGamepadDevice(os::GamepadUID uid) {
		for (GamepadDevice* device : m_gamepad_devices) {
			if (device->uid == uid) return device;
		}
		return nullptr;
	}

	GamepadDevice* getOrCreateGamepadDevice(os::GamepadUID uid) {
		GamepadDevice* device = findGamepadDevice(uid);
		if (device) return device;

		device = LUMIX_NEW(m_allocator, GamepadDevice)(uid);
		device->type = Device::GAMEPAD;
		m_gamepad_devices.push(device);
		addDevice(device);
		return device;
	}
};


UniquePtr<InputSystem> InputSystem::create(Engine& engine) {
	return UniquePtr<InputSystemImpl>::create(engine.getAllocator(), engine);
}


} // namespace Lumix
