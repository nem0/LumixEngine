#include "core/input_system.h"
#include "core/map.h"
#include "core/string.h"
#include <Windows.h>


namespace Lumix
{

	struct InputSystemImpl
	{
		InputSystemImpl(IAllocator& allocator)
			: m_actions(allocator)
		{}

		struct Action
		{
			InputSystem::InputType type;
			int key;
		};
		Map<uint32_t, Action> m_actions;
		float m_mouse_rel_x;
		float m_mouse_rel_y;
	};


	void InputSystem::update(float)
	{
		m_impl->m_mouse_rel_x = 0;
		m_impl->m_mouse_rel_y = 0;
	}


	bool InputSystem::create(IAllocator& allocator)
	{
		m_impl = LUMIX_NEW(InputSystemImpl)(allocator);
		m_impl->m_mouse_rel_x = 0;
		m_impl->m_mouse_rel_y = 0;
		return true;
	}


	void InputSystem::destroy()
	{
		LUMIX_DELETE(m_impl);
		m_impl = NULL;
	}


	void InputSystem::injectMouseXMove(float value)
	{
		m_impl->m_mouse_rel_x = value;
	}


	void InputSystem::injectMouseYMove(float value)
	{
		m_impl->m_mouse_rel_y = value;
	}


	void InputSystem::addAction(uint32_t action, InputType type, int key)
	{
		InputSystemImpl::Action value;
		value.key = key;
		value.type = type;
		m_impl->m_actions[action] =  value;
	}


	float InputSystem::getActionValue(uint32_t action)
	{
		InputSystemImpl::Action value;
		if(m_impl->m_actions.find(action, value))
		{
			switch(value.type)
			{
				case InputType::PRESSED:
					return GetAsyncKeyState(value.key) >> 8 ? 1.0f : 0;
					break;
				case InputType::DOWN:
					return GetAsyncKeyState(value.key) & 1 ? 1.0f : 0;
					break;
				case InputType::MOUSE_X:
					return m_impl->m_mouse_rel_x;
					break;
				case InputType::MOUSE_Y:
					return m_impl->m_mouse_rel_y;
					break;
			};
		}
		return -1;
	}


} // ~ namespace Lumix
