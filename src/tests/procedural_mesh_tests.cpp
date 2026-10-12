#include "core/crt.h"
#include "core/log.h"
#include "core/stream.h"
#include "engine/file_system.h"
#include "renderer/editor/procedural_mesh.h"
#include "tests/common.h"

using namespace Lumix;
namespace {
struct Fixture {
	Fixture() : fs(FileSystem::create(".", getGlobalAllocator())), result(getGlobalAllocator()), error(getGlobalAllocator()) {
		fs->mount(".", "");
	}
	bool run(const char* source, bool expect_error = false, u32 lod_count = 1) {
		const bool success = compileProceduralMesh(*fs, source, Path("test.lpg"), result, error, getGlobalAllocator(), nullptr, lod_count);
		if (!success && !expect_error) logError(error);
		return success;
	}
	UniquePtr<FileSystem> fs;
	ProceduralMesh result;
	String error;
};

bool testProceduralMeshCube() {
	Fixture f;
	ASSERT_TRUE(f.run(R"(
import "core:procedural_geom"
fn main() : Mesh {
    return cube(2.0, 4.0, 6.0);
}
)"));
	ASSERT_EQ(24, f.result.vertices.size());
	ASSERT_EQ(36, f.result.indices.size());
	float max_x = 0, max_y = 0, max_z = 0;
	for (const ProceduralMesh::Vertex& v : f.result.vertices) {
		max_x = maximum(max_x, v.position.x);
		max_y = maximum(max_y, v.position.y);
		max_z = maximum(max_z, v.position.z);
	}
	ASSERT_FLOAT_EQ(1.f, max_x); ASSERT_FLOAT_EQ(2.f, max_y); ASSERT_FLOAT_EQ(3.f, max_z);
	return true;
}

bool testProceduralMeshMerge() {
	Fixture f;
	ASSERT_TRUE(f.run(R"(
import "core:procedural_geom"
import "core:vec3"
fn main() : Mesh {
    return merge(cube(1.0, 1.0, 1.0), translate(cone(1.0, 2.0, 8), Vec3 { 0.0, 5.0, 0.0 }));
}
)"));
	ASSERT_TRUE(f.result.vertices.size() > 24);
	for (u32 index : f.result.indices) ASSERT_TRUE(index < (u32)f.result.vertices.size());
	return true;
}

bool testProceduralMeshDefaultRecipe() {
	Fixture f;
	ASSERT_TRUE(f.run(R"(import "core:procedural_geom"
import "core:vec3"

fn main() : Mesh {
    var body = cube(1.0, 1.0, 1.0);
    var top = translate(sphere(0.5, 32, 16), Vec3 { 0.0, 0.75, 0.0 });
    return merge(body, top);
}
)"));
	ASSERT_TRUE(f.result.vertices.size() > 24);
	return true;
}

bool testProceduralMeshPagodaExample() {
	Fixture f;
	OutputMemoryStream source(getGlobalAllocator());
	ASSERT_TRUE(f.fs->getContentSync(Path("scripts/tests/pagoda.lpg"), source));
	ASSERT_TRUE(f.run(String(StringView((const char*)source.data(), (u32)source.size()), getGlobalAllocator()).c_str()));
	ASSERT_TRUE(f.result.vertices.size() > 1000);
	float max_y = 0, min_y = 0;
	for (const ProceduralMesh::Vertex& v : f.result.vertices) {
		max_y = maximum(max_y, v.position.y);
		min_y = minimum(min_y, v.position.y);
	}
	ASSERT_FLOAT_EQ(0.f, min_y);
	ASSERT_TRUE(max_y > 5.f);
	return true;
}

bool testProceduralMeshHouseExample() {
	Fixture f;
	OutputMemoryStream source(getGlobalAllocator());
	ASSERT_TRUE(f.fs->getContentSync(Path("scripts/tests/house.lpg"), source));
	ASSERT_TRUE(f.run(String(StringView((const char*)source.data(), (u32)source.size()), getGlobalAllocator()).c_str()));
	// default material and stone, walls, roof, wood, glass; the chimney (part 7) shares the stone material of the foundation
	ASSERT_EQ(6, f.result.material_names.size());
	ASSERT_EQ(19, f.result.parts.size());
	ASSERT_EQ(f.result.parts[0].material, f.result.parts[7].material);
	for (const ProceduralMesh::Part& part : f.result.parts) {
		ASSERT_TRUE(part.material > 0);
		ASSERT_EQ(0, part.lod);
	}
	return true;
}

bool testProceduralMeshMaterials() {
	{
		Fixture f;
		ASSERT_TRUE(f.run(R"(
import "core:procedural_geom"
import "core:vec3"
fn main() : Mesh {
    var body = withMaterial(cube(1.0, 1.0, 1.0), "body");
    var top = withMaterial(translate(sphere(0.5, 8, 4), Vec3 { 0.0, 0.75, 0.0 }), "top");
    return merge(body, top);
}
)"));
		ASSERT_EQ(2, f.result.parts.size());
		ASSERT_EQ(1, f.result.parts[0].material);
		ASSERT_EQ(0, f.result.parts[0].first_index);
		ASSERT_EQ(36, f.result.parts[0].index_count);
		ASSERT_EQ(2, f.result.parts[1].material);
		ASSERT_EQ(36, f.result.parts[1].first_index);
		ASSERT_EQ(f.result.indices.size() - 36, f.result.parts[1].index_count);
	}
	{
		// parts of a merged mesh without parts use the default material
		Fixture f;
		ASSERT_TRUE(f.run(R"(
import "core:procedural_geom"
fn main() : Mesh { return merge(cube(1.0, 1.0, 1.0), withMaterial(cube(1.0, 1.0, 1.0), "crate")); }
)"));
		ASSERT_EQ(2, f.result.parts.size());
		ASSERT_EQ(0, f.result.parts[0].material);
		ASSERT_EQ(36, f.result.parts[0].index_count);
		ASSERT_EQ(1, f.result.parts[1].material);
		ASSERT_EQ(36, f.result.parts[1].first_index);
	}
	{
		Fixture f;
		ASSERT_TRUE(f.run("import \"core:procedural_geom\"\nfn main() : Mesh { return sphere(1.0, 8, 4); }\n"));
		ASSERT_EQ(1, f.result.parts.size());
		ASSERT_EQ(0, f.result.parts[0].material);
		ASSERT_EQ(f.result.indices.size(), f.result.parts[0].index_count);
	}
	{
		// instances keep the material of the instance
		Fixture f;
		ASSERT_TRUE(f.run(R"(
import "core:procedural_geom"
fn main() : Mesh { return placeInstancesAtPoints(point({0.0, 0.0, 0.0}), withMaterial(cube(1.0, 1.0, 1.0), "crate")); }
)"));
		ASSERT_EQ(1, f.result.parts.size());
		ASSERT_EQ(1, f.result.parts[0].material);
		ASSERT_EQ(36, f.result.parts[0].index_count);
	}
	{
		Fixture f;
		ASSERT_TRUE(!f.run(R"(
import "core:procedural_geom"
fn main() : Mesh { return withMaterial(cube(1.0, 1.0, 1.0), "a/b"); }
)", true));
	}
	{
		Fixture f;
		ASSERT_TRUE(!f.run(R"(
import "core:procedural_geom"
fn main() : Mesh { return withMaterial(circle(1.0, 8), "roof"); }
)", true));
	}
	return true;
}

bool testProceduralMeshLods() {
	{
		Fixture f;
		ASSERT_TRUE(f.run(R"(
import "core:procedural_geom"
fn main() : Mesh {
    return merge(withLod(cube(1.0, 1.0, 1.0), 0), withLod(withMaterial(sphere(0.5, 8, 4), "roof"), 1));
}
)", false, 3));
		ASSERT_EQ(2, f.result.getLodCount());
		ASSERT_TRUE(f.result.hasLod(0));
		ASSERT_TRUE(f.result.hasLod(1));
		ASSERT_TRUE(!f.result.hasLod(2));
		ASSERT_EQ(36, f.result.parts[0].index_count);
		ASSERT_EQ(0, f.result.parts[0].lod);
		ASSERT_EQ(1, f.result.parts[1].lod);
		ASSERT_EQ(1, f.result.parts[1].material);
	}
	{
		// withLod keeps the material of the parts
		Fixture f;
		ASSERT_TRUE(f.run(R"(
import "core:procedural_geom"
fn main() : Mesh { return withLod(withMaterial(cube(1.0, 1.0, 1.0), "wood"), 0); }
)"));
		ASSERT_EQ(1, f.result.parts[0].material);
		ASSERT_EQ(0, f.result.parts[0].lod);
	}
	{
		// a level above lod_count is an error
		Fixture f;
		ASSERT_TRUE(!f.run(R"(
import "core:procedural_geom"
fn main() : Mesh { return merge(withLod(cube(1.0, 1.0, 1.0), 0), withLod(cube(0.5, 0.5, 0.5), 1)); }
)", true, 1));
	}
	{
		// level 0 must have triangles
		Fixture f;
		ASSERT_TRUE(!f.run(R"(
import "core:procedural_geom"
fn main() : Mesh { return withLod(cube(1.0, 1.0, 1.0), 1); }
)", true, 2));
	}
	{
		// a level that is a path is an error
		Fixture f;
		ASSERT_TRUE(!f.run(R"(
import "core:procedural_geom"
fn main() : Mesh { return merge(withLod(cube(1.0, 1.0, 1.0), 0), withLod(circle(1.0, 8), 1)); }
)", true, 2));
	}
	return true;
}

bool testProceduralMeshErrors() {
	{
		Fixture f;
		ASSERT_TRUE(!f.run(R"(
import "core:procedural_geom"
fn main() : Mesh { return circle(1.0, 8); }
)", true));
		ASSERT_TRUE(f.result.vertices.size() == 0);
	}
	{
		Fixture f;
		ASSERT_TRUE(!f.run(R"(
import "core:procedural_geom"
fn main() : Mesh { return cube(-1.0, 1.0, 1.0); }
)", true));
	}
	{
		Fixture f;
		ASSERT_TRUE(!f.run("fn main() : i32 { return 1; }", true));
	}
	{
		Fixture f;
		ASSERT_TRUE(!f.run(R"(
import "core:procedural_geom"
fn main() : Mesh { var m = empty(); return m; }
)", true));
	}
	return true;
}
} // namespace

void runProceduralMeshTests() {
	RUN_TEST(testProceduralMeshCube);
	RUN_TEST(testProceduralMeshMerge);
	RUN_TEST(testProceduralMeshDefaultRecipe);
	RUN_TEST(testProceduralMeshPagodaExample);
	RUN_TEST(testProceduralMeshHouseExample);
	RUN_TEST(testProceduralMeshMaterials);
	RUN_TEST(testProceduralMeshLods);
	RUN_TEST(testProceduralMeshErrors);
}
