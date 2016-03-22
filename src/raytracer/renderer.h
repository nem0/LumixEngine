#pragma once

#include "lumix.h"
#include "iplugin.h"


namespace Lumix
{


class LUMIX_RENDERER_API Renderer : public IPlugin 
{
public:
	virtual ~Renderer() {}
	virtual void frame() = 0;
	virtual void resize(int width, int height) = 0;
	virtual int getViewCounter() const = 0;
	virtual void viewCounterAdd() = 0;
	virtual void makeScreenshot(const Path& filename) = 0;

	virtual Engine& getEngine() = 0;
};


} // !namespace Lumix
