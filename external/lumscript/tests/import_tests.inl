TEST(ImportConst) {
	const char* main_source = R"(
		import "a" as x

		fn main() : i32 {
			return x.value;
		}
	)";
	const char* a_source = R"(
		const value : i32 = 42; 
	)";
	LumScriptImportFile files_storage[] = {
		{ toLs("a"), toLs(a_source) },
	};
	LumScriptImportFiles files = { files_storage, lengthOf(files_storage) };
	CAPI_BEGIN(module, diagnostics);
	EXPECT_TRUE(ls_module_compile(module, toLs(main_source), makeStringView(__func__), &resolveLumScriptImportC, &files));
	CAPI_RUNTIME(module, runtime);
	EXPECT_EQ(ls_call(runtime, toLs("main")), LS_RESULT_OK);
	EXPECT_EQ(ls_to_i32(runtime, -1), 42);
	CAPI_END(module);
	return true;
}

TEST(QualifiedNonFunctionCallFails) {
	const char* main_source = R"(
		import "a" as a

		fn main() : i32 {
			return a.value();
		}
	)";
	const char* a_source = R"(
		const value : i32 = 42;
	)";
	LumScriptImportFile files_storage[] = {
		{ toLs("a"), toLs(a_source) },
	};
	LumScriptImportFiles files = { files_storage, lengthOf(files_storage) };
	EXPECT_COMPILE_FAIL_WITH_IMPORTS(main_source, files);
	return true;
}

TEST(UnaliasedImportedVariableIsAssignable) {
	const char* main_source = R"(
		import "a"

		fn main() : void {
			value = 42;
		}
	)";
	const char* a_source = R"(
		var value : i32 = 0;
	)";
	LumScriptImportFile file = { toLs("a"), toLs(a_source) };
	LumScriptImportFiles files = { &file, 1 };
	EXPECT_COMPILE_WITH_IMPORTS(main_source, files);
	return true;
}

TEST(DiamondImportTypechecks) {
	const char* main_source = R"(
		import "a" as a
		import "b" as b

		fn main() : i32 {
			return a.get_value() + b.get_value();
		}
	)";
	const char* a_source = R"(
		import "base"
		fn get_value() : i32 {
			return value;
		}
	)";
	const char* b_source = R"(
		import "base"
		fn get_value() : i32 {
			return value;
		}
	)";
	const char* base_source = R"(
		const value : i32 = 42;

		fn foo() : void {}
	)";
	LumScriptImportFile files_storage[] = {
		{ toLs("a"), toLs(a_source) },
		{ toLs("b"), toLs(b_source) },
		{ toLs("base"), toLs(base_source) }
	};
	LumScriptImportFiles files = { files_storage, lengthOf(files_storage) };
	EXPECT_COMPILE_WITH_IMPORTS(main_source, files);
	return true;
}

TEST(ImportPathCanMatchPreviousAlias) {
	const char* main_source = R"(
		import "a" as b
		import "b" as c

		fn main() : i32 {
			return b.one() + c.two();
		}
	)";
	const char* a_source = R"(
		fn one() : i32 {
			return 1;
		}
	)";
	const char* b_source = R"(
		fn two() : i32 {
			return 2;
		}
	)";
	LumScriptImportFile files_storage[] = {
		{ toLs("a"), toLs(a_source) },
		{ toLs("b"), toLs(b_source) }
	};
	LumScriptImportFiles files = { files_storage, lengthOf(files_storage) };
	EXPECT_COMPILE_WITH_IMPORTS(main_source, files);
	return true;
}

TEST(QualifiedDeclarationCanNotUseImportedPath) {
	const char* main_source = R"(
		import "lib"

		fn main() : i32 {
			return lib.get_value();
		}
	)";
	const char* lib_source = R"(
		fn get_value() : i32 {
			return 42;
		}
	)";
	LumScriptImportFile files_storage[] = {
		{ toLs("lib"), toLs(lib_source) }
	};
	LumScriptImportFiles files = { files_storage, lengthOf(files_storage) };
	EXPECT_COMPILE_FAIL_WITH_IMPORTS(main_source, files);
	return true;
}

