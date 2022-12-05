#pragma once

#include "editor/studio_app.h"

namespace Lumix {

namespace gpu { struct VertexDecl; }

template <typename T> struct UniquePtr;

struct ParticleEditor : StudioApp::GUIPlugin {
	static UniquePtr<ParticleEditor> create(StudioApp& app);
	static gpu::VertexDecl getVertexDecl(const char* path, StudioApp& app);
	virtual void open(const char* path) = 0;
	virtual bool compile(struct InputMemoryStream& input, struct OutputMemoryStream& output, const char* path) = 0;
};

}