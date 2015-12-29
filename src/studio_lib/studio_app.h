#pragma once


#include "lumix.h"


namespace Lumix
{
class WorldEditor;
}


class LUMIX_STUDIO_LIB_API StudioApp
{
public:
	class IPlugin
	{
	public:
		virtual ~IPlugin() {}

		virtual void onWindowGUI() = 0;
		virtual const char* getWindowName() = 0;

		struct Action* m_action;
	};

public:
	static StudioApp* create();
	static void destroy(StudioApp& app);
	
	virtual class PropertyGrid* getPropertyGrid() = 0;
	virtual Lumix::WorldEditor* getWorldEditor() = 0;
	virtual void addPlugin(IPlugin& plugin) = 0;
	virtual void removePlugin(IPlugin& plugin) = 0;

	virtual ~StudioApp() {}
	virtual void run() = 0;
};

