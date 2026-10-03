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
	EvoxImportFile files_storage[] = {
		{ toLs("a"), toLs(a_source) },
	};
	EvoxImportFiles files = { files_storage, lengthOf(files_storage) };
	CAPI_BEGIN(module, diagnostics);
	EXPECT_TRUE(ex_module_compile(module, toLs(main_source), makeStringView(__func__), &resolveEvoxImportC, &files));
	CAPI_RUNTIME(module, runtime);
	EXPECT_EQ(test_call(runtime, toLs("main")), EX_CALL_RESULT_OK);
	EXPECT_EQ(ex_task_to_i32(runtime, -1), 42);
	CAPI_END(module);
	return true;
}

TEST(ExternStructCanBeImported) {
	const char* main_source = R"(
		import "native" as native

		fn main() : i64 {
			var v = native.Padded { 1, 42 };
			return v.b;
		}
	)";
	const char* native_source = R"(
		extern struct Padded {
			a : i8;
			b : i64;
		}
	)";
	EvoxImportFile files_storage[] = {
		{ toLs("native"), toLs(native_source) },
	};
	EvoxImportFiles files = { files_storage, lengthOf(files_storage) };
	CAPI_BEGIN(module, diagnostics);
	EXPECT_TRUE(ex_module_compile(module, toLs(main_source), makeStringView(__func__), &resolveEvoxImportC, &files));
	CAPI_RUNTIME(module, runtime);
	EXPECT_EQ(test_call(runtime, toLs("main")), EX_CALL_RESULT_OK);
	EXPECT_EQ(ex_task_to_i64(runtime, -1), 42);
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
	EvoxImportFile files_storage[] = {
		{ toLs("a"), toLs(a_source) },
	};
	EvoxImportFiles files = { files_storage, lengthOf(files_storage) };
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
	EvoxImportFile file = { toLs("a"), toLs(a_source) };
	EvoxImportFiles files = { &file, 1 };
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
	EvoxImportFile files_storage[] = {
		{ toLs("a"), toLs(a_source) },
		{ toLs("b"), toLs(b_source) },
		{ toLs("base"), toLs(base_source) }
	};
	EvoxImportFiles files = { files_storage, lengthOf(files_storage) };
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
	EvoxImportFile files_storage[] = {
		{ toLs("a"), toLs(a_source) },
		{ toLs("b"), toLs(b_source) }
	};
	EvoxImportFiles files = { files_storage, lengthOf(files_storage) };
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
	EvoxImportFile files_storage[] = {
		{ toLs("lib"), toLs(lib_source) }
	};
	EvoxImportFiles files = { files_storage, lengthOf(files_storage) };
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
	EvoxImportFile files_storage[] = {
		{ toLs("a"), toLs(a_source) },
		{ toLs("b"), toLs(b_source) }
	};
	EvoxImportFiles files = { files_storage, lengthOf(files_storage) };
	EXPECT_COMPILE_FAIL_WITH_IMPORTS(main_source, files);
	return true;
}

TEST(ImportAliasMissingMemberReportsMemberName) {
	const char* main_source = R"(
		import "core:imgui" as imgui

		fn main() : void {
			imgui.beginWindow("Evox demo");
		}
	)";
	const char* imgui_source = R"(
		fn textUnformatted(text : []const u8) : void {}
		fn button(label : []const u8) : bool { return false; }
		fn endWindow() : void {}
	)";
	EvoxImportFile files_storage[] = {
		{ toLs("core:imgui"), toLs(imgui_source) }
	};
	EvoxImportFiles files = { files_storage, lengthOf(files_storage) };
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
	EvoxImportFile file = { toLs("math"), toLs(math_source) };
	EvoxImportFiles files = { &file, 1 };
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
	EvoxImportFile files_storage[] = {
		{ toLs("b"), toLs(b_source) },
		{ toLs("c"), toLs(c_source) }
	};
	EvoxImportFiles files = { files_storage, lengthOf(files_storage) };
	EXPECT_COMPILE_FAIL_WITH_IMPORTS(a_source, files);
	return true;
}

