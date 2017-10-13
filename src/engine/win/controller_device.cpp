#include "engine/controller_device.h"
#include "engine/iallocator.h"


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
	void update(float dt) override {}
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
