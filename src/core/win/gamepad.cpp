#include "core/gamepad.h"
#include "core/os.h"
#include "core/profiler.h"
#include "core/win/simple_win.h"

namespace Lumix {

typedef decltype(XInputGetState)* XInputGetState_fn_ptr;

class XInputBackend : public IGamepadBackend {
public:
	XInputBackend(IAllocator& allocator)
		: m_allocator(allocator) {}

	bool init() override {
		m_lib = os::loadLibrary("Xinput9_1_0.dll");
		if (!m_lib) return false;

		m_get_state = (XInputGetState_fn_ptr)os::getLibrarySymbol(m_lib, "XInputGetState");
		if (!m_get_state) {
			os::unloadLibrary(m_lib);
			m_lib = nullptr;
			return false;
		}

		for (int i = 0; i < XUSER_MAX_COUNT; ++i) {
			m_states[i] = {};
		}

		return true;
	}

	void shutdown() override {
		if (m_lib) os::unloadLibrary(m_lib);
	}

	int getMaxControllers() const override { return XUSER_MAX_COUNT; }

	bool updateController(int index, GamepadState& state) override {
		if (!m_get_state || index >= XUSER_MAX_COUNT) return false;

		XINPUT_STATE xinput_state;
		DWORD result = m_get_state(index, &xinput_state);

		if (result != ERROR_SUCCESS) {
			state.connected = false;
			return false;
		}

		state.connected = true;
		state.packet_number = xinput_state.dwPacketNumber;

		// Convert XInput format to our generic format
		const XINPUT_GAMEPAD& gamepad = xinput_state.Gamepad;

		state.left_stick.x = gamepad.sThumbLX / 32768.0f;
		state.left_stick.y = gamepad.sThumbLY / 32768.0f;
		state.right_stick.x = gamepad.sThumbRX / 32768.0f;
		state.right_stick.y = gamepad.sThumbRY / 32768.0f;

		state.left_trigger = gamepad.bLeftTrigger / 255.0f;
		state.right_trigger = gamepad.bRightTrigger / 255.0f;

		state.buttons = gamepad.wButtons;

		return true;
	}

	bool isControllerConnected(int index) override {
		if (!m_get_state || index >= XUSER_MAX_COUNT) return false;

		XINPUT_STATE xinput_state;
		return m_get_state(index, &xinput_state) == ERROR_SUCCESS;
	}

private:
	IAllocator& m_allocator;
	HMODULE m_lib = nullptr;
	XInputGetState_fn_ptr m_get_state = nullptr;
	XINPUT_STATE m_states[XUSER_MAX_COUNT] = {};
};

IGamepadBackend* createGamepadBackend(IAllocator& allocator) {
	return LUMIX_NEW(allocator, XInputBackend)(allocator);
}

} // namespace Lumix