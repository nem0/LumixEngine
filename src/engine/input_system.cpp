#include "engine/input_system.h"
#include "engine/os.h"
#include "engine/controller_device.h"
#include "engine/delegate.h"
#include "engine/delegate_list.h"
#include "engine/engine.h"
#include "engine/lua_wrapper.h"
#include "engine/profiler.h"
#include "engine/math.h"


namespace Lumix
{


struct MouseDevice : InputSystem::Device
{
	void update(float dt) override {}
	const char* getName() const override { return "mouse"; }
};



struct KeyboardDevice : InputSystem::Device
{
	void update(float dt) override {}
	const char* getName() const override { return "keyboard"; }
};


struct InputSystemImpl final : InputSystem
{
	explicit InputSystemImpl(Engine& engine)
		: m_engine(engine)
		, m_allocator(engine.getAllocator())
		, m_events(m_allocator)
		, m_devices(m_allocator)
		, m_to_remove(m_allocator)
	{
		m_mouse_device = LUMIX_NEW(m_allocator, MouseDevice);
		m_mouse_device->type = Device::MOUSE;
		m_keyboard_device = LUMIX_NEW(m_allocator, KeyboardDevice);
		m_keyboard_device->type = Device::KEYBOARD;
		m_devices.push(m_keyboard_device);
		m_devices.push(m_mouse_device);
		ControllerDevice::init(*this);
		registerLuaAPI();
	}


	~InputSystemImpl()
	{
		ControllerDevice::shutdown();
		for (Device* device : m_devices)
		{
			LUMIX_DELETE(m_allocator, device);
		}
	}


	IAllocator& getAllocator() override { return m_allocator; }
	
	
	void addDevice(Device* device) override
	{
		m_devices.push(device);
		Event event;
		event.type = Event::DEVICE_ADDED;
		event.device = device;
		injectEvent(event);
	}


	void removeDevice(Device* device) override 
	{ 
		ASSERT(device != m_keyboard_device);
		ASSERT(device != m_mouse_device);
		m_to_remove.push(device);

		Event event;
		event.type = Event::DEVICE_REMOVED;
		event.device = device;
		injectEvent(event);
	}


	void update(float dt) override
	{
		PROFILE_FUNCTION();

		for (Device* device : m_to_remove)
		{
			m_devices.eraseItem(device);
			LUMIX_DELETE(m_allocator, device);
		}

		m_events.clear();

		for (Device* device : m_devices) device->update(dt);
		ControllerDevice::frame(dt);
	}


	void injectEvent(const OS::Event& event, int mouse_base_x, int mouse_base_y) override
	{
		switch (event.type) {
			case OS::Event::Type::MOUSE_BUTTON: {
				InputSystem::Event input_event;
				input_event.type = InputSystem::Event::BUTTON;
				input_event.device = m_mouse_device;
				input_event.data.button.key_id = (int)event.mouse_button.button;
				input_event.data.button.down = event.mouse_button.down;
				const OS::Point cp = OS::getMouseScreenPos();
				input_event.data.button.x = (float)cp.x - mouse_base_x;
				input_event.data.button.y = (float)cp.y - mouse_base_y;
				injectEvent(input_event);
				break;
			}
			case OS::Event::Type::MOUSE_MOVE: {
				InputSystem::Event input_event;
				input_event.type = InputSystem::Event::AXIS;
				input_event.device = m_mouse_device;
				const OS::Point cp = OS::getMouseScreenPos();
				input_event.data.axis.x_abs = (float)cp.x - mouse_base_x;
				input_event.data.axis.y_abs = (float)cp.y - mouse_base_y;
				input_event.data.axis.x = (float)event.mouse_move.xrel;
				input_event.data.axis.y = (float)event.mouse_move.yrel;
				injectEvent(input_event);
				break;
			}
			case OS::Event::Type::KEY: {
				InputSystem::Event input_event;
				input_event.type = InputSystem::Event::BUTTON;
				input_event.device = m_keyboard_device;
				input_event.data.button.down = event.key.down;
				input_event.data.button.key_id = (int)event.key.keycode;
				injectEvent(input_event);
				break;
			}
			case OS::Event::Type::CHAR: {
				InputSystem::Event input_event;
				input_event.type = InputSystem::Event::TEXT_INPUT;
				input_event.device = m_keyboard_device;
				input_event.data.text.utf8 = event.text_input.utf8;
				injectEvent(input_event);
				break;
			}
			default: break;
		}
	}

	
	void injectEvent(const Event& event) override
	{
		m_events.push(event);
	}


	int getEventsCount() const override { return m_events.size(); }
	const Event* getEvents() const override { return m_events.empty() ? nullptr : &m_events[0]; }

	int getDevicesCount() const override { return m_devices.size(); }
	Device* getDevice(int index) override { return m_devices[index]; }
	void registerLuaAPI();


