#include "engine/input_system.h"
#include "core/os.h"
#include "engine/controller_device.h"
#include "core/delegate.h"
#include "core/delegate_list.h"
#include "engine/engine.h"
#include "core/profiler.h"
#include "core/math.h"


namespace Lumix
{


struct MouseDevice : InputSystem::Device
{
	void update(float dt) override {}
	const char* getName() const override { return "mouse"; }
};



struct KeyboardDevice : InputSystem::Device
{
	void update(float dt) override {}
	const char* getName() const override { return "keyboard"; }
};


struct InputSystemImpl final : InputSystem
{
	explicit InputSystemImpl(Engine& engine)
		: m_engine(engine)
		, m_allocator(engine.getAllocator())
		, m_events(m_allocator)
		, m_devices(m_allocator)
		, m_to_remove(m_allocator)
	{
		m_mouse_device = LUMIX_NEW(m_allocator, MouseDevice);
		m_mouse_device->type = Device::MOUSE;
		m_keyboard_device = LUMIX_NEW(m_allocator, KeyboardDevice);
		m_keyboard_device->type = Device::KEYBOARD;
		m_devices.push(m_keyboard_device);
		m_devices.push(m_mouse_device);
		ControllerDevice::init(*this);
	}


	~InputSystemImpl()
	{
		ControllerDevice::shutdown();
		for (Device* device : m_devices)
		{
			LUMIX_DELETE(m_allocator, device);
		}
	}


	IAllocator& getAllocator() override { return m_allocator; }
	
	
	void addDevice(Device* device) override
	{
		m_devices.push(device);
		Event event;
		event.type = Event::DEVICE_ADDED;
		event.device = device;
		injectEvent(event);
	}


	void removeDevice(Device* device) override 
	{ 
		ASSERT(device != m_keyboard_device);
		ASSERT(device != m_mouse_device);
		m_to_remove.push(device);

		Event event;
		event.type = Event::DEVICE_REMOVED;
		event.device = device;
		injectEvent(event);
	}


	void update(float dt) override
	{
		PROFILE_FUNCTION();

		for (Device* device : m_to_remove)
		{
			m_devices.eraseItem(device);
			LUMIX_DELETE(m_allocator, device);
		}

		m_events.clear();

		for (Device* device : m_devices) device->update(dt);
		ControllerDevice::frame(dt);
	}


	void injectEvent(const os::Event& event, int mouse_base_x, int mouse_base_y) override
	{
		switch (event.type) {
			case os::Event::Type::MOUSE_BUTTON: {
				InputSystem::Event input_event;
				input_event.type = InputSystem::Event::BUTTON;
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
				InputSystem::Event input_event;
				input_event.type = InputSystem::Event::AXIS;
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
				InputSystem::Event input_event;
				input_event.type = InputSystem::Event::BUTTON;
				input_event.device = m_keyboard_device;
				input_event.data.button.down = event.key.down;
				input_event.data.button.key_id = (int)event.key.keycode;
				input_event.data.button.is_repeat = (int)event.key.is_repeat;
				injectEvent(input_event);
				break;
			}
			case os::Event::Type::CHAR: {
				InputSystem::Event input_event;
				input_event.type = InputSystem::Event::TEXT_INPUT;
				input_event.device = m_keyboard_device;
				input_event.data.text.utf8 = event.text_input.utf8;
				injectEvent(input_event);
				break;
			}
			default: break;
		}
	}

	
	void injectEvent(const Event& event) override
	{
		m_events.push(event);
	}

	Span<const Event> getEvents() const override { return m_events; }

	Span<Device*> getDevices() override { return m_devices; }
	
	void registerLuaAPI();

	Engine& m_engine;
	IAllocator& m_allocator;
	MouseDevice* m_mouse_device;
	KeyboardDevice* m_keyboard_device;
	Array<Event> m_events;
	Array<Device*> m_devices;
	Array<Device*> m_to_remove;
};


UniquePtr<InputSystem> InputSystem::create(Engine& engine)
{
	return UniquePtr<InputSystemImpl>::create(engine.getAllocator(), engine);
}


} // namespace Lumix
