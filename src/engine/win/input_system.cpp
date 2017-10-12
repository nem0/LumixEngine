#include "engine/input_system.h"
#include "engine/associative_array.h"
#include "engine/delegate.h"
#include "engine/delegate_list.h"
#include "engine/profiler.h"
#include "engine/string.h"
#include "engine/vec.h"

#include <windows.h>
#include <Xinput.h>


namespace Lumix
{


static const float DEADZONE = 0.2f;


struct InputSystemImpl LUMIX_FINAL : public InputSystem
{
	typedef decltype(XInputGetState)* XInputGetState_fn_ptr;

	explicit InputSystemImpl(IAllocator& allocator)
		: m_allocator(allocator)
		, m_events(m_allocator)
		, m_is_enabled(false)
		, m_xinput_library(nullptr)
		, m_xinput_get_state(nullptr)
	{
		m_mouse_device.type = Device::MOUSE;
		m_keyboard_device.type = Device::KEYBOARD;
		m_last_checked_controller = 0;
		for (int i = 0; i < lengthOf(m_xinput_connected); ++i)
		{
			m_xinput_connected[i] = false;
		}
		m_xinput_library = LoadLibrary("Xinput9_1_0.dll");
		if (m_xinput_library)
		{
			m_xinput_get_state = (XInputGetState_fn_ptr)GetProcAddress(m_xinput_library, "XInputGetState");
			if (!m_xinput_get_state)
			{
				FreeLibrary(m_xinput_library);
				m_xinput_library = nullptr;
			}
		}
	}


	~InputSystemImpl()
	{
		if (m_xinput_library) FreeLibrary(m_xinput_library);
	}


	void enable(bool enabled) override { m_is_enabled = enabled; }


	void update(float) override
	{
		PROFILE_FUNCTION();

		m_events.clear();
		if (m_xinput_get_state)
		{
			for (int i = 0; i < XUSER_MAX_COUNT; ++i)
			{
				if (m_xinput_connected[i] || i == m_last_checked_controller)
				{
					auto status = m_xinput_get_state(i, &m_xinput_states[i]);
					m_xinput_connected[i] = status == ERROR_SUCCESS;
				}
			}
			m_last_checked_controller = (m_last_checked_controller + 1) % XUSER_MAX_COUNT;
		}
	}

	
	void injectEvent(const Event& event) override
	{
		m_events.push(event);
	}


	int getEventsCount() const override { return m_events.size(); }
	const Event* getEvents() const override { return m_events.empty() ? nullptr : &m_events[0]; }

	Vec2 getCursorPosition() const override { return m_cursor_pos; }
	void setCursorPosition(const Vec2& pos) override { m_cursor_pos = pos; }

	static float deadZone(float value, float dead_zone)
	{
		if (value < dead_zone && value > -dead_zone) return 0;
		return value;
	}


	Device* getMouseDevice() override
	{
		return &m_mouse_device;
	}


	Device* getKeyboardDevice() override
	{
		return &m_keyboard_device;
	}


	Device m_mouse_device;
	Device m_keyboard_device;
	IAllocator& m_allocator;
	Array<Event> m_events;
	bool m_is_enabled;
	Vec2 m_cursor_pos;
	HMODULE m_xinput_library;
	XInputGetState_fn_ptr m_xinput_get_state;
	XINPUT_STATE m_xinput_states[XUSER_MAX_COUNT];
	bool m_xinput_connected[XUSER_MAX_COUNT];
	u32 m_last_checked_controller;
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