TEST(QualifiedDeclarationRequiresDirectImport) {
	const char* main_source = R"(
		import "a"

		fn main() : i32 {
			return b.get_value();
		}
	)";
	const char* a_source = R"(
		import "b"

		fn use_a() : void {
		}
	)";
	const char* b_source = R"(
		fn get_value() : i32 {
			return 42;
		}
	)";
	LumScriptImportFile files_storage[] = {
		{ toLs("a"), toLs(a_source) },
		{ toLs("b"), toLs(b_source) }
	};
	LumScriptImportFiles files = { files_storage, lengthOf(files_storage) };
	EXPECT_COMPILE_FAIL_WITH_IMPORTS(main_source, files);
	return true;
}

TEST(ImportAliasMissingMemberReportsMemberName) {
	const char* main_source = R"(
		import "core:imgui" as imgui

		fn main() : void {
			imgui.beginWindow("LumScript demo");
		}
	)";
	const char* imgui_source = R"(
		fn textUnformatted(text : []const u8) : void {}
		fn button(label : []const u8) : bool { return false; }
		fn endWindow() : void {}
	)";
	LumScriptImportFile files_storage[] = {
		{ toLs("core:imgui"), toLs(imgui_source) }
	};
	LumScriptImportFiles files = { files_storage, lengthOf(files_storage) };
	EXPECT_COMPILE_FAIL_WITH_IMPORTS(main_source, files);
	return true;
}

TEST(ImportedStructTypeIsNotAValue) {
	const char* main_source = R"(
		import "math" as math

		fn main() : i32 {
			return math.Vec2;
		}
	)";
	const char* math_source = R"(
		struct Vec2 {
			x : i32;
			y : i32;
		}
	)";
	LumScriptImportFile file = { toLs("math"), toLs(math_source) };
	LumScriptImportFiles files = { &file, 1 };
	EXPECT_COMPILE_FAIL_WITH_IMPORTS(main_source, files);
	return true;
}

TEST(ImportsAreNotTransitiveAcrossModules) {
	const char* a_source = R"(
		import "b" as b
		import "c" as c

		fn main() : i32 {
			return b.get_value();
		}
	)";
	const char* b_source = R"(
		fn get_value() : i32 {
			return value;
		}
	)";
	const char* c_source = R"(
		const value : i32 = 42;
	)";
	LumScriptImportFile files_storage[] = {
		{ toLs("b"), toLs(b_source) },
		{ toLs("c"), toLs(c_source) }
	};
	LumScriptImportFiles files = { files_storage, lengthOf(files_storage) };
	EXPECT_COMPILE_FAIL_WITH_IMPORTS(a_source, files);
	return true;
}

TEST(ImportSymbolCollisionFails) {
	const char* main_source = R"(
		import "a"
		import "b"
		fn main() : i32 {
			return foo();
		}
	)";
	const char* a_source = R"(
		fn foo() : i32 { return 1; }
	)";
	const char* b_source = R"(
		fn foo() : i32 { return 2; }
	)";
	LumScriptImportFile files_storage[] = {
		{ toLs("a"), toLs(a_source) },
		{ toLs("b"), toLs(b_source) }
	};
	LumScriptImportFiles files = { files_storage, lengthOf(files_storage) };
	EXPECT_COMPILE_FAIL_WITH_IMPORTS(main_source, files);
	return true;
}

TEST(ImportAddsDeclarationsToCurrentModule) {
	const char* main_source = R"(
		import "math"

		fn main() : i32 {
			const v : Vec2 = Vec2 { 20, 22 };
			return sum(v);
		}
	)";
	const char* math_source = R"(
		struct Vec2 {
			x : i32;
			y : i32;
		}

		fn sum(v : Vec2) : i32 {
			return v.x + v.y;
		}
	)";
	LumScriptImportFile file = { toLs("math"), toLs(math_source) };
	LumScriptImportFiles files = { &file, 1 };
	EXPECT_COMPILE_WITH_IMPORTS(main_source, files);
	return true;
}

