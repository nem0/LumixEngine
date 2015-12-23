#pragma once


namespace Lumix
{
class Engine;
}


class ProfilerUI
{
public:
	virtual ~ProfilerUI() {}
	virtual void onGUI() = 0;
	virtual void setGUIInterface(class GUIInterface& gui) = 0;

	static ProfilerUI* create(Lumix::Engine& engine);
	static void destroy(ProfilerUI& ui);

	bool m_is_opened;
};