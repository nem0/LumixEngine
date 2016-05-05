#include "engine/core/input_system.h"
#include "engine/core/associative_array.h"
#include "engine/core/profiler.h"
#include "engine/core/string.h"
#include "engine/core/vec.h"


namespace Lumix
{
	struct InputSystemImpl : public InputSystem
	{
		InputSystemImpl(IAllocator& allocator) : m_allocator(allocator) {}

		bool create() { return false; }

		void enable(bool enabled) override {}
		void update(float dt) override {}
		float getActionValue(uint32 action) override { return 0; }
		void injectMouseXMove(float value) override {}
		void injectMouseYMove(float value) override {}
		float getMouseXMove() const override { return 0; }
		float getMouseYMove() const override { return 0; }
		void addAction(uint32 action, InputType type, int key, int controller_id) override {}

		IAllocator& m_allocator;
	};


	InputSystem* InputSystem::create(IAllocator& allocator)
	{
		auto* system = LUMIX_NEW(allocator, InputSystemImpl)(allocator);
		if (!system->create())
		{
			LUMIX_DELETE(allocator, system);
			return nullptr;
		}
		return system;
	}


	void InputSystem::destroy(InputSystem& system)
	{
		auto* impl = static_cast<InputSystemImpl*>(&system);
		LUMIX_DELETE(impl->m_allocator, impl);
	}


} // namespace Lumix
