#pragma once


#include "engine/iplugin.h"


namespace Lumix
{


struct Vec2;


class GUISystem : public IPlugin
{
public:
	class Interface
	{
	public:
		virtual class Pipeline* getPipeline() = 0;
		virtual Vec2 getPos() const = 0;
		virtual Vec2 getSize() const = 0;
		virtual void enableCursor(bool enable) = 0;
	};

	virtual void setInterface(Interface* interface) = 0;
};


} // namespace Lumix