#pragma once

#include "editor/studio_app.h"

namespace Lumix {

struct ParticleEditor : StudioApp::GUIPlugin {
	static UniquePtr<ParticleEditor> create(StudioApp& app);
	virtual void open(const char* path) = 0;
	virtual bool compile(InputMemoryStream& input, OutputMemoryStream& output, const char* path) = 0;
};

}