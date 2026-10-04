#pragma once

#include "core/allocator.h"
#include "core/array.h"
#include "core/math.h"
#include "core/path.h"
#include "core/string.h"
#include "engine/lumix.h"

namespace Lumix {

struct AssetCompiler;
struct FileSystem;
struct IAllocator;
struct ModelImporter;
struct StudioApp;

// Extension of procedural mesh recipes (Lumix Procedural Geometry).
// A recipe is an Evox program importing `core:procedural_geom` and returning `Mesh` from `main`.
static constexpr const char* PROCEDURAL_MESH_EXTENSION = "lpg";

struct LUMIX_RENDERER_API ProceduralMesh {
	// matches core:procedural_geom.Vertex
	struct Vertex {
		Vec3 position;
		Vec2 uv;
		Vec3 normal;
		Vec3 tangent;
	};

	ProceduralMesh(IAllocator& allocator) : vertices(allocator), indices(allocator) {}

	Array<Vertex> vertices;
	Array<u32> indices;
};

// Runs a recipe in an isolated Evox runtime (no world, no engine bindings) and validates its output.
LUMIX_RENDERER_API bool compileProceduralMesh(FileSystem& fs,
	StringView source, const Path& path, ProceduralMesh& result,
	String& error, IAllocator& allocator, AssetCompiler* dependencies = nullptr);

// Compiles a recipe to a regular Model resource.
// Material `<dir>/<basename>.mat` is created next to the recipe if it does not exist yet.
LUMIX_RENDERER_API bool compileProceduralMeshModel(StudioApp& app, const Path& src, StringView source);

// Registers .lpg with asset compiler and asset browser, unregisters in destructor.
struct ProceduralMeshEditor {
	virtual ~ProceduralMeshEditor() {}
	static UniquePtr<ProceduralMeshEditor> create(StudioApp& app);
};

} // namespace Lumix
