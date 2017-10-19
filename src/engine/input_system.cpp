#include "engine/input_system.h"
#include "engine/associative_array.h"
#include "engine/controller_device.h"
#include "engine/delegate.h"
#include "engine/delegate_list.h"
#include "engine/profiler.h"
#include "engine/string.h"
#include "engine/vec.h"


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


struct InputSystemImpl LUMIX_FINAL : public InputSystem
{
	explicit InputSystemImpl(IAllocator& allocator)
		: m_allocator(allocator)
		, m_events(m_allocator)
		, m_is_enabled(false)
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
	void enable(bool enabled) override { m_is_enabled = enabled; }
	
	
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

	
	void injectEvent(const Event& event) override
	{
		m_events.push(event);
	}


	int getEventsCount() const override { return m_events.size(); }
	const Event* getEvents() const override { return m_events.empty() ? nullptr : &m_events[0]; }

	Vec2 getCursorPosition() const override { return m_cursor_pos; }
	void setCursorPosition(const Vec2& pos) override { m_cursor_pos = pos; }


	int getDevicesCount() const override { return m_devices.size(); }
	Device* getDevice(int index) override { return m_devices[index]; }
	Device* getMouseDevice() override { return m_mouse_device; }
	Device* getKeyboardDevice() override { return m_keyboard_device; }


	MouseDevice* m_mouse_device;
	KeyboardDevice* m_keyboard_device;
	IAllocator& m_allocator;
	Array<Event> m_events;
	bool m_is_enabled;
	Vec2 m_cursor_pos;
	Array<Device*> m_devices;
	Array<Device*> m_to_remove;
};


InputSystem* InputSystem::create(IAllocator& allocator)
{
	return LUMIX_NEW(allocator, InputSystemImpl)(allocator);
}


void InputSystem::destroy(InputSystem& system)
{
	auto* impl = static_cast<InputSystemImpl*>(&system);
	LUMIX_DELETE(impl->m_allocator, impl);
}


} // namespace Lumix
