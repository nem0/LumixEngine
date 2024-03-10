#pragma once

#include "editor/studio_app.hpp"

namespace Lumix {

namespace gpu { struct VertexDecl; }

template <typename T> struct UniquePtr;

struct ParticleEditor {
	virtual ~ParticleEditor() {}

	static UniquePtr<ParticleEditor> create(StudioApp& app);
	static gpu::VertexDecl getVertexDecl(const char* path, u32 emitter_idx, Array<struct String>& attribute_names, StudioApp& app);
	static void registerDependencies(const struct Path& path, StudioApp& app);
	virtual bool compile(struct InputMemoryStream& input, struct OutputMemoryStream& output, const char* path) = 0;
};

}