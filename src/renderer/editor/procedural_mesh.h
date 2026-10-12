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

	// Range of indices with a material and a level of detail. material is an index into material_names.
	// matches core:procedural_geom.Part, except for the material
	struct Part {
		u32 material;
		u32 lod;
		u32 first_index;
		u32 index_count;
	};

	ProceduralMesh(IAllocator& allocator) : vertices(allocator), indices(allocator), parts(allocator), material_names(allocator) {}

	bool hasLod(u32 lod) const {
		for (const Part& part : parts) {
			if (part.lod == lod) return true;
		}
		return false;
	}

	// Number of levels of detail, the parts use levels 0 to count - 1.
	u32 getLodCount() const {
		u32 count = 0;
		for (const Part& part : parts) {
			if (part.lod + 1 > count) count = part.lod + 1;
		}
		return count;
	}

	Array<Vertex> vertices;
	Array<u32> indices;
	Array<Part> parts;
	// Material names of the recipe, index 0 is the default material (the empty name). Each name is a material of the model.
	Array<String> material_names;
};

// Runs a recipe in an isolated Evox runtime (no world, no engine bindings) and validates its output.
// Parts of the recipe may use levels of detail below lod_count.
LUMIX_RENDERER_API bool compileProceduralMesh(FileSystem& fs,
	StringView source, const Path& path, ProceduralMesh& result,
	String& error, IAllocator& allocator, AssetCompiler* dependencies = nullptr, u32 lod_count = 1);

// Compiles a recipe to a regular Model resource.
// Options are read from `<recipe>.meta`: lod_count (1-4), lodN_distance and autolodN (see model_meta.h).
// Material `<dir>/<basename>.mat` is created next to the recipe if it does not exist yet, named materials get `<basename>_<name>.mat`.
LUMIX_RENDERER_API bool compileProceduralMeshModel(StudioApp& app, const Path& src, StringView source);

// Registers .lpg with asset compiler and asset browser, unregisters in destructor.
struct ProceduralMeshEditor {
	virtual ~ProceduralMeshEditor() {}
	static UniquePtr<ProceduralMeshEditor> create(StudioApp& app);
};

} // namespace Lumix
