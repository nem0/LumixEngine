#pragma once


#include "lumix.h"


namespace Lumix
{
class Engine;
}


class ProfilerUI
{
public:
	virtual ~ProfilerUI() {}
	virtual void onGUI() = 0;

	static ProfilerUI* create(Lumix::Engine& engine);
	static void destroy(ProfilerUI& ui);

	bool m_is_opened;
	Lumix::uint64 m_frame_start;
	Lumix::uint64 m_frame_end;
};