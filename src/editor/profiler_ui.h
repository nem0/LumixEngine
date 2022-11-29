#pragma once


#include "editor/studio_app.h"
#include "engine/lumix.h"


namespace Lumix
{

	
struct Engine;
template <typename T> struct UniquePtr;

struct ProfilerUI : StudioApp::GUIPlugin
{
	static UniquePtr<ProfilerUI> create(struct StudioApp& app);
};


} // namespace Lumix
