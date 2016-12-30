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
		: m_actions(allocator)
		, m_allocator(allocator)
		, m_is_enabled(false)
		, m_mouse_rel_pos(0, 0)
		, m_injected_mouse_rel_pos(0, 0)
		, m_xinput_library(nullptr)
		, m_xinput_get_state(nullptr)
	{
		m_last_checked_controller = 0;
		for (int i = 0; i < Lumix::lengthOf(m_xinput_connected); ++i)
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
		m_mouse_rel_pos = m_injected_mouse_rel_pos;
		m_injected_mouse_rel_pos = {0, 0};
	}


	void injectMouseXMove(float rel, float abs) override
	{
		m_injected_mouse_rel_pos.x = rel;
		m_mouse_pos.x = abs;
	}


	void injectMouseYMove(float rel, float abs) override
	{
		m_injected_mouse_rel_pos.y = rel;
		m_mouse_pos.y = abs;
	}


	void addAction(u32 action, InputType type, int key, int controller_id) override
	{
		InputSystemImpl::Action value;
		value.key = key;
		value.type = type;
		value.controller_id = controller_id;
		m_actions[action] = value;
	}


	static float deadZone(float value, float dead_zone)
	{
		if (value < dead_zone && value > -dead_zone) return 0;
		return value;
	}


	float getMouseXMove() const override { return m_mouse_rel_pos.x; }
	float getMouseYMove() const override { return m_mouse_rel_pos.y; }
	Vec2 getMousePos() const override { return m_mouse_pos; }
	bool isMouseDown(MouseButton button) override
	{
		switch (button)
		{
			case LEFT: return (GetAsyncKeyState(VK_LBUTTON) >> 8) != 0;
			case RIGHT: return (GetAsyncKeyState(VK_RBUTTON) >> 8) != 0;
			case MIDDLE: return (GetAsyncKeyState(VK_MBUTTON) >> 8) != 0;
		}
		return false;
	}

	float getActionValue(u32 action) override
	{
		if (!m_is_enabled) return 0;
		InputSystemImpl::Action value;
		if (m_actions.find(action, value))
		{
			switch (value.type)
			{
				case InputType::PRESSED: return (GetAsyncKeyState(value.key) >> 8) ? 1.0f : 0;
				case InputType::DOWN:
					if (value.controller_id < 0)
					{
						return GetAsyncKeyState(value.key) & 1 ? 1.0f : 0;
					}
					else
					{
						return m_xinput_states[value.controller_id].Gamepad.wButtons & value.key ? 1.0f : 0;
					}
				case InputType::MOUSE_X: return m_mouse_rel_pos.x;
				case InputType::MOUSE_Y: return m_mouse_rel_pos.y;
			};

			if (!m_xinput_connected[value.controller_id]) return 0;
			switch (value.type)
			{
				case InputType::LTHUMB_X:
					return deadZone(m_xinput_states[value.controller_id].Gamepad.sThumbLX / 32767.0f, DEADZONE);
				case InputType::LTHUMB_Y:
					return deadZone(m_xinput_states[value.controller_id].Gamepad.sThumbLY / 32767.0f, DEADZONE);
				case InputType::RTHUMB_X:
					return deadZone(m_xinput_states[value.controller_id].Gamepad.sThumbRX / 32767.0f, DEADZONE);
				case InputType::RTHUMB_Y:
					return deadZone(m_xinput_states[value.controller_id].Gamepad.sThumbRY / 32767.0f, DEADZONE);
				case InputType::RTRIGGER:
					return deadZone(m_xinput_states[value.controller_id].Gamepad.bRightTrigger / 255.0f, DEADZONE);
				case InputType::LTRIGGER:
					return deadZone(m_xinput_states[value.controller_id].Gamepad.bLeftTrigger / 255.0f, DEADZONE);
				default: ASSERT(false); break;
			}
		}
		return 0;
	}


	struct Action
	{
		InputSystem::InputType type;
		int key;
		int controller_id;
	};


	IAllocator& m_allocator;
	AssociativeArray<u32, Action> m_actions;
	Vec2 m_injected_mouse_rel_pos;
	Vec2 m_mouse_rel_pos;
	Vec2 m_mouse_pos;
	bool m_is_enabled;
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
