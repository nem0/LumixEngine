#pragma once


#include "engine/input_system.h"


namespace Lumix
{


struct ControllerDevice : public InputSystem::Device
{
	static void init(InputSystem& input_system);
	static void update(float dt);
	static void shutdown();

	const char* getName() const override { return "controller"; }
};


} // namespace Lumix