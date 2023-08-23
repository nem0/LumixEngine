#pragma once

#include "editor/studio_app.h"

namespace Lumix {

template <typename T> struct UniquePtr;

namespace anim {

struct ControllerEditor {
	static UniquePtr<ControllerEditor> create(StudioApp& app);
	virtual ~ControllerEditor() {}
};

} // namespace anim
} // namespace Lumix