TEST(ImportAliasDoesNotReExportImportedSymbols) {
	const char* main_source = R"(
		import "a" as a

		fn main() : i32 {
			return a.load();
		}
	)";
	const char* a_source = R"(
		import "b"

		fn load() : i32 { return 1; }
	)";
	const char* b_source = R"(
		fn load() : i32 { return 2; }
	)";
	EvoxImportFile files_storage[] = {
		{ toLs("a"), toLs(a_source) },
		{ toLs("b"), toLs(b_source) }
	};
	EvoxImportFiles files = { files_storage, lengthOf(files_storage) };
	EXPECT_COMPILE_WITH_IMPORTS(main_source, files);
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
	EvoxImportFile files_storage[] = {
		{ toLs("a"), toLs(a_source) },
		{ toLs("b"), toLs(b_source) }
	};
	EvoxImportFiles files = { files_storage, lengthOf(files_storage) };
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
	EvoxImportFile file = { toLs("math"), toLs(math_source) };
	EvoxImportFiles files = { &file, 1 };
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
	EvoxImportFile files_storage[] = {
		{ toLs("a"), toLs(a_source) },
		{ toLs("b"), toLs(b_source) },
		{ toLs("c"), toLs(c_source) }
	};
	EvoxImportFiles files = { files_storage, lengthOf(files_storage) };
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
	EvoxImportFile file = { toLs("math"), toLs(math_source) };
	EvoxImportFiles files = { &file, 1 };
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
	EvoxImportFile file = { toLs("math"), toLs(math_source) };
	EvoxImportFiles files = { &file, 1 };
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
	EvoxImportFile file = { toLs("math"), toLs(math_source) };
	EvoxImportFiles files = { &file, 1 };
	EXPECT_COMPILE_WITH_IMPORTS(main_source, files);
	return true;
}

TEST(ImportedFunctionErrorIsNotReplacedByCallError) {
	const char* main_source = R"(
		import "lib" as lib
		fn main() : void {
			lib.update();
		}
	)";
	const char* lib_source = R"(
		fn update() : void {
			missing();
		}
	)";
	EvoxImportFile file = { toLs("lib"), toLs(lib_source) };
	EvoxImportFiles files = { &file, 1 };

	struct Diagnostics { char text[1024] = {}; } diagnostics;
	ex_host host = {};
	ex_default_arena_create(&host.arena);
	host.diagnostics_userdata = &diagnostics;
	host.print = [](void* userdata, ex_string_view message) {
		Diagnostics& diagnostics = *(Diagnostics*)userdata;
		size_t offset = strlen(diagnostics.text);
		size_t count = (size_t)message.length;
		if (count > sizeof(diagnostics.text) - offset - 1) count = sizeof(diagnostics.text) - offset - 1;
		memcpy(diagnostics.text + offset, message.begin, count);
		diagnostics.text[offset + count] = '\0';
	};

	ex_module* module = ex_module_create(&host);
	EXPECT_TRUE(module != nullptr);
	EXPECT_EQ(EX_RESULT_FAILURE, ex_module_compile(module, toLs(main_source), makeStringView(__func__), &resolveEvoxImportC, &files));
	EXPECT_TRUE(strstr(diagnostics.text, "Unknown identifier missing") != nullptr);
	EXPECT_TRUE(strstr(diagnostics.text, "Cannot call compile-time value") == nullptr);
	ex_module_destroy(module);
	ex_default_arena_destroy(&host.arena);
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
	ex_module* module = ex_module_create(&diagnostics.host);
	EXPECT_TRUE(module != nullptr);
	EXPECT_TRUE(!ex_module_compile(module, toLs(source), makeStringView(__func__), [](void*, ex_string_view, ex_string_view, ex_string_view*) {
		return 0;
	}, nullptr));
	ex_module_destroy(module);
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
	EvoxImportFile file = { toLs("math"), toLs(math_source) };
	EvoxImportFiles files = { &file, 1 };
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
	EvoxImportFile file = { toLs("math"), toLs(math_source) };
	EvoxImportFiles files = { &file, 1 };
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
	EvoxImportFile files_storage[] = {
		{ toLs("math_a"), toLs(math_a_source) },
		{ toLs("math_b"), toLs(math_b_source) }
	};
	EvoxImportFiles files = { files_storage, lengthOf(files_storage) };
	EXPECT_COMPILE_FAIL_WITH_IMPORTS(source, files);
	return true;
}

TEST(ImportCycleIsAllowed) {
	const char* source = R"(
		import "a"

		fn main() : i32 {
			return in_a();
		}
	)";
	const char* a_source = R"(
		import "b" as b

		fn in_a() : i32 {
			return b.in_b();
		}
	)";
	const char* b_source = R"(
		import "a"

		fn in_b() : i32 {
			return 2;
		}
	)";
	EvoxImportFile files_storage[] = {
		{ toLs("a"), toLs(a_source) },
		{ toLs("b"), toLs(b_source) }
	};
	EvoxImportFiles files = { files_storage, lengthOf(files_storage) };
	EXPECT_RUNTIME_WITH_IMPORTS(source, files, runtime,
		EXPECT_EQ(test_call(runtime, toLs("main")), EX_CALL_RESULT_OK);
		EXPECT_EQ(ex_task_to_i32(runtime, -1), 2);
	);
	return true;
}

TEST(ThreeModuleImportCycleIsAllowed) {
	const char* source = R"(
		import "a" as a

		fn main() : i32 {
			return a.value();
		}
	)";
	const char* a_source = R"(
		import "b" as b

		fn value() : i32 {
			return b.value();
		}
	)";
	const char* b_source = R"(
		import "c" as c

		fn value() : i32 {
			return c.value();
		}
	)";
	const char* c_source = R"(
		import "a" as a

		fn value() : i32 {
			return 3;
		}
	)";
	EvoxImportFile files_storage[] = {
		{ toLs("a"), toLs(a_source) },
		{ toLs("b"), toLs(b_source) },
		{ toLs("c"), toLs(c_source) }
	};
	EvoxImportFiles files = { files_storage, lengthOf(files_storage) };
	EXPECT_RUNTIME_WITH_IMPORTS(source, files, runtime,
		EXPECT_EQ(test_call(runtime, toLs("main")), EX_CALL_RESULT_OK);
		EXPECT_EQ(ex_task_to_i32(runtime, -1), 3);
	);
	return true;
}

TEST(CyclicImportsResolveTypesAndValues) {
	const char* source = R"(
		import "a" as a

		fn main() : i32 {
			return a.read().value;
		}
	)";
	const char* a_source = R"(
		import "b" as b

		struct AValue {
			value : i32;
		}

		fn read() : AValue {
			return AValue { b.value };
		}
	)";
	const char* b_source = R"(
		import "a" as a

		const value : i32 = 7;
		fn read_a(v : a.AValue) : i32 {
			return v.value;
		}
	)";
	EvoxImportFile files_storage[] = {
		{ toLs("a"), toLs(a_source) },
		{ toLs("b"), toLs(b_source) }
	};
	EvoxImportFiles files = { files_storage, lengthOf(files_storage) };
	EXPECT_RUNTIME_WITH_IMPORTS(source, files, runtime,
		EXPECT_EQ(test_call(runtime, toLs("main")), EX_CALL_RESULT_OK);
		EXPECT_EQ(ex_task_to_i32(runtime, -1), 7);
	);
	return true;
}

