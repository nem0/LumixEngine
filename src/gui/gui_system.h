#pragma once


#include "engine/plugin.h"
#include "core/os.h"


namespace Lumix
{


struct Vec2;


struct GUISystem : ISystem
{
	struct Interface
	{
		virtual ~Interface() {}
		virtual struct Pipeline* getPipeline() = 0;
		virtual Vec2 getPos() const = 0;
		virtual Vec2 getSize() const = 0;
		virtual void enableCursor(bool enable) = 0;
		virtual void setCursor(os::CursorType type) = 0;
	};

	virtual void setInterface(Interface* interface) = 0;
	virtual void enableCursor(bool enable) = 0;
	virtual void setCursor(os::CursorType type) = 0;
	virtual Engine& getEngine() = 0;
};


} // namespace Lumix