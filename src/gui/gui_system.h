#pragma once


#include "engine/plugin.h"


namespace Lumix
{


struct Vec2;


struct GUISystem : IPlugin
{
	struct Interface
	{
		virtual ~Interface() {}
		virtual struct Pipeline* getPipeline() = 0;
		virtual Vec2 getPos() const = 0;
		virtual Vec2 getSize() const = 0;
		virtual void enableCursor(bool enable) = 0;
	};

	virtual void setInterface(Interface* interface) = 0;
	virtual void enableCursor(bool enable) = 0;
	virtual Engine& getEngine() = 0;
};


} // namespace Lumix