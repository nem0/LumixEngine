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

	static UniquePtr<ProfilerUI> create(Engine& engine);

	bool m_is_open;
};


} // namespace Lumix