TEST(CyclicComptimeDefinitionStillFails) {
	const char* source = R"(
		import "a"

		fn main() : i32 {
			return 0;
		}
	)";
	const char* a_source = R"(
		import "b" as b
		comptime value : i32 = b.value;
	)";
	const char* b_source = R"(
		import "a" as a
		comptime value : i32 = a.value;
	)";
	EvoxImportFile files_storage[] = {
		{ toLs("a"), toLs(a_source) },
		{ toLs("b"), toLs(b_source) }
	};
	EvoxImportFiles files = { files_storage, lengthOf(files_storage) };
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

	EvoxImportFile files_storage[] = {
		{ toLs("entity_mod"), toLs(entity_source) },
		{ toLs("helper_mod"), toLs(helper_source) }
	};
	EvoxImportFiles files = { files_storage, lengthOf(files_storage) };

	// e.destroy() = 2 (qualified), x.destroy() = 7 (method syntax prefers the
	// receiver type's unit), destroy(x) = 3 (plain call stays lexical)
	EXPECT_RUNTIME_WITH_IMPORTS(main_source, files, runtime,
		EXPECT_EQ(EX_CALL_RESULT_OK, test_call(runtime, toLs("main")));
		EXPECT_EQ(12, ex_task_to_i32(runtime, -1));
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

	EvoxImportFile files_storage[] = {
		{ toLs("entity_mod"), toLs(entity_source) }
	};
	EvoxImportFiles files = { files_storage, lengthOf(files_storage) };

	// x.destroy() binds to entity_mod.destroy (receiver's unit), not the local fn
	EXPECT_RUNTIME_WITH_IMPORTS(main_source, files, runtime,
		EXPECT_EQ(EX_CALL_RESULT_OK, test_call(runtime, toLs("main")));
		EXPECT_EQ(7, ex_task_to_i32(runtime, -1));
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

	EvoxImportFile files_storage[] = {
		{ toLs("entity_mod"), toLs(entity_source) }
	};
	EvoxImportFiles files = { files_storage, lengthOf(files_storage) };

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

	EvoxImportFile files_storage[] = {
		{ toLs("entity_mod"), toLs(entity_source) },
		{ toLs("helper_mod"), toLs(helper_source) }
	};
	EvoxImportFiles files = { files_storage, lengthOf(files_storage) };

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

	EvoxImportFile files_storage[] = {
		{ toLs("world"), toLs(world_source) },
		{ toLs("entity"), toLs(entity_source) }
	};
	EvoxImportFiles files = { files_storage, lengthOf(files_storage) };
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

	EvoxImportFile files_storage[] = {
		{ toLs("entity"), toLs(entity_source) }
	};
	EvoxImportFiles files = { files_storage, lengthOf(files_storage) };
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

	EvoxImportFile files_storage[] = {
		{ toLs("a"), toLs(a_source) },
		{ toLs("b"), toLs(b_source) },
	};
	EvoxImportFiles files = { files_storage, lengthOf(files_storage) };
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

	EvoxImportFile files_storage[] = {
		{ toLs("a"), toLs(a_source) },
		{ toLs("b"), toLs(b_source) },
	};
	EvoxImportFiles files = { files_storage, lengthOf(files_storage) };
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

	EvoxImportFile files_storage[] = {
		{ toLs("world"), toLs(world_source) },
		{ toLs("entity"), toLs(entity_source) }
	};
	EvoxImportFiles files = { files_storage, lengthOf(files_storage) };
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

	EvoxImportFile files_storage[] = {
		{ toLs("math"), toLs(math_source) }
	};
	EvoxImportFiles files = { files_storage, lengthOf(files_storage) };

	TestContext diagnostics;
	ex_module* module = ex_module_create(&diagnostics.host);
	EXPECT_TRUE(module != nullptr);
	EXPECT_TRUE(ex_module_compile(module, toLs(main_source), makeStringView(__func__), &resolveEvoxImportC, &files));

	ex_bytecode* bytecode = ex_bytecode_compile(module, &diagnostics.host, nullptr);
	EXPECT_TRUE(bytecode != nullptr);

	ex_runtime* runtime = ex_runtime_create(bytecode, nullptr);
	EXPECT_TRUE(runtime != nullptr);
	EXPECT_TRUE(ex_runtime_set_native_resolver(test_vm(runtime), [](ex_runtime*, ex_native_function_desc, void*) -> ex_native_fn {
		return [](ex_runtime*, ex_call_frame frame) {
			EX_ARG(frame, i32, a);
			EX_ARG(frame, i32, b);
			EX_RESULT(frame, a + b);
		};
	}, nullptr) == EX_RESULT_OK);

	EXPECT_EQ(EX_CALL_RESULT_OK, test_call(runtime, toLs("main")));
	EXPECT_EQ(63, ex_task_to_i32(runtime, -1));

	test_runtime_destroy(runtime);
	ex_bytecode_destroy(bytecode);
	ex_module_destroy(module);
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

	EvoxImportFile files_storage[] = {
		{ toLs("math"), toLs(math_source) }
	};
	EvoxImportFiles files = { files_storage, lengthOf(files_storage) };

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

	EvoxImportFile files_storage[] = {
		{ toLs("math"), toLs(math_source) }
	};
	EvoxImportFiles files = { files_storage, lengthOf(files_storage) };

	EXPECT_COMPILE_FAIL_WITH_IMPORTS(main_source, files);
	return true;
}

TEST(CoreMathImportRuntime) {
	// every std:math function in its f32 and f64 form, plus the pi constants
	const char* source = R"(
		import "std:math" as math

		fn sin32() : f32 { return math.sin(0.0); }
		fn cos32() : f32 { return math.cos(0.0); }
		fn tan32() : f32 { return math.tan(0.0); }
		fn asin32() : f32 { return math.asin(1.0); }
		fn acos32() : f32 { return math.acos(1.0); }
		fn atan32() : f32 { return math.atan(1.0); }
		fn exp32() : f32 { return math.exp(0.0); }
		fn log32() : f32 { return math.log(1.0); }
		fn sqrt32() : f32 { return math.sqrt(9.0); }
		fn floor32() : f32 { return math.floor(-1.5); }
		fn ceil32() : f32 { return math.ceil(-1.5); }
		fn round32() : f32 { return math.round(2.5); }
		fn abs32() : f32 { return math.abs(-3.0); }
		fn pow32() : f32 { return math.pow(4.0, 0.5); }
		fn atan232() : f32 { return math.atan2(1.0, 1.0); }
		fn min32() : f32 { return math.min(2.0 as f32, -1.0 as f32); }
		fn max32() : f32 { return math.max(2.0 as f32, -1.0 as f32); }
		fn hypot32() : f32 { return math.hypot(3.0, 4.0); }
		fn fmod32() : f32 { return math.fmod(7.5, 2.0); }
		fn pi32() : f32 { return math.pi; }
		fn sin64() : f64 { return math.sin_f64(0.0); }
		fn cos64() : f64 { return math.cos_f64(0.0); }
		fn tan64() : f64 { return math.tan_f64(0.0); }
		fn asin64() : f64 { return math.asin_f64(1.0); }
		fn acos64() : f64 { return math.acos_f64(1.0); }
		fn atan64() : f64 { return math.atan_f64(1.0); }
		fn exp64() : f64 { return math.exp_f64(0.0); }
		fn log64() : f64 { return math.log_f64(1.0); }
		fn sqrt64() : f64 { return math.sqrt_f64(16.0); }
		fn floor64() : f64 { return math.floor_f64(3000000000.5); }
		fn ceil64() : f64 { return math.ceil_f64(3000000000.5); }
		fn round64() : f64 { return math.round_f64(2.5); }
		fn abs64() : f64 { return math.abs_f64(-0.25); }
		fn pow64() : f64 { return math.pow_f64(4.0, 0.5); }
		fn atan264() : f64 { return math.atan2_f64(1.0, -1.0); }
		fn min64() : f64 { return math.min(2.0, 1.5); }
		fn max64() : f64 { return math.max(2.0, 1.5); }
		fn minInt() : i32 { return math.min(3, 7); }
		fn maxInt() : i32 { return math.max(-3, -7); }
		fn hypot64() : f64 { return math.hypot_f64(5.0, 12.0); }
		fn fmod64() : f64 { return math.fmod_f64(7.5, 2.0); }
		fn pi64() : f64 { return math.pi; }
	)";

	CAPI_BEGIN(module, diagnostics);
	EXPECT_TRUE(ex_module_compile(module, toLs(source), makeStringView(__func__), nullptr, nullptr));

	CAPI_RUNTIME(module, runtime);
	struct Case32 { const char* name; float expected; };
	const Case32 cases32[] = {
		{"sin32", 0.0f}, {"cos32", 1.0f}, {"tan32", 0.0f}, {"asin32", 1.5707964f}, {"acos32", 0.0f}, {"atan32", 0.7853982f}, {"exp32", 1.0f},
		{"log32", 0.0f}, {"sqrt32", 3.0f}, {"floor32", -2.0f}, {"ceil32", -1.0f}, {"round32", 3.0f}, {"abs32", 3.0f}, {"pow32", 2.0f},
		{"atan232", 0.7853982f}, {"min32", -1.0f}, {"max32", 2.0f}, {"hypot32", 5.0f}, {"fmod32", 1.5f}, {"pi32", 3.1415927f}
	};
	for (const Case32& c : cases32) {
		EXPECT_EQ(EX_CALL_RESULT_OK, test_call(runtime, toLs(c.name)));
		EXPECT_FLOAT_EQ(c.expected, ex_task_to_f32(runtime, -1));
	}
	struct Case64 { const char* name; double expected; };
	const Case64 cases64[] = {
		{"sin64", 0.0}, {"cos64", 1.0}, {"tan64", 0.0}, {"asin64", 1.5707963267948966}, {"acos64", 0.0}, {"atan64", 0.7853981633974483}, {"exp64", 1.0},
		{"log64", 0.0}, {"sqrt64", 4.0}, {"floor64", 3000000000.0}, {"ceil64", 3000000001.0}, {"round64", 3.0}, {"abs64", 0.25}, {"pow64", 2.0},
		{"atan264", 2.356194490192345}, {"min64", 1.5}, {"max64", 2.0}, {"hypot64", 13.0}, {"fmod64", 1.5}, {"pi64", 3.141592653589793}
	};
	for (const Case64& c : cases64) {
		EXPECT_EQ(EX_CALL_RESULT_OK, test_call(runtime, toLs(c.name)));
		EXPECT_TRUE(fabs(ex_task_to_f64(runtime, -1) - c.expected) < 1e-9);
	}
	// min and max are generic, so they also work on integers
	EXPECT_EQ(EX_CALL_RESULT_OK, test_call(runtime, toLs("minInt")));
	EXPECT_EQ(3, ex_task_to_i32(runtime, -1));
	EXPECT_EQ(EX_CALL_RESULT_OK, test_call(runtime, toLs("maxInt")));
	EXPECT_EQ(-3, ex_task_to_i32(runtime, -1));
	CAPI_END(module);
	return true;
}

