#include "engine/input_system.h"
#include "core/delegate.h"
#include "core/delegate_list.h"
#include "core/gamepad.h"
#include "core/math.h"
#include "core/os.h"
#include "core/profiler.h"
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
	GamepadDevice(int controller_index)
		: controller_index(controller_index) {}

	void update(float dt) override {}

	const char* getName() const override { return "gamepad"; }

	int controller_index;
	GamepadState last_state = {};
};


struct InputSystemImpl final : InputSystem {
	explicit InputSystemImpl(Engine& engine)
		: m_engine(engine)
		, m_allocator(engine.getAllocator())
		, m_events(m_allocator)
		, m_devices(m_allocator)
		, m_to_remove(m_allocator)
		, m_down_keys(m_allocator)
		, m_gamepad_states(m_allocator)
		, m_gamepad_devices(m_allocator) {
		m_mouse_device = LUMIX_NEW(m_allocator, MouseDevice);
		m_mouse_device->type = Device::MOUSE;
		m_keyboard_device = LUMIX_NEW(m_allocator, KeyboardDevice);
		m_keyboard_device->type = Device::KEYBOARD;
		m_devices.push(m_keyboard_device);
		m_devices.push(m_mouse_device);

		// Initialize gamepad backend
		m_gamepad_backend = createGamepadBackend(m_allocator);
		if (m_gamepad_backend && m_gamepad_backend->init()) {
			int max_controllers = m_gamepad_backend->getMaxControllers();
			m_gamepad_states.resize(max_controllers);
			m_gamepad_devices.resize(max_controllers);

			// Initialize all states
			for (int i = 0; i < max_controllers; ++i) {
				m_gamepad_states[i] = {};
				m_gamepad_devices[i] = nullptr;
			}
		} else {
			// Cleanup if init failed
			if (m_gamepad_backend) {
				LUMIX_DELETE(m_allocator, m_gamepad_backend);
				m_gamepad_backend = nullptr;
			}
		}

		m_gamepad_last_checked = 0;
	}


	~InputSystemImpl() {
		// Clean up gamepad backend
		if (m_gamepad_backend) {
			m_gamepad_backend->shutdown();
			LUMIX_DELETE(m_allocator, m_gamepad_backend);
		}

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

		m_events.clear();

		for (Device* device : m_devices) device->update(dt);

		updateGamepads(dt);
	}

	void injectEvent(const os::Event& event, int mouse_base_x, int mouse_base_y) override {
		switch (event.type) {
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

	IGamepadBackend* m_gamepad_backend = nullptr;
	Array<GamepadState> m_gamepad_states;
	Array<GamepadDevice*> m_gamepad_devices;
	int m_gamepad_last_checked = 0;

	void updateGamepads(float dt) {
		if (!m_gamepad_backend) return;

		PROFILE_FUNCTION();

		int max_controllers = m_gamepad_backend->getMaxControllers();

		// Check one controller per frame for connection changes (performance optimization)
		for (int i = 0; i < max_controllers; ++i) {
			bool should_check = (i == m_gamepad_last_checked) || (m_gamepad_devices[i] != nullptr); // Always check connected devices

			if (should_check) {
				GamepadState new_state;
				bool success = m_gamepad_backend->updateController(i, new_state);
				bool is_connected = success && new_state.connected;

				// Handle connection changes
				if (is_connected && !m_gamepad_devices[i]) {
					// Controller connected
					GamepadDevice* device = LUMIX_NEW(m_allocator, GamepadDevice)(i);
					device->type = Device::CONTROLLER;
					device->last_state = new_state;
					m_gamepad_devices[i] = device;
					addDevice(device);
				} else if (!is_connected && m_gamepad_devices[i]) {
					// Controller disconnected
					removeDevice(m_gamepad_devices[i]);
					m_gamepad_devices[i] = nullptr;
				}

				// Update state and generate events for connected controllers
				if (is_connected && m_gamepad_devices[i]) {
					updateGamepadEvents(i, new_state);
					m_gamepad_states[i] = new_state;
					m_gamepad_devices[i]->last_state = new_state;
				}
			}
		}

		m_gamepad_last_checked = (m_gamepad_last_checked + 1) % max_controllers;
	}

	void updateGamepadEvents(int controller_index, const GamepadState& new_state) {
		const GamepadState& old_state = m_gamepad_states[controller_index];
		GamepadDevice* device = m_gamepad_devices[controller_index];

		// Only process if packet changed
		if (new_state.packet_number == old_state.packet_number) return;

		// Handle button changes
		if (new_state.buttons != old_state.buttons) {
			for (int i = 0; i < 16; ++i) {
				u16 mask = 1 << i;
				bool new_pressed = (new_state.buttons & mask) != 0;
				bool old_pressed = (old_state.buttons & mask) != 0;

				if (new_pressed != old_pressed) {
					Event event;
					event.type = Event::BUTTON;
					event.device = device;
					event.data.button.key_id = i;
					event.data.button.down = new_pressed;
					event.data.button.is_repeat = false;
					event.data.button.x = 0;
					event.data.button.y = 0;
					injectEvent(event);
				}
			}
		}

		// Handle trigger changes
		if (new_state.left_trigger != old_state.left_trigger) {
			Event event;
			event.type = Event::AXIS;
			event.device = device;
			event.data.axis.x = new_state.left_trigger;
			event.data.axis.y = 0;
			event.data.axis.x_abs = 0;
			event.data.axis.y_abs = 0;
			event.data.axis.axis = AxisEvent::LTRIGGER;
			injectEvent(event);
		}

		if (new_state.right_trigger != old_state.right_trigger) {
			Event event;
			event.type = Event::AXIS;
			event.device = device;
			event.data.axis.x = new_state.right_trigger;
			event.data.axis.y = 0;
			event.data.axis.x_abs = 0;
			event.data.axis.y_abs = 0;
			event.data.axis.axis = AxisEvent::RTRIGGER;
			injectEvent(event);
		}

		// Handle stick changes
		if (new_state.left_stick.x != old_state.left_stick.x || new_state.left_stick.y != old_state.left_stick.y) {
			Event event;
			event.type = Event::AXIS;
			event.device = device;
			event.data.axis.x = new_state.left_stick.x;
			event.data.axis.y = new_state.left_stick.y;
			event.data.axis.x_abs = 0;
			event.data.axis.y_abs = 0;
			event.data.axis.axis = AxisEvent::LTHUMB;
			injectEvent(event);
		}

		if (new_state.right_stick.x != old_state.right_stick.x || new_state.right_stick.y != old_state.right_stick.y) {
			Event event;
			event.type = Event::AXIS;
			event.device = device;
			event.data.axis.x = new_state.right_stick.x;
			event.data.axis.y = new_state.right_stick.y;
			event.data.axis.x_abs = 0;
			event.data.axis.y_abs = 0;
			event.data.axis.axis = AxisEvent::RTHUMB;
			injectEvent(event);
		}
	}
};


UniquePtr<InputSystem> InputSystem::create(Engine& engine) {
	return UniquePtr<InputSystemImpl>::create(engine.getAllocator(), engine);
}


} // namespace Lumix
