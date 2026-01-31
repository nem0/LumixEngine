#pragma once


#include "engine/plugin.h"

namespace black {

namespace os { enum class CursorType : u32; }

struct Vec2;

//@ object
struct GUISystem : ISystem {
	struct Interface {
		virtual ~Interface() {}
		virtual struct Pipeline* getPipeline() = 0;
		virtual Vec2 getPos() const = 0;
		virtual Vec2 getSize() const = 0;
		virtual void enableCursor(bool enable) = 0;
		virtual void setCursor(os::CursorType type) = 0;
	};

	virtual void setInterface(Interface* interface) = 0;
	//@ function
	virtual void enableCursor(bool enable) = 0;
	virtual void setCursor(os::CursorType type) = 0;
	virtual Engine& getEngine() = 0;
};


} // namespace black