#pragma once
#include "core/allocator.h"

namespace Lumix {

struct GamepadState {
	struct {
		float x, y;
	} left_stick, right_stick;

	float left_trigger, right_trigger;
	u16 buttons;
	u32 packet_number;
	bool connected;
};

class IGamepadBackend {
public:
	virtual ~IGamepadBackend() = default;
	virtual bool init() = 0;
	virtual void shutdown() = 0;
	virtual int getMaxControllers() const = 0;
	virtual bool updateController(int index, GamepadState& state) = 0;
	virtual bool isControllerConnected(int index) = 0;
};

IGamepadBackend* createGamepadBackend(IAllocator& allocator);

} // namespace Lumix