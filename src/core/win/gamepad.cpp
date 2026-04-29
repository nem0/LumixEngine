#include "core/allocator.h"
#include "core/crt.h"
#include "core/gamepad.h"
#include "core/log.h"
#include "core/os.h"
#include "core/profiler.h"
#include "core/stack_array.h"
#include "core/string.h"
#include "core/win/simple_win.h"

namespace Lumix::gamepad {

typedef decltype(XInputGetState)* XInputGetState_fn_ptr;

struct State : XINPUT_STATE {
	bool connected = false;
	UID uid;
};

struct System {
	System() : events(dummy_allocator) {}
	
	struct DummyAllocator : IAllocator {
		void* allocate(size_t size, size_t align) override {
			ASSERT(false);
			return nullptr;
		}

		void deallocate(void* ptr) override {}

		void* reallocate(void* ptr, size_t new_size, size_t old_size, size_t align) override {
			ASSERT(false);
			return nullptr;
		}
	};

	State states[XUSER_MAX_COUNT];
	u32 uid_generator = 0;
	DummyAllocator dummy_allocator;
	StackArray<Event, 64> events;

} g_gamepad;

struct {
	void* lib = nullptr;
	XInputGetState_fn_ptr get_state;
} g_xinput;

bool init() {
	if (g_xinput.lib) {
		logError("Gamepad: already initialized");
		return false;
	}

	g_xinput.lib = os::loadLibrary("Xinput9_1_0.dll");
	if (!g_xinput.lib) {
		logInfo("Gamepad: Could not load Xinput9_1_0.dll");
		return false;
	}

	g_xinput.get_state = (XInputGetState_fn_ptr)os::getLibrarySymbol(g_xinput.lib, "XInputGetState");
	if (!g_xinput.get_state) {
		logError("Gamepad: Failed to get XInputGetState");
		os::unloadLibrary(g_xinput.lib);
		g_xinput.lib = nullptr;
		return false;
	}

	return true;	
}

void shutdown() {
	if (g_xinput.lib) os::unloadLibrary(g_xinput.lib);
	g_xinput.lib = nullptr;
}

Span <const Event> update() {
	g_gamepad.events.clear();
	
	if (g_xinput.lib) return g_gamepad.events;

	for (u32 i = 0; i < XUSER_MAX_COUNT; ++i) {
		XINPUT_STATE xinput_state;
		DWORD result = g_xinput.get_state(i, &xinput_state);
		State& state = g_gamepad.states[i];

		// disconnect
		if (result != ERROR_SUCCESS) {
			if (state.connected) {
				Event& e = g_gamepad.events.emplace();
				e.uid = state.uid;
				e.type = Event::DISCONNECTED;
				state.connected = false;	
			}
			continue;
		}

		// connect
		if (!state.connected) {
			++g_gamepad.uid_generator;
			state.uid = UID(g_gamepad.uid_generator);
			Event& e = g_gamepad.events.emplace();
			e.uid = state.uid;
			e.type = Event::CONNECTED;
		}

		state.connected = true;
		XINPUT_STATE& old_state = (XINPUT_STATE&)state;
		if (memcmp(&xinput_state, &state, sizeof(xinput_state)) == 0) continue;
		
		const XINPUT_GAMEPAD& gp = xinput_state.Gamepad;
		const XINPUT_GAMEPAD& old_gp = old_state.Gamepad;
		const WORD changed_buttons = gp.wButtons ^ old_gp.wButtons;
		if (changed_buttons) {
			for (u32 bit = 0; bit < 16; ++bit) {
				const WORD button = WORD(1u << bit);
				if ((changed_buttons & button) == 0) continue;
				Event& e = g_gamepad.events.emplace();
				e.uid = state.uid;
				e.type = Event::BUTTON;
				e.button = button;
				e.down = (gp.wButtons & button) != 0;
			}
		}

		if (gp.bLeftTrigger != old_gp.bLeftTrigger) {
			Event& e = g_gamepad.events.emplace();
			e.uid = state.uid;
			e.type = Event::AXIS;
			e.axis = Event::Axis::LTRIGGER;
			e.x = gp.bLeftTrigger * (1.0f / 255.0f);
		}

		if (gp.bRightTrigger != old_gp.bRightTrigger) {
			Event& e = g_gamepad.events.emplace();
			e.uid = state.uid;
			e.type = Event::AXIS;
			e.axis = Event::Axis::RTRIGGER;
			e.x = gp.bRightTrigger * (1.0f / 255.0f);
		}

		if (gp.sThumbLX != old_gp.sThumbLX || gp.sThumbLY != old_gp.sThumbLY) {
			Event& e = g_gamepad.events.emplace();
			e.uid = state.uid;
			e.type = Event::AXIS;
			e.axis = Event::Axis::LTHUMB;
			e.x = gp.sThumbLX * (1.0f / 32768.0f);
			e.y = gp.sThumbLY * (1.0f / 32768.0f);
		}

		if (gp.sThumbRX != old_gp.sThumbRX || gp.sThumbRY != old_gp.sThumbRY) {
			Event& e = g_gamepad.events.emplace();
			e.uid = state.uid;
			e.type = Event::AXIS;
			e.axis = Event::Axis::RTHUMB;
			e.x = gp.sThumbRX * (1.0f / 32768.0f);
			e.y = gp.sThumbRY * (1.0f / 32768.0f);
		}

		((XINPUT_STATE&)state) = xinput_state;
	}

	return g_gamepad.events;
}

} // namespace Lumix
