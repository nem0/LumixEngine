#pragma once


#include "lumix.h"


namespace Lumix
{
class WorldEditor;
}


class LUMIX_STUDIO_LIB_API StudioApp
{
public:
	static StudioApp* create();
	static void destroy(StudioApp& app);
	
	virtual class PropertyGrid* getPropertyGrid() = 0;
	virtual Lumix::WorldEditor* getWorldEditor() = 0;

	virtual ~StudioApp() {}
	virtual void run() = 0;
};

