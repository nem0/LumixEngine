#include "core/input_system.h"
#include "core/associative_array.h"
#include "core/string.h"

#include "core/pc/simple_win.h"
#undef NOKEYSTATES
#undef NOUSER
#include <Windows.h>


namespace Lumix
{

	struct InputSystemImpl : public InputSystem
	{
		InputSystemImpl(IAllocator& allocator)
			: m_actions(allocator)
			, m_allocator(allocator)
			, m_is_enabled(false)
			, m_mouse_rel_x(0)
			, m_mouse_rel_y(0)
		{}


		void enable(bool enabled) override
		{
			m_is_enabled = enabled;
		}


		void update(float) override
		{
			m_mouse_rel_x = 0;
			m_mouse_rel_y = 0;
		}


		void injectMouseXMove(float value) override
		{
			m_mouse_rel_x = value;
		}


		void injectMouseYMove(float value) override
		{
			m_mouse_rel_y = value;
		}


		void addAction(uint32 action, InputType type, int key) override
		{
			InputSystemImpl::Action value;
			value.key = key;
			value.type = type;
			m_actions[action] = value;
		}


		float getActionValue(uint32 action) override
		{
			if (!m_is_enabled) return 0;
			InputSystemImpl::Action value;
			if (m_actions.find(action, value))
			{
				switch (value.type)
				{
				case InputType::PRESSED:
					return (GetAsyncKeyState(value.key) >> 8) ? 1.0f : 0;
					break;
				case InputType::DOWN:
					return GetAsyncKeyState(value.key) & 1 ? 1.0f : 0;
					break;
				case InputType::MOUSE_X:
					return m_mouse_rel_x;
					break;
				case InputType::MOUSE_Y:
					return m_mouse_rel_y;
					break;
				};
			}
			return -1;
		}


		struct Action
		{
			InputSystem::InputType type;
			int key;
		};


		IAllocator& m_allocator;
		AssociativeArray<uint32, Action> m_actions;
		float m_mouse_rel_x;
		float m_mouse_rel_y;
		bool m_is_enabled;
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
