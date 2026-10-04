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
	bool run(const char* source, bool expect_error = false) {
		const bool success = compileProceduralMesh(*fs, source, Path("test.lpg"), result, error, getGlobalAllocator());
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
	RUN_TEST(testProceduralMeshErrors);
}
