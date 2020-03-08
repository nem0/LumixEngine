#include "engine/controller_device.h"
#include "engine/allocator.h"


#include <windows.h>
#include <Xinput.h>


namespace Lumix
{


typedef decltype(XInputGetState)* XInputGetState_fn_ptr;


static struct {
	int last_checked;
	bool connected[XUSER_MAX_COUNT];
	HMODULE lib;
	XInputGetState_fn_ptr get_state;
	InputSystem* input;
	XINPUT_STATE states[XUSER_MAX_COUNT];
	InputSystem::Device* devices[XUSER_MAX_COUNT];
} g_controllers;


struct XInputControllerDevice : ControllerDevice
{
	void update(float dt) override
	{
		XINPUT_STATE new_state;
		const XINPUT_STATE& old_state = g_controllers.states[index];
		g_controllers.get_state(index, &new_state);
		InputSystem& input = *g_controllers.input;

		bool is_changed = new_state.dwPacketNumber != old_state.dwPacketNumber;
		if (!is_changed) return;

		if (new_state.Gamepad.wButtons != old_state.Gamepad.wButtons)
		{
			for (int i = 0; i < 16; ++i)
			{
				WORD mask = 1 << i;
				WORD new_bit = new_state.Gamepad.wButtons & mask;
				WORD old_bit = old_state.Gamepad.wButtons & mask;
				if (new_bit != old_bit)
				{
					InputSystem::Event event;
					event.device = this;
					event.type = InputSystem::Event::BUTTON;
					event.data.button.key_id = i;
					event.data.button.down = new_bit != 0;
					input.injectEvent(event);
				}
			}
		}

		auto checkAxisEvent = [this, &input](BYTE new_state, BYTE old_state, InputSystem::AxisEvent::Axis axis, float max_value) {
			if (new_state != old_state)
			{
				InputSystem::Event event;
				event.device = this;
				event.type = InputSystem::Event::AXIS;
				event.data.axis.x = new_state / max_value;
				event.data.axis.axis = axis;
				input.injectEvent(event);
			}

		};

		checkAxisEvent(new_state.Gamepad.bLeftTrigger, old_state.Gamepad.bLeftTrigger, InputSystem::AxisEvent::LTRIGGER, 255.0f);
		checkAxisEvent(new_state.Gamepad.bRightTrigger, old_state.Gamepad.bRightTrigger, InputSystem::AxisEvent::RTRIGGER, 255.0f);

		auto check2AxisEvent = [this, &input](SHORT new_x, SHORT old_x, SHORT new_y, SHORT old_y, InputSystem::AxisEvent::Axis axis, float max_value) {
			if (new_x != old_x || new_y != old_y)
			{
				InputSystem::Event event;
				event.device = this;
				event.type = InputSystem::Event::AXIS;
				event.data.axis.x = new_x / max_value;
				event.data.axis.y = new_y / max_value;
				event.data.axis.axis = axis;
				input.injectEvent(event);
			}
		};

		check2AxisEvent(new_state.Gamepad.sThumbLX, old_state.Gamepad.sThumbLX, new_state.Gamepad.sThumbLY, old_state.Gamepad.sThumbLY, InputSystem::AxisEvent::LTHUMB, 65535.0f);
		check2AxisEvent(new_state.Gamepad.sThumbRX, old_state.Gamepad.sThumbRX, new_state.Gamepad.sThumbRY, old_state.Gamepad.sThumbRY, InputSystem::AxisEvent::RTHUMB, 65535.0f);

		g_controllers.states[index] = new_state;
	}
};


void ControllerDevice::init(InputSystem& input_system)
{
	for (Device*& dev : g_controllers.devices) dev = nullptr;
	for (bool& connected : g_controllers.connected) connected = false;
	
	g_controllers.input = &input_system;
	g_controllers.last_checked = 0;
	g_controllers.lib = LoadLibrary("Xinput9_1_0.dll");
	if (g_controllers.lib)
	{
		g_controllers.get_state = (XInputGetState_fn_ptr)GetProcAddress(g_controllers.lib, "XInputGetState");
		if (!g_controllers.get_state)
		{
			FreeLibrary(g_controllers.lib);
			g_controllers.lib = nullptr;
		}
	}
}


void ControllerDevice::frame(float dt)
{
	if (!g_controllers.get_state) return;

	for (int i = 0; i < XUSER_MAX_COUNT; ++i)
	{
		if (g_controllers.connected[i] || i == g_controllers.last_checked)
		{
			auto status = g_controllers.get_state(i, &g_controllers.states[i]);
			g_controllers.connected[i] = status == ERROR_SUCCESS;
			if (g_controllers.connected[i] && !g_controllers.devices[i])
			{
				XInputControllerDevice* new_device = LUMIX_NEW(g_controllers.input->getAllocator(), XInputControllerDevice);
				new_device->type = InputSystem::Device::CONTROLLER;
				new_device->index = i;
				g_controllers.devices[i] = new_device;
				g_controllers.input->addDevice(new_device);
			}
			else if (!g_controllers.connected[i] && g_controllers.devices[i])
			{
				g_controllers.input->removeDevice(g_controllers.devices[i]);
				g_controllers.devices[i] = nullptr;
			}
		}
	}
	g_controllers.last_checked = (g_controllers .last_checked + 1) % XUSER_MAX_COUNT;
}


void ControllerDevice::shutdown()
{
	if (g_controllers.lib) FreeLibrary(g_controllers.lib);
}


} // namespace Lumix