	Engine& m_engine;
	IAllocator& m_allocator;
	MouseDevice* m_mouse_device;
	KeyboardDevice* m_keyboard_device;
	Array<Event> m_events;
	Array<Device*> m_devices;
	Array<Device*> m_to_remove;
};


void InputSystemImpl::registerLuaAPI()
{
	lua_State* state = m_engine.getState();

	#define REGISTER_KEYCODE(KEYCODE) \
		LuaWrapper::createSystemVariable(state, "Engine", "INPUT_KEYCODE_" #KEYCODE, (int)OS::Keycode::KEYCODE);

		REGISTER_KEYCODE(LBUTTON); 
		REGISTER_KEYCODE(RBUTTON); 
		REGISTER_KEYCODE(CANCEL);
		REGISTER_KEYCODE(MBUTTON);
		REGISTER_KEYCODE(BACKSPACE);
		REGISTER_KEYCODE(TAB);
		REGISTER_KEYCODE(CLEAR);
		REGISTER_KEYCODE(RETURN);
		REGISTER_KEYCODE(SHIFT);
		REGISTER_KEYCODE(CTRL);
		REGISTER_KEYCODE(MENU);
		REGISTER_KEYCODE(PAUSE);
		REGISTER_KEYCODE(CAPITAL);
		REGISTER_KEYCODE(KANA);
		REGISTER_KEYCODE(HANGEUL);
		REGISTER_KEYCODE(HANGUL);
		REGISTER_KEYCODE(JUNJA);
		REGISTER_KEYCODE(FINAL);
		REGISTER_KEYCODE(HANJA);
		REGISTER_KEYCODE(KANJI);
		REGISTER_KEYCODE(ESCAPE);
		REGISTER_KEYCODE(CONVERT);
		REGISTER_KEYCODE(NONCONVERT);
		REGISTER_KEYCODE(ACCEPT);
		REGISTER_KEYCODE(MODECHANGE);
		REGISTER_KEYCODE(SPACE);
		REGISTER_KEYCODE(PAGEUP);
		REGISTER_KEYCODE(PAGEDOWN);
		REGISTER_KEYCODE(END);
		REGISTER_KEYCODE(HOME);
		REGISTER_KEYCODE(LEFT);
		REGISTER_KEYCODE(UP);
		REGISTER_KEYCODE(RIGHT);
		REGISTER_KEYCODE(DOWN);
		REGISTER_KEYCODE(SELECT);
		REGISTER_KEYCODE(PRINT);
		REGISTER_KEYCODE(EXECUTE);
		REGISTER_KEYCODE(SNAPSHOT);
		REGISTER_KEYCODE(INSERT);
		REGISTER_KEYCODE(DEL);
		REGISTER_KEYCODE(HELP);
		REGISTER_KEYCODE(LWIN);
		REGISTER_KEYCODE(RWIN);
		REGISTER_KEYCODE(APPS);
		REGISTER_KEYCODE(SLEEP);
		REGISTER_KEYCODE(NUMPAD0);
		REGISTER_KEYCODE(NUMPAD1);
		REGISTER_KEYCODE(NUMPAD2);
		REGISTER_KEYCODE(NUMPAD3);
		REGISTER_KEYCODE(NUMPAD4);
		REGISTER_KEYCODE(NUMPAD5);
		REGISTER_KEYCODE(NUMPAD6);
		REGISTER_KEYCODE(NUMPAD7);
		REGISTER_KEYCODE(NUMPAD8);
		REGISTER_KEYCODE(NUMPAD9);
		REGISTER_KEYCODE(MULTIPLY);
		REGISTER_KEYCODE(ADD);
		REGISTER_KEYCODE(SEPARATOR);
		REGISTER_KEYCODE(SUBTRACT);
		REGISTER_KEYCODE(DECIMAL);
		REGISTER_KEYCODE(DIVIDE);
		REGISTER_KEYCODE(F1);
		REGISTER_KEYCODE(F2);
		REGISTER_KEYCODE(F3);
		REGISTER_KEYCODE(F4);
		REGISTER_KEYCODE(F5);
		REGISTER_KEYCODE(F6);
		REGISTER_KEYCODE(F7);
		REGISTER_KEYCODE(F8);
		REGISTER_KEYCODE(F9);
		REGISTER_KEYCODE(F10);
		REGISTER_KEYCODE(F11);
		REGISTER_KEYCODE(F12);
		REGISTER_KEYCODE(F13);
		REGISTER_KEYCODE(F14);
		REGISTER_KEYCODE(F15);
		REGISTER_KEYCODE(F16);
		REGISTER_KEYCODE(F17);
		REGISTER_KEYCODE(F18);
		REGISTER_KEYCODE(F19);
		REGISTER_KEYCODE(F20);
		REGISTER_KEYCODE(F21);
		REGISTER_KEYCODE(F22);
		REGISTER_KEYCODE(F23);
		REGISTER_KEYCODE(F24);
		REGISTER_KEYCODE(NUMLOCK);
		REGISTER_KEYCODE(SCROLL);
		REGISTER_KEYCODE(OEM_NEC_EQUAL);
		REGISTER_KEYCODE(OEM_FJ_JISHO);
		REGISTER_KEYCODE(OEM_FJ_MASSHOU);
		REGISTER_KEYCODE(OEM_FJ_TOUROKU);
		REGISTER_KEYCODE(OEM_FJ_LOYA);
		REGISTER_KEYCODE(OEM_FJ_ROYA);
		REGISTER_KEYCODE(LSHIFT);
		REGISTER_KEYCODE(RSHIFT);
		REGISTER_KEYCODE(LCTRL);
		REGISTER_KEYCODE(RCTRL);
		REGISTER_KEYCODE(LMENU);
		REGISTER_KEYCODE(RMENU);
		REGISTER_KEYCODE(BROWSER_BACK);
		REGISTER_KEYCODE(BROWSER_FORWARD);
		REGISTER_KEYCODE(BROWSER_REFRESH);
		REGISTER_KEYCODE(BROWSER_STOP);
		REGISTER_KEYCODE(BROWSER_SEARCH);
		REGISTER_KEYCODE(BROWSER_FAVORITES);
		REGISTER_KEYCODE(BROWSER_HOME);
		REGISTER_KEYCODE(VOLUME_MUTE);
		REGISTER_KEYCODE(VOLUME_DOWN);
		REGISTER_KEYCODE(VOLUME_UP);
		REGISTER_KEYCODE(MEDIA_NEXT_TRACK);
		REGISTER_KEYCODE(MEDIA_PREV_TRACK);
		REGISTER_KEYCODE(MEDIA_STOP);
		REGISTER_KEYCODE(MEDIA_PLAY_PAUSE);
		REGISTER_KEYCODE(LAUNCH_MAIL);
		REGISTER_KEYCODE(LAUNCH_MEDIA_SELECT);
		REGISTER_KEYCODE(LAUNCH_APP1);
		REGISTER_KEYCODE(LAUNCH_APP2);
		REGISTER_KEYCODE(OEM_1);
		REGISTER_KEYCODE(OEM_PLUS);
		REGISTER_KEYCODE(OEM_COMMA);
		REGISTER_KEYCODE(OEM_MINUS);
		REGISTER_KEYCODE(OEM_PERIOD);
		REGISTER_KEYCODE(OEM_2);
		REGISTER_KEYCODE(OEM_3);
		REGISTER_KEYCODE(OEM_4);
		REGISTER_KEYCODE(OEM_5);
		REGISTER_KEYCODE(OEM_6);
		REGISTER_KEYCODE(OEM_7);
		REGISTER_KEYCODE(OEM_8);
		REGISTER_KEYCODE(OEM_AX);
		REGISTER_KEYCODE(OEM_102);
		REGISTER_KEYCODE(ICO_HELP);
		REGISTER_KEYCODE(ICO_00);
		REGISTER_KEYCODE(PROCESSKEY);
		REGISTER_KEYCODE(ICO_CLEAR);
		REGISTER_KEYCODE(PACKET);
		REGISTER_KEYCODE(OEM_RESET);
		REGISTER_KEYCODE(OEM_JUMP);
		REGISTER_KEYCODE(OEM_PA1);
		REGISTER_KEYCODE(OEM_PA2);
		REGISTER_KEYCODE(OEM_PA3);
		REGISTER_KEYCODE(OEM_WSCTRL);
		REGISTER_KEYCODE(OEM_CUSEL);
		REGISTER_KEYCODE(OEM_ATTN);
		REGISTER_KEYCODE(OEM_FINISH);
		REGISTER_KEYCODE(OEM_COPY);
		REGISTER_KEYCODE(OEM_AUTO);
		REGISTER_KEYCODE(OEM_ENLW);
		REGISTER_KEYCODE(OEM_BACKTAB);
		REGISTER_KEYCODE(ATTN);
		REGISTER_KEYCODE(CRSEL);
		REGISTER_KEYCODE(EXSEL);
		REGISTER_KEYCODE(EREOF);
		REGISTER_KEYCODE(PLAY);
		REGISTER_KEYCODE(ZOOM);
		REGISTER_KEYCODE(NONAME);
		REGISTER_KEYCODE(PA1);
		REGISTER_KEYCODE(OEM_CLEAR);

	#undef REGISTER_KEYCODE
}


InputSystem* InputSystem::create(Engine& engine)
{
	return LUMIX_NEW(engine.getAllocator(), InputSystemImpl)(engine);
}


void InputSystem::destroy(InputSystem& system)
{
	auto* impl = static_cast<InputSystemImpl*>(&system);
	LUMIX_DELETE(impl->m_allocator, impl);
}


} // namespace Lumix
