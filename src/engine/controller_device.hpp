#pragma once


#include "engine/input_system.hpp"


namespace Lumix
{


struct ControllerDevice : InputSystem::Device
{
	static void init(InputSystem& input_system);
	static void frame(float dt);
	static void shutdown();

	const char* getName() const override { return "controller"; }
};


} // namespace Lumix