TEST(ImportVisibilityIsNotTransitive) {
	const char* main_source = R"(
		import "a"

		fn main() : void {
			const value : C = undefined;
		}
	)";
	const char* a_source = R"(
		import "b"

		fn use_a() : void {
		}
	)";
	const char* b_source = R"(
		import "c"
	)";
	const char* c_source = R"(
		struct C {
			x : i32;
		}
	)";
	LumScriptImportFile files_storage[] = {
		{ toLs("a"), toLs(a_source) },
		{ toLs("b"), toLs(b_source) },
		{ toLs("c"), toLs(c_source) }
	};
	LumScriptImportFiles files = { files_storage, lengthOf(files_storage) };
	EXPECT_COMPILE_FAIL_WITH_IMPORTS(main_source, files);
	return true;
}

TEST(UnaliasedImportCollidesWithLocalDeclaration) {
	const char* main_source = R"(
		import "math"

		fn foo() : i32 {
			return 1;
		}

		fn main() : i32 {
			return foo();
		}
	)";
	const char* math_source = R"(
		fn foo() : i32 {
			return 2;
		}
	)";
	LumScriptImportFile file = { toLs("math"), toLs(math_source) };
	LumScriptImportFiles files = { &file, 1 };
	EXPECT_COMPILE_FAIL_WITH_IMPORTS(main_source, files);
	return true;
}

TEST(UnaliasedImportCollidesWithLocalVariableUse) {
	const char* main_source = R"(
		import "math"

		const value : i32 = 1;

		fn main() : i32 {
			return value;
		}
	)";
	const char* math_source = R"(
		const value : i32 = 2;
	)";
	LumScriptImportFile file = { toLs("math"), toLs(math_source) };
	LumScriptImportFiles files = { &file, 1 };
	EXPECT_COMPILE_FAIL_WITH_IMPORTS(main_source, files);
	return true;
}

TEST(UnusedUnaliasedImportCollisionIsAllowed) {
	const char* main_source = R"(
		import "math"

		fn foo() : i32 {
			return 1;
		}

		fn main() : i32 {
			return 0;
		}
	)";
	const char* math_source = R"(
		fn foo() : i32 {
			return 2;
		}
	)";
	LumScriptImportFile file = { toLs("math"), toLs(math_source) };
	LumScriptImportFiles files = { &file, 1 };
	EXPECT_COMPILE_WITH_IMPORTS(main_source, files);
	return true;
}

TEST(MissingImportFails) {
	const char* source = R"(
		import "missing"

		fn main() : void {
		}
	)";
	EXPECT_COMPILE_FAIL(source);
	return true;
}

TEST(ImportResolverRejectsImportFails) {
	const char* source = R"(
		import "blocked"

		fn main() : void {
		}
	)";
	TestContext diagnostics;
	diagnostics.diagnostics.output_enabled = false;
	ls_module* module = ls_module_create(&diagnostics.host);
	EXPECT_TRUE(module != nullptr);
	EXPECT_TRUE(!ls_module_compile(module, toLs(source), makeStringView(__func__), [](void*, ls_string_view, ls_string_view, ls_string_view*) {
		return 0;
	}, nullptr));
	ls_module_destroy(module);
	return true;
}

TEST(DuplicateUnaliasedImportFails) {
	const char* main_source = R"(
		import "math"
		import "math"

		fn main() : i32 {
			const v : Vec2 = Vec2 { 20, 22 };
			return sum(v);
		}
	)";
	const char* math_source = R"(
		struct Vec2 {
			x : i32;
			y : i32;
		}

		fn sum(v : Vec2) : i32 {
			return v.x + v.y;
		}
	)";
	LumScriptImportFile file = { toLs("math"), toLs(math_source) };
	LumScriptImportFiles files = { &file, 1 };
	EXPECT_COMPILE_FAIL_WITH_IMPORTS(main_source, files);
	return true;
}

