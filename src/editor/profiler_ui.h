#pragma once


#include "engine/lumix.h"


namespace Lumix
{

	
class Engine;


class ProfilerUI
{
public:
	virtual ~ProfilerUI() {}
	virtual void onGUI() = 0;

	static ProfilerUI* create(Engine& engine);
	static void destroy(ProfilerUI& ui);

	bool m_is_open;
	u64 m_frame_start;
	u64 m_frame_end;
};


} // namespace Lumix
