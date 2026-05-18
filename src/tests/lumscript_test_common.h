#pragma once

#include "lumscript/lumscript.h"
#include "tests/common.h"

namespace Lumix::LumScriptTests {

static const char* SAMPLE = R"(
import "core:vec3"

fn add(a : Vec3, b : Vec3) : Vec3 {
	const x = a.x + b.x;
	const y : f32 = a.y + b.y;
	const z = a.z + b.z;
	return { x, y, z };
}

fn main() : void {
	var a : Vec3 = { 10.0, 20.0, 30.0 };
	const b = Vec3 { 40.0, 50.0, 60.0 };
	a = add(a, b);
	
	var i : i32 = 10;
	while i > 0 {
		a = Vec3 { a.x + i as f32, a.y, a.z };
		i -= 1;
	}
}
)";

} // namespace Lumix::LumScriptTests