TEST(DuplicateAliasedImportOfSamePathFails) {
	const char* main_source = R"(
		import "math" as m
		import "math" as m

		fn main() : i32 {
			const v : m.Vec2 = m.Vec2 { 20, 22 };
			return m.sum(v);
		}
	)";
	const char* math_source = R"(
		struct Vec2 {
			x : i32;
			y : i32;
		}

		fn sum(v : Vec2) : i32 {
			return v.x + v.y;
		}
	)";
	LumScriptImportFile file = { toLs("math"), toLs(math_source) };
	LumScriptImportFiles files = { &file, 1 };
	EXPECT_COMPILE_FAIL_WITH_IMPORTS(main_source, files);
	return true;
}

TEST(AliasedImportCollisionFails) {
	const char* source = R"(
		import "math_a" as m
		import "math_b" as m

		fn main() : i32 {
			return 0;
		}
	)";
	const char* math_a_source = R"(
		fn one() : i32 {
			return 1;
		}
	)";
	const char* math_b_source = R"(
		fn two() : i32 {
			return 2;
		}
	)";
	LumScriptImportFile files_storage[] = {
		{ toLs("math_a"), toLs(math_a_source) },
		{ toLs("math_b"), toLs(math_b_source) }
	};
	LumScriptImportFiles files = { files_storage, lengthOf(files_storage) };
	EXPECT_COMPILE_FAIL_WITH_IMPORTS(source, files);
	return true;
}

TEST(ImportCycleFails) {
	const char* source = R"(
		import "a"

		fn main() : i32 {
			return 0;
		}
	)";
	const char* a_source = R"(
		import "b"

		fn in_a() : i32 {
			return 1;
		}
	)";
	const char* b_source = R"(
		import "a"

		fn in_b() : i32 {
			return 2;
		}
	)";
	LumScriptImportFile files_storage[] = {
		{ toLs("a"), toLs(a_source) },
		{ toLs("b"), toLs(b_source) }
	};
	LumScriptImportFiles files = { files_storage, lengthOf(files_storage) };
	EXPECT_COMPILE_FAIL_WITH_IMPORTS(source, files);
	return true;
}

TEST(UFCSNamespacePreferredOverLocalFunction) {
	const char* main_source = R"(
		import "entity_mod" as entity
		import "helper_mod" as e

		fn destroy(x : entity.Entity) : i32 {
			return 3;
		}

		fn main() : i32 {
			const x : entity.Entity = entity.Entity { 7 };
			return e.destroy() + x.destroy() + destroy(x);
		}
	)";

	const char* entity_source = R"(
		struct Entity {
			id : i32;
		}

		fn destroy(x : Entity) : i32 {
			return x.id;
		}
	)";

	const char* helper_source = R"(
		fn destroy() : i32 {
			return 2;
		}
	)";

	LumScriptImportFile files_storage[] = {
		{ toLs("entity_mod"), toLs(entity_source) },
		{ toLs("helper_mod"), toLs(helper_source) }
	};
	LumScriptImportFiles files = { files_storage, lengthOf(files_storage) };

	// e.destroy() = 2 (qualified), x.destroy() = 7 (method syntax prefers the
	// receiver type's unit), destroy(x) = 3 (plain call stays lexical)
	EXPECT_RUNTIME_WITH_IMPORTS(main_source, files, runtime,
		EXPECT_TRUE(ls_call(runtime, toLs("main")));
		EXPECT_EQ(12, ls_to_i32(runtime, -1));
	);
	return true;
}

TEST(UFCSPrefersNamespaceOverLocalFunction) {
	const char* main_source = R"(
		import "entity_mod" as entity

		fn destroy(x : entity.Entity) : i32 {
			return 3;
		}

		fn main() : i32 {
			const x : entity.Entity = entity.Entity { 7 };
			return x.destroy();
		}
	)";

	const char* entity_source = R"(
		struct Entity {
			id : i32;
		}

		fn destroy(x : Entity) : i32 {
			return x.id;
		}
	)";

	LumScriptImportFile files_storage[] = {
		{ toLs("entity_mod"), toLs(entity_source) }
	};
	LumScriptImportFiles files = { files_storage, lengthOf(files_storage) };

	// x.destroy() binds to entity_mod.destroy (receiver's unit), not the local fn
	EXPECT_RUNTIME_WITH_IMPORTS(main_source, files, runtime,
		EXPECT_TRUE(ls_call(runtime, toLs("main")));
		EXPECT_EQ(7, ls_to_i32(runtime, -1));
	);
	return true;
}



TEST(UFCSWithImportAliasResolvesToImportedFunction) {
	const char* main_source = R"(
		import "entity_mod" as entity

		fn destroy(x : entity.Entity) : i32 {
			return 3;
		}

		fn main() : i32 {
			const x : entity.Entity = entity.Entity { 7 };
			return x.destroy();
		}
	)";

	const char* entity_source = R"(
		struct Entity {
			id : i32;
		}
	)";

	LumScriptImportFile files_storage[] = {
		{ toLs("entity_mod"), toLs(entity_source) }
	};
	LumScriptImportFiles files = { files_storage, lengthOf(files_storage) };

	EXPECT_COMPILE_WITH_IMPORTS(main_source, files);
	return true;
}

TEST(UFCSWithUnaliasedImportResolvesToImportedFunction) {
	const char* main_source = R"(
		import "entity_mod" as entity
		import "helper_mod"

		fn main() : i32 {
			const x : entity.Entity = entity.Entity { 7 };
			return x.destroy();
		}
	)";

	const char* entity_source = R"(
		struct Entity {
			id : i32;
		}
	)";

	const char* helper_source = R"(
		import "entity_mod" as entity

		fn destroy(x : entity.Entity) : i32 {
			return x.id;
		}
	)";

	LumScriptImportFile files_storage[] = {
		{ toLs("entity_mod"), toLs(entity_source) },
		{ toLs("helper_mod"), toLs(helper_source) }
	};
	LumScriptImportFiles files = { files_storage, lengthOf(files_storage) };

	EXPECT_COMPILE_WITH_IMPORTS(main_source, files);
	return true;
}

TEST(ImportAliasEntityResolution) {
	const char* main_source = R"(
		import "world" as world
		import "entity" as entity

		fn main() : i32 {
			var w : world.World = world.World { 0 };
			var e : entity.Entity = world.createEntity(w);
			return 0;
		}
	)";

	const char* entity_source = R"(
		struct Entity { index : i32; }
	)";

	const char* world_source = R"(
		import "entity"
		struct World { world : i32; }
		extern fn createEntity(w : World) : Entity;
	)";

	LumScriptImportFile files_storage[] = {
		{ toLs("world"), toLs(world_source) },
		{ toLs("entity"), toLs(entity_source) }
	};
	LumScriptImportFiles files = { files_storage, lengthOf(files_storage) };
	EXPECT_COMPILE_WITH_IMPORTS(main_source, files);
	return true;
}

TEST(ImportedMethodCallResolvesCallerGlobalArgument) {
	const char* main_source = R"(
		import "entity" as entity

		const offset : i32 = 35;

		fn main() : i32 {
			const value : entity.Entity = entity.Entity { 7 };
			return value.add(offset);
		}
	)";

	const char* entity_source = R"(
		struct Entity {
			value : i32;
		}

		fn add(entity : Entity, amount : i32) : i32 {
			return entity.value + amount;
		}
	)";

	LumScriptImportFile files_storage[] = {
		{ toLs("entity"), toLs(entity_source) }
	};
	LumScriptImportFiles files = { files_storage, lengthOf(files_storage) };
	EXPECT_COMPILE_WITH_IMPORTS(main_source, files);
	return true;
}

TEST(ImportExternFnDuplicateUsed) {
	const char* main_source = R"(
		import "a"
		import "b"

		fn main() : i32 {
			foo(); // error - duplicate
		}
	)";

	const char* a_source = R"(
		extern fn foo() : i32;
	)";

	const char* b_source = R"(
		extern fn foo() : i32;
	)";

	LumScriptImportFile files_storage[] = {
		{ toLs("a"), toLs(a_source) },
		{ toLs("b"), toLs(b_source) },
	};
	LumScriptImportFiles files = { files_storage, lengthOf(files_storage) };
	EXPECT_COMPILE_FAIL_WITH_IMPORTS(main_source, files);
	return true;
}

TEST(ImportExternFnDuplicateNotUsed) {
	const char* main_source = R"(
		import "a"
		import "b"

		fn main() : i32 { return 0; }
	)";

	const char* a_source = R"(
		extern fn foo() : i32;
	)";

	const char* b_source = R"(
		extern fn foo() : i32;
	)";

	LumScriptImportFile files_storage[] = {
		{ toLs("a"), toLs(a_source) },
		{ toLs("b"), toLs(b_source) },
	};
	LumScriptImportFiles files = { files_storage, lengthOf(files_storage) };
	EXPECT_COMPILE_WITH_IMPORTS(main_source, files);
	return true;
}

TEST(ImportAliasExternFnReturnTypeRequiresDirectImport) {
	const char* main_source = R"(
		import "entity" as entity
		import "world" as world

		fn main() : i32 {
			var e : entity.Entity = world.createEntity();
			return e.index;
		}
	)";

	const char* entity_source = R"(
		struct Entity { index : i32; }
	)";

	const char* world_source = R"(
		extern fn createEntity() : Entity;
	)";

	LumScriptImportFile files_storage[] = {
		{ toLs("world"), toLs(world_source) },
		{ toLs("entity"), toLs(entity_source) }
	};
	LumScriptImportFiles files = { files_storage, lengthOf(files_storage) };
	EXPECT_COMPILE_FAIL_WITH_IMPORTS(main_source, files);
	return true;
}

TEST(ExternImport) {
	const char* main_source = R"(
		import "math" as m

		fn main() : i32 {
			const v1 : m.Vec2 = m.Vec2 { 10, 11 };
			const s1 : i32 = m.sum(v1); // with namespace
			const s3 : i32 = v1.sum(); // method-style namespace lookup
			const v2 : m.Vec2 = m.Vec2 { 9, 12 };
			const s2 : i32 = sum(v2); // inferred namespaced
			return s1 + s2 + s3;
		}
	)";
	
	const char* math_source = R"(
		struct Vec2 {
			x : i32;
			y : i32;
		}

		extern fn sum(v : Vec2) : i32;
	)";

	LumScriptImportFile files_storage[] = {
		{ toLs("math"), toLs(math_source) }
	};
	LumScriptImportFiles files = { files_storage, lengthOf(files_storage) };

	TestContext diagnostics;
	ls_module* module = ls_module_create(&diagnostics.host);
	EXPECT_TRUE(module != nullptr);
	EXPECT_TRUE(ls_module_compile(module, toLs(main_source), makeStringView(__func__), &resolveLumScriptImportC, &files));

	ls_bytecode* bytecode = ls_bytecode_compile(module, &diagnostics.host);
	EXPECT_TRUE(bytecode != nullptr);

	auto sumfn = [](ls_runtime* runtime, ls_call_frame frame) -> void {
		LS_ARG(frame, i32, a);
		LS_ARG(frame, i32, b);
		LS_RESULT(frame, a + b);
	};

	ls_runtime* runtime = ls_runtime_create(bytecode, nullptr);
	EXPECT_TRUE(runtime != nullptr);
	EXPECT_TRUE(setNativeFunctionCallback(runtime, module, toLs("math.sum"), sumfn) == LS_RESULT_OK);

	EXPECT_TRUE(ls_call(runtime, toLs("main")));
	EXPECT_EQ(63, ls_to_i32(runtime, -1));

	ls_runtime_destroy(runtime);
	ls_bytecode_destroy(bytecode);
	ls_module_destroy(module);
	return true;
}

TEST(ADLLocalFunctionLiteralPreferredOverNamespace) {
	const char* main_source = R"(
		import "math" as m

		const sum = fn(v : m.Vec2) : i32 {
			return sum(v);
		};

		fn main() : i32 {
			const v : m.Vec2 = m.Vec2 { 20, 21 };
			return sum(v);
		}
	)";

	const char* math_source = R"(
		struct Vec2 {
			x : i32;
			y : i32;
		}

		fn sum(v : Vec2) : i32 {
			return v.x + v.y;
		}
	)";

	LumScriptImportFile files_storage[] = {
		{ toLs("math"), toLs(math_source) }
	};
	LumScriptImportFiles files = { files_storage, lengthOf(files_storage) };

	EXPECT_COMPILE_WITH_IMPORTS(main_source, files);
	return true;
}