TEST(ImportedUntypedComptimeNumericRuntime) {
	// an untyped comptime constant from another unit takes the type of its use, whatever its width
	const char* main_source = R"(
		import "consts" as c

		fn asF32() : f32 { return c.pi; }
		fn asF64() : f64 { return c.pi; }
		fn scaled(x : f32) : f32 { return x * c.pi; }
		fn intAsF32() : f32 { return c.n; }
		fn intAsI64() : i64 { return c.n; }
	)";
	const char* consts_source = R"(
		comptime pi = 3.14159265358979323846;
		comptime n = 7;
	)";
	EvoxImportFile files_storage[] = {
		{ toLs("consts"), toLs(consts_source) }
	};
	EvoxImportFiles files = { files_storage, lengthOf(files_storage) };
	CAPI_BEGIN(module, diagnostics);
	EXPECT_TRUE(ex_module_compile(module, toLs(main_source), makeStringView(__func__), &resolveEvoxImportC, &files));

	CAPI_RUNTIME(module, runtime);
	EXPECT_EQ(EX_CALL_RESULT_OK, test_call(runtime, toLs("asF32")));
	EXPECT_FLOAT_EQ(3.1415927f, ex_task_to_f32(runtime, -1));
	EXPECT_EQ(EX_CALL_RESULT_OK, test_call(runtime, toLs("asF64")));
	EXPECT_TRUE(fabs(ex_task_to_f64(runtime, -1) - 3.141592653589793) < 1e-12);
	EXPECT_EQ(EX_CALL_RESULT_OK, test_call(runtime, toLs("intAsF32")));
	EXPECT_FLOAT_EQ(7.0f, ex_task_to_f32(runtime, -1));
	EXPECT_EQ(EX_CALL_RESULT_OK, test_call(runtime, toLs("intAsI64")));
	EXPECT_EQ(7, ex_task_to_i64(runtime, -1));
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
	EvoxImportFile files_storage[] = {
		{ toLs("math"), toLs(math_source) },
		{ toLs("state"), toLs(state_source) }
	};
	EvoxImportFiles files = { files_storage, lengthOf(files_storage) };
	CAPI_BEGIN(module, diagnostics);
	EXPECT_TRUE(ex_module_compile(module, toLs(main_source), makeStringView(__func__), &resolveEvoxImportC, &files));

	CAPI_RUNTIME(module, runtime);
	EXPECT_EQ(EX_CALL_RESULT_OK, test_call(runtime, toLs("main")));
	EXPECT_EQ(42, ex_task_to_i32(runtime, -1));
	CAPI_END(module);
	return true;
}

