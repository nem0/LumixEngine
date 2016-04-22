#include "input_system.h"
#include "engine/core/map.h"
#include "engine/core/string.h"


namespace Lux
{

	struct InputSystemImpl
	{
		struct Action
		{
			InputSystem::InputType type;
			int key;
		};
		map<unsigned int, Action> actions;
		float mouse_rel_x;
		float mouse_rel_y;
	};


	void InputSystem::update(float dt)
	{
		m_impl->mouse_rel_x = 0;
		m_impl->mouse_rel_y = 0;
	}


	bool InputSystem::create()
	{
		m_impl = new InputSystemImpl();
		m_impl->mouse_rel_x = 0;
		m_impl->mouse_rel_y = 0;
		return true;
	}


	void InputSystem::destroy()
	{
		delete m_impl;
	}


	void InputSystem::injectMouseXMove(float value)
	{
		m_impl->mouse_rel_x = value;
	}


	void InputSystem::injectMouseYMove(float value)
	{
		m_impl->mouse_rel_y = value;
	}


	void InputSystem::addAction(unsigned int action, InputType type, int key)
	{
		InputSystemImpl::Action value;
		value.key = key;
		value.type = type;
		m_impl->actions.insert(action, value);
	}


	float InputSystem::getActionValue(unsigned int action)
	{
		InputSystemImpl::Action value;
		if(m_impl->actions.find(action, value))
		{
			switch(value.type)
			{
				case InputType::PRESSED:
//					return GetAsyncKeyState(value.key) >> 8 ? 1.0f : 0;
					return 0;
					break;
				case InputType::DOWN:
					//return GetAsyncKeyState(value.key) & 1 ? 1.0f : 0;
					return 0;
					break;
				case InputType::MOUSE_X:
					return m_impl->mouse_rel_x;
					break;
				case InputType::MOUSE_Y:
					return m_impl->mouse_rel_y;
					break;
			};
		}
		return -1;
	}


} // ~ namespace Lux