TEST(GlobalFunctionLiteralInitializerNamespaceCollisionFails) {
	const char* main_source = R"(
		import "math" as m

		const sum = fn(v : m.Vec2) : i32 {
			return sum(v, 1);
		};

		fn main() : i32 {
			const v : m.Vec2 = m.Vec2 { 20, 21 };
			return sum(v);
		}
	)";

	const char* math_source = R"(
		struct Vec2 {
			x : i32;
			y : i32;
		}

		fn sum(v : Vec2, offset : i32) : i32 {
			return v.x + v.y + offset;
		}
	)";

	LumScriptImportFile files_storage[] = {
		{ toLs("math"), toLs(math_source) }
	};
	LumScriptImportFiles files = { files_storage, lengthOf(files_storage) };

	EXPECT_COMPILE_FAIL_WITH_IMPORTS(main_source, files);
	return true;
}

TEST(CoreMathImportRuntime) {
	const char* source = R"(
		import "std:math" as math

		fn sin32() : f32 {
			return math.sin(0.0);
		}

		fn cos32() : f32 {
			return math.cos(0.0);
		}

		fn sin64() : f64 {
			return math.sin_f64(0.0);
		}

		fn cos64() : f64 {
			return math.cos_f64(0.0);
		}

		fn sqrt32() : f32 {
			return math.sqrt(9.0);
		}

		fn sqrt64() : f64 {
			return math.sqrt_f64(16.0);
		}

		fn pow32() : f32 {
			return math.pow(4.0, 0.5);
		}

		fn pow64() : f64 {
			return math.pow_f64(4.0, 0.5);
		}
	)";

	CAPI_BEGIN(module, diagnostics);
	EXPECT_TRUE(ls_module_compile(module, toLs(source), makeStringView(__func__), nullptr, nullptr));

	CAPI_RUNTIME(module, runtime);
	EXPECT_TRUE(ls_call(runtime, toLs("sin32")));
	EXPECT_FLOAT_EQ(0.0f, ls_to_f32(runtime, -1));
	EXPECT_TRUE(ls_call(runtime, toLs("cos32")));
	EXPECT_FLOAT_EQ(1.0f, ls_to_f32(runtime, -1));
	EXPECT_TRUE(ls_call(runtime, toLs("sin64")));
	EXPECT_FLOAT_EQ(0.0f, (float)ls_to_f64(runtime, -1));
	EXPECT_TRUE(ls_call(runtime, toLs("cos64")));
	EXPECT_FLOAT_EQ(1.0f, (float)ls_to_f64(runtime, -1));
	EXPECT_TRUE(ls_call(runtime, toLs("sqrt32")));
	EXPECT_FLOAT_EQ(3.0f, ls_to_f32(runtime, -1));
		EXPECT_TRUE(ls_call(runtime, toLs("sqrt64")));
		EXPECT_FLOAT_EQ(4.0f, (float)ls_to_f64(runtime, -1));
		EXPECT_TRUE(ls_call(runtime, toLs("pow32")));
		EXPECT_FLOAT_EQ(2.0f, ls_to_f32(runtime, -1));
		EXPECT_TRUE(ls_call(runtime, toLs("pow64")));
		EXPECT_FLOAT_EQ(2.0f, (float)ls_to_f64(runtime, -1));
	CAPI_END(module);
	return true;
}

