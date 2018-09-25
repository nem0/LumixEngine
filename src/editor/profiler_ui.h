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
};


} // namespace Lumix
