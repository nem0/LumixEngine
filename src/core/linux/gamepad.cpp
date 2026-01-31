#include "core/gamepad.h"

namespace black
{

class LinuxGamepadBackend : public IGamepadBackend
{
public:
    LinuxGamepadBackend(IAllocator& allocator) : m_allocator(allocator) {}
    
    bool init() override
    {
        return false;
    }
    
    void shutdown() override
    {
    }
    
    int getMaxControllers() const override
    {
        return 4; // Common limit
    }
    
    bool updateController(int index, GamepadState& state) override
    {
        state.connected = false;
        return false;
    }
    
    bool isControllerConnected(int index) override
    {
        return false;
    }

private:
    IAllocator& m_allocator;
};

IGamepadBackend* createGamepadBackend(IAllocator& allocator)
{
    return BLACK_NEW(allocator, LinuxGamepadBackend)(allocator);
}

} // namespace black