TEST(AliasedImportRuntime) {
	const char* main_source = R"(
		import "math" as math
		import "state" as state

		fn main() : i32 {
			const v : math.Vec2 = math.Vec2 { 20, 22 };
			if state.is_running(state.State.Running) {
				return math.sum(v);
			}
			return 0;
		}
	)";
	const char* math_source = R"(
		struct Vec2 {
			x : i32;
			y : i32;
		}
		fn sum(v : Vec2) : i32 {
			return v.x + v.y;
		}
	)";
	const char* state_source = R"(
		enum State {
			Idle,
			Running
		}
		fn is_running(state : State) : bool {
			return state == .Running;
		}
	)";
	LumScriptImportFile files_storage[] = {
		{ toLs("math"), toLs(math_source) },
		{ toLs("state"), toLs(state_source) }
	};
	LumScriptImportFiles files = { files_storage, lengthOf(files_storage) };
	CAPI_BEGIN(module, diagnostics);
	EXPECT_TRUE(ls_module_compile(module, toLs(main_source), makeStringView(__func__), &resolveLumScriptImportC, &files));

	CAPI_RUNTIME(module, runtime);
	EXPECT_TRUE(ls_call(runtime, toLs("main")));
	EXPECT_EQ(42, ls_to_i32(runtime, -1));
	CAPI_END(module);
	return true;
}

// Two imports each contribute one extern fn. The flat native-function index must
// account for all units, so lib_b's fn is at index 1, not 0. If findNativeFunction
// (CanonicalName) forgets to count previous units, the wrong callback fires.
TEST(ExternFnSecondImportCorrectIndex) {
	const char* main_source = R"(
		import "lib_a" as a
		import "lib_b" as b

		fn main() : i32 {
			return b.mul(3, 7);
		}
	)";
	const char* lib_a_source = R"(
		extern fn add(x : i32, y : i32) : i32;
	)";
	const char* lib_b_source = R"(
		extern fn mul(x : i32, y : i32) : i32;
	)";
	LumScriptImportFile files_storage[] = {
		{ toLs("lib_a"), toLs(lib_a_source) },
		{ toLs("lib_b"), toLs(lib_b_source) },
	};
	LumScriptImportFiles files = { files_storage, lengthOf(files_storage) };

	TestContext diagnostics;
	ls_module* module = ls_module_create(&diagnostics.host);
	EXPECT_TRUE(module != nullptr);
	EXPECT_TRUE(ls_module_compile(module, toLs(main_source), makeStringView(__func__), &resolveLumScriptImportC, &files));

	ls_bytecode* bytecode = ls_bytecode_compile(module, &diagnostics.host);
	EXPECT_TRUE(bytecode != nullptr);

	ls_runtime* runtime = ls_runtime_create(bytecode, nullptr);
	EXPECT_TRUE(runtime != nullptr);
	EXPECT_TRUE(setNativeFunctionCallback(runtime, module, toLs("lib_a.add"), [](ls_runtime* rt, ls_call_frame frame) {
		LS_ARG(frame, i32, a); LS_ARG(frame, i32, b);
		LS_RESULT(frame, a + b);
	}) == LS_RESULT_OK);
	EXPECT_TRUE(setNativeFunctionCallback(runtime, module, toLs("lib_b.mul"), [](ls_runtime* rt, ls_call_frame frame) {
		LS_ARG(frame, i32, a); LS_ARG(frame, i32, b);
		LS_RESULT(frame, a * b);
	}) == LS_RESULT_OK);

	EXPECT_TRUE(ls_call(runtime, toLs("main")));
	EXPECT_EQ(21, ls_to_i32(runtime, -1)); // 3 * 7 = 21, not 3 + 7 = 10

	ls_runtime_destroy(runtime);
	ls_bytecode_destroy(bytecode);
	ls_module_destroy(module);
	return true;
}

// A local fn whose first parameter cannot take the UFCS receiver must not
// shadow the same-named fn from the receiver type's unit.
TEST(UfcsNotShadowedByLocalFunctionWithSameName) {
	const char* main_source = R"(
		import "arr" as arr

		fn init(x : i32) : void {}

		fn main() : void {
			var a : arr.Array = undefined;
			a.init();
			init(5);
		}
	)";
	const char* arr_source = R"(
		struct Array { size : isize; }

		fn init(array : Array) : void {}
	)";
	LumScriptImportFile files_storage[] = {
		{ toLs("arr"), toLs(arr_source) },
	};
	LumScriptImportFiles files = { files_storage, lengthOf(files_storage) };
	EXPECT_COMPILE_WITH_IMPORTS(main_source, files);
	return true;
}

