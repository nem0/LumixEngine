#pragma once


#include "engine/lumix.h"


namespace Lumix
{

	
struct Engine;
template <typename T> struct UniquePtr;

struct ProfilerUI
{
	virtual ~ProfilerUI() {}
	virtual void onGUI() = 0;

	static UniquePtr<ProfilerUI> create(struct StudioApp& app);

	bool m_is_open;
};


} // namespace Lumix