// Two imports each contribute one extern fn after the root unit's main function.
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
	EvoxImportFile files_storage[] = {
		{ toLs("lib_a"), toLs(lib_a_source) },
		{ toLs("lib_b"), toLs(lib_b_source) },
	};
	EvoxImportFiles files = { files_storage, lengthOf(files_storage) };

	TestContext diagnostics;
	ex_module* module = ex_module_create(&diagnostics.host);
	EXPECT_TRUE(module != nullptr);
	EXPECT_TRUE(ex_module_compile(module, toLs(main_source), makeStringView(__func__), &resolveEvoxImportC, &files));

	ex_bytecode* bytecode = ex_bytecode_compile(module, &diagnostics.host, nullptr);
	EXPECT_TRUE(bytecode != nullptr);

	ex_runtime* runtime = ex_runtime_create(bytecode, nullptr);
	EXPECT_TRUE(runtime != nullptr);
	EXPECT_TRUE(ex_runtime_set_native_resolver(test_vm(runtime), [](ex_runtime*, ex_native_function_desc function, void*) -> ex_native_fn {
		if (equalStrings(function.unit_path, "lib_a") && equalStrings(function.name, "add")) {
			return [](ex_runtime*, ex_call_frame frame) {
				EX_ARG(frame, i32, a); EX_ARG(frame, i32, b);
				EX_RESULT(frame, a + b);
			};
		}
		if (equalStrings(function.unit_path, "lib_b") && equalStrings(function.name, "mul")) {
			return [](ex_runtime*, ex_call_frame frame) {
				EX_ARG(frame, i32, a); EX_ARG(frame, i32, b);
				EX_RESULT(frame, a * b);
			};
		}
		return nullptr;
	}, nullptr) == EX_RESULT_OK);

	EXPECT_EQ(EX_CALL_RESULT_OK, test_call(runtime, toLs("main")));
	EXPECT_EQ(21, ex_task_to_i32(runtime, -1)); // 3 * 7 = 21, not 3 + 7 = 10

	test_runtime_destroy(runtime);
	ex_bytecode_destroy(bytecode);
	ex_module_destroy(module);
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
	EvoxImportFile files_storage[] = {
		{ toLs("arr"), toLs(arr_source) },
	};
	EvoxImportFiles files = { files_storage, lengthOf(files_storage) };
	EXPECT_COMPILE_WITH_IMPORTS(main_source, files);
	return true;
}

