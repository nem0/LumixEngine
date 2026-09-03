TEST(ModuleDefinitionAt) {
	TestContext context;
	const char* source =
		"fn foo() : void {\n"
		"}\n"
		"fn main() : void {\n"
		"    foo();\n"
		"}\n";
	ex_module* module = ex_module_create(&context.host);
	EXPECT_TRUE(module != nullptr);

	const ex_string_view text = makeStringView(source);
	const ex_string_view path = makeStringView("definition_test.evox");
	EXPECT_EQ(EX_RESULT_OK, ex_module_parse(module, text, path));
	EXPECT_EQ(EX_RESULT_OK, ex_module_typecheck(module));

	ex_definition_location location = {};
	EXPECT_EQ(EX_RESULT_OK, ex_module_definition_at(module, path, 3, 5, &location));
	EXPECT_TRUE(equalStrings(location.source_name, path));
	EXPECT_EQ(0, location.line);
	EXPECT_EQ(3, location.column);
	EXPECT_EQ(3, location.length);

	// The character immediately after an identifier is still considered part
	// of that identifier for editor cursor purposes.
	location = {};
	EXPECT_EQ(EX_RESULT_OK, ex_module_definition_at(module, path, 3, 7, &location));
	EXPECT_EQ(0, location.line);
	EXPECT_EQ(3, location.column);

	ex_module_destroy(module);
	return true;
}

TEST(ModuleDefinitionAtLocalVar) {
	TestContext context;
	const char* source =
		"fn main() : i32 {\n"
		"    var value : i32 = 1;\n"
		"    return value;\n"
		"}\n";
	ex_module* module = ex_module_create(&context.host);
	EXPECT_TRUE(module != nullptr);

	const ex_string_view text = makeStringView(source);
	const ex_string_view path = makeStringView("definition_local.evox");
	EXPECT_EQ(EX_RESULT_OK, ex_module_parse(module, text, path));
	EXPECT_EQ(EX_RESULT_OK, ex_module_typecheck(module));

	ex_definition_location location = {};
	EXPECT_EQ(EX_RESULT_OK, ex_module_definition_at(module, path, 2, 13, &location));
	EXPECT_TRUE(equalStrings(location.source_name, path));
	EXPECT_EQ(1, location.line);
	EXPECT_EQ(8, location.column);
	EXPECT_EQ(5, location.length);

	ex_module_destroy(module);
	return true;
}

TEST(ModuleDefinitionAtChoosesLaterLocalDeclaration) {
	TestContext context;
	const char* source =
		"fn main() : void {\n"
		"    if true {\n"
		"        var elem = 1;\n"
		"        elem;\n"
		"    }\n"
		"    if true {\n"
		"        var elem = 2;\n"
		"        elem;\n"
		"    }\n"
		"}\n";
	ex_module* module = ex_module_create(&context.host);
	EXPECT_TRUE(module != nullptr);
	const ex_string_view text = makeStringView(source);
	const ex_string_view path = makeStringView("definition_later_local.evox");
	EXPECT_EQ(EX_RESULT_OK, ex_module_parse(module, text, path));
	EXPECT_EQ(EX_RESULT_OK, ex_module_typecheck(module));

	ex_definition_location location = {};
	EXPECT_EQ(EX_RESULT_OK, ex_module_definition_at(module, path, 7, 9, &location));
	EXPECT_EQ(6, location.line);
	EXPECT_EQ(12, location.column);
	EXPECT_EQ(4, location.length);

	ex_module_destroy(module);
	return true;
}

TEST(ModuleDefinitionAtImportedFieldAccess) {
	TestContext context;
	const ex_string_view library = makeStringView("struct Element { ptr : cptr; }\nextern fn setVisible(object : Element, show : bool) : void;\n");
	const ex_string_view source = makeStringView("import \"lib\" as lib;\nfn main() : void {\n    var elem : lib.Element = undefined;\n    elem.setVisible(true);\n}\n");
	const ex_string_view library_path = makeStringView("lib");
	const ex_string_view source_path = makeStringView("main.evox");
	ex_module* module = ex_module_create(&context.host);
	EXPECT_TRUE(module != nullptr);
	EvoxImportFile import_file{library_path, library};
	EvoxImportFiles imports{&import_file, 1};
	EXPECT_EQ(EX_RESULT_OK, ex_module_compile(module, source, source_path, &resolveEvoxImportC, &imports));

	ex_definition_location location = {};
	EXPECT_EQ(EX_RESULT_OK, ex_module_definition_at(module, source_path, 3, 10, &location));
	EXPECT_TRUE(equalStrings(location.source_name, library_path));
	EXPECT_EQ(1, location.line);
	EXPECT_EQ(10, location.column);
	EXPECT_EQ(10, location.length);

	ex_module_destroy(module);
	return true;
}

TEST(ModuleDefinitionAtImportAlias) {
	TestContext context;
	const ex_string_view library = makeStringView("struct Element { value : i32; }\n");
	const ex_string_view source = makeStringView(
		"import \"lib\" as lib;\n"
		"fn main() : void {\n"
		"    var elem : lib.Element = undefined;\n"
		"}\n");
	const ex_string_view library_path = makeStringView("lib");
	const ex_string_view source_path = makeStringView("alias_main.evox");
	ex_module* module = ex_module_create(&context.host);
	EXPECT_TRUE(module != nullptr);
	EvoxImportFile import_file{library_path, library};
	EvoxImportFiles imports{&import_file, 1};
	EXPECT_EQ(EX_RESULT_OK, ex_module_compile(module, source, source_path, &resolveEvoxImportC, &imports));

	ex_definition_location location = {};
	EXPECT_EQ(EX_RESULT_OK, ex_module_definition_at(module, source_path, 2, 15, &location));
	EXPECT_TRUE(equalStrings(location.source_name, source_path));
	EXPECT_EQ(0, location.line);
	EXPECT_EQ(16, location.column);
	EXPECT_EQ(3, location.length);

	ex_module_destroy(module);
	return true;
}

TEST(ModuleDefinitionAtAttribute) {
	TestContext context;
	const ex_string_view source = makeStringView(
		"struct Tag { value : i32; }\n"
		"#[Tag { 1 }]\n"
		"struct Data { field : i32; }\n");
	const ex_string_view path = makeStringView("attribute_definition.evox");
	ex_module* module = ex_module_create(&context.host);
	EXPECT_TRUE(module != nullptr);
	EXPECT_EQ(EX_RESULT_OK, ex_module_compile(module, source, path, nullptr, nullptr));

	ex_definition_location location = {};
	EXPECT_EQ(EX_RESULT_OK, ex_module_definition_at(module, path, 1, 2, &location));
	EXPECT_TRUE(equalStrings(location.source_name, path));
	EXPECT_EQ(0, location.line);
	EXPECT_EQ(7, location.column);
	EXPECT_EQ(3, location.length);

	ex_module_destroy(module);
	return true;
}

TEST(ModuleDefinitionAtImportedAttribute) {
	TestContext context;
	const ex_string_view library = makeStringView("struct Data { value : i32; }\n");
	const ex_string_view source = makeStringView(
		"import \"ld\" as ld;\n"
		"#[ld.Data { 1 }]\n"
		"struct Main { value : i32; }\n");
	const ex_string_view library_path = makeStringView("ld");
	const ex_string_view source_path = makeStringView("main.evox");
	ex_module* module = ex_module_create(&context.host);
	EXPECT_TRUE(module != nullptr);
	EvoxImportFile import_file{library_path, library};
	EvoxImportFiles imports{&import_file, 1};
	EXPECT_EQ(EX_RESULT_OK, ex_module_compile(module, source, source_path, &resolveEvoxImportC, &imports));

	ex_definition_location location = {};
	EXPECT_EQ(EX_RESULT_OK, ex_module_definition_at(module, source_path, 1, 5, &location));
	EXPECT_TRUE(equalStrings(location.source_name, library_path));
	EXPECT_EQ(0, location.line);
	EXPECT_EQ(7, location.column);
	EXPECT_EQ(4, location.length);

	ex_module_destroy(module);
	return true;
}

TEST(ModuleDefinitionAtForVariable) {
	TestContext context;
	const ex_string_view source = makeStringView(
		"struct Event { action : i32; }\n"
		"fn main(events : []Event) : void {\n"
		"    for event in events {\n"
		"        match event.action {\n"
		"            case 0: {}\n"
		"        }\n"
		"    }\n"
		"}\n");
	const ex_string_view path = makeStringView("for_definition.evox");
	ex_module* module = ex_module_create(&context.host);
	EXPECT_TRUE(module != nullptr);
	EXPECT_EQ(EX_RESULT_OK, ex_module_compile(module, source, path, nullptr, nullptr));

	ex_definition_location location = {};
	EXPECT_EQ(EX_RESULT_OK, ex_module_definition_at(module, path, 3, 14, &location));
	EXPECT_TRUE(equalStrings(location.source_name, path));
	EXPECT_EQ(2, location.line);
	EXPECT_EQ(8, location.column);
	EXPECT_EQ(5, location.length);

	ex_module_destroy(module);
	return true;
}

TEST(ModuleDefinitionAtPairedForVariables) {
	TestContext context;
	const ex_string_view source = makeStringView(
		"struct Event { action : i32; }\n"
		"fn main(events : []Event) : void {\n"
		"    for iiii, event in events {\n"
		"        match event.action {\n"
		"            case 0: {}\n"
		"        }\n"
		"    }\n"
		"}\n");
	const ex_string_view path = makeStringView("paired_for_definition.evox");
	ex_module* module = ex_module_create(&context.host);
	EXPECT_TRUE(module != nullptr);
	EXPECT_EQ(EX_RESULT_OK, ex_module_compile(module, source, path, nullptr, nullptr));
	ex_definition_location location = {};
	EXPECT_EQ(EX_RESULT_OK, ex_module_definition_at(module, path, 3, 14, &location));
	EXPECT_EQ(2, location.line);
	EXPECT_EQ(14, location.column);
	EXPECT_EQ(5, location.length);
	location = {};
	EXPECT_EQ(EX_RESULT_OK, ex_module_definition_at(module, path, 2, 8, &location));
	EXPECT_EQ(2, location.line);
	EXPECT_EQ(8, location.column);
	EXPECT_EQ(4, location.length);
	ex_module_destroy(module);
	return true;
}

TEST(ModuleDefinitionAtGenericLocal) {
	TestContext context;
	const ex_string_view source = makeStringView(
		"fn get(T : comptime type, value : T) : T {\n"
		"    var local : T = value;\n"
		"    return local;\n"
		"}\n"
		"fn main() : i32 {\n"
		"    return get(i32, 1);\n"
		"}\n");
	const ex_string_view path = makeStringView("generic_local_definition.evox");
	ex_module* module = ex_module_create(&context.host);
	EXPECT_TRUE(module != nullptr);
	EXPECT_EQ(EX_RESULT_OK, ex_module_compile(module, source, path, nullptr, nullptr));

	ex_definition_location location = {};
	EXPECT_EQ(EX_RESULT_OK, ex_module_definition_at(module, path, 2, 11, &location));
	EXPECT_EQ(1, location.line);
	EXPECT_EQ(8, location.column);
	EXPECT_EQ(5, location.length);

	ex_module_destroy(module);
	return true;
}

TEST(ModuleDefinitionAtGenericStructField) {
	TestContext context;
	const ex_string_view source = makeStringView(
		"fn Box(T : comptime type) : type {\n"
		"    return struct { value : T; };\n"
		"}\n"
		"fn main() : i32 {\n"
		"    var box : Box(i32) = undefined;\n"
		"    return box.value;\n"
		"}\n");
	const ex_string_view path = makeStringView("generic_field_definition.evox");
	ex_module* module = ex_module_create(&context.host);
	EXPECT_TRUE(module != nullptr);
	EXPECT_EQ(EX_RESULT_OK, ex_module_compile(module, source, path, nullptr, nullptr));

	ex_definition_location location = {};
	EXPECT_EQ(EX_RESULT_OK, ex_module_definition_at(module, path, 5, 15, &location));
	EXPECT_EQ(1, location.line);
	EXPECT_EQ(20, location.column);
	EXPECT_EQ(5, location.length);

	ex_module_destroy(module);
	return true;
}

TEST(ModuleDefinitionAtEnumMember) {
	TestContext context;
	const ex_string_view source = makeStringView(
		"enum Color { Red, Green }\n"
		"fn main() : Color {\n"
		"    return Color.Green;\n"
		"}\n");
	const ex_string_view path = makeStringView("enum_definition.evox");
	ex_module* module = ex_module_create(&context.host);
	EXPECT_TRUE(module != nullptr);
	EXPECT_EQ(EX_RESULT_OK, ex_module_compile(module, source, path, nullptr, nullptr));

	ex_definition_location location = {};
	EXPECT_EQ(EX_RESULT_OK, ex_module_definition_at(module, path, 2, 17, &location));
	EXPECT_EQ(0, location.line);
	EXPECT_EQ(18, location.column);
	EXPECT_EQ(5, location.length);

	ex_module_destroy(module);
	return true;
}

TEST(ModuleDefinitionAtFunctionParameterType) {
	TestContext context;
	const ex_string_view source = makeStringView(
		"struct Value { number : i32; }\n"
		"fn consume(value : Value) : void {}\n");
	const ex_string_view path = makeStringView("parameter_type_definition.evox");
	ex_module* module = ex_module_create(&context.host);
	EXPECT_TRUE(module != nullptr);
	EXPECT_EQ(EX_RESULT_OK, ex_module_compile(module, source, path, nullptr, nullptr));

	ex_definition_location location = {};
	EXPECT_EQ(EX_RESULT_OK, ex_module_definition_at(module, path, 1, 19, &location));
	EXPECT_EQ(0, location.line);
	EXPECT_EQ(7, location.column);
	EXPECT_EQ(5, location.length);

	ex_module_destroy(module);
	return true;
}

TEST(ModuleDefinitionAtNoDefinition) {
	TestContext context;
	const char* source = "fn main() : void {\n    missing();\n}\n";
	ex_module* module = ex_module_create(&context.host);
	EXPECT_TRUE(module != nullptr);

	const ex_string_view text = makeStringView(source);
	const ex_string_view path = makeStringView("definition_missing.evox");
	EXPECT_EQ(EX_RESULT_OK, ex_module_parse(module, text, path));

	ex_definition_location location = {};
	EXPECT_EQ(EX_RESULT_FAILURE, ex_module_definition_at(module, path, 1, 5, &location));

	ex_module_destroy(module);
	return true;
}

TEST(ModuleDefinitionAtEnumMemberDeclaration) {
	TestContext context;
	const ex_string_view source = makeStringView("enum Color { Red, Green }\nfn main() : Color {\n    return Color.Green;\n}\n");
	const ex_string_view path = makeStringView("enum_member_decl.evox");
	ex_module* module = ex_module_create(&context.host);
	EXPECT_TRUE(module != nullptr);
	EXPECT_EQ(EX_RESULT_OK, ex_module_compile(module, source, path, nullptr, nullptr));

	// `Red` starts at column 13 on line 0; cursor on the declaration itself
	// should resolve to itself.
	ex_definition_location location = {};
	EXPECT_EQ(EX_RESULT_OK, ex_module_definition_at(module, path, 0, 14, &location));
	EXPECT_TRUE(equalStrings(location.source_name, path));
	EXPECT_EQ(0, location.line);
	EXPECT_EQ(13, location.column);
	EXPECT_EQ(3, location.length);

	ex_module_destroy(module);
	return true;
}

TEST(ModuleDefinitionAtStructFieldDeclaration) {
	TestContext context;
	const ex_string_view source = makeStringView("struct Value { number : i32; }\nfn main() : i32 {\n    var v : Value = undefined;\n    return v.number;\n}\n");
	const ex_string_view path = makeStringView("struct_field_decl.evox");
	ex_module* module = ex_module_create(&context.host);
	EXPECT_TRUE(module != nullptr);
	EXPECT_EQ(EX_RESULT_OK, ex_module_compile(module, source, path, nullptr, nullptr));

	// `number` field decl starts at column 15 on line 0.
	ex_definition_location location = {};
	EXPECT_EQ(EX_RESULT_OK, ex_module_definition_at(module, path, 0, 16, &location));
	EXPECT_TRUE(equalStrings(location.source_name, path));
	EXPECT_EQ(0, location.line);
	EXPECT_EQ(15, location.column);
	EXPECT_EQ(6, location.length);

	// Use site `v.number` resolves to the same declaration.
	location = {};
	EXPECT_EQ(EX_RESULT_OK, ex_module_definition_at(module, path, 3, 14, &location));
	EXPECT_EQ(0, location.line);
	EXPECT_EQ(15, location.column);
	EXPECT_EQ(6, location.length);

	ex_module_destroy(module);
	return true;
}

TEST(ModuleDefinitionAtFunctionParamDeclaration) {
	TestContext context;
	const ex_string_view source = makeStringView("struct Value { number : i32; }\nfn consume(value : Value) : void {}\n");
	const ex_string_view path = makeStringView("param_decl.evox");
	ex_module* module = ex_module_create(&context.host);
	EXPECT_TRUE(module != nullptr);
	EXPECT_EQ(EX_RESULT_OK, ex_module_compile(module, source, path, nullptr, nullptr));

	// `value` param decl starts at column 11 on line 1; cursor on it is a self-jump.
	ex_definition_location location = {};
	EXPECT_EQ(EX_RESULT_OK, ex_module_definition_at(module, path, 1, 12, &location));
	EXPECT_TRUE(equalStrings(location.source_name, path));
	EXPECT_EQ(1, location.line);
	EXPECT_EQ(11, location.column);
	EXPECT_EQ(5, location.length);

	ex_module_destroy(module);
	return true;
}

TEST(ModuleDefinitionAtVarDeclarationSelf) {
	TestContext context;
	const ex_string_view source = makeStringView("fn main() : i32 {\n    var value : i32 = 1;\n    return value;\n}\n");
	const ex_string_view path = makeStringView("var_decl_self.evox");
	ex_module* module = ex_module_create(&context.host);
	EXPECT_TRUE(module != nullptr);
	EXPECT_EQ(EX_RESULT_OK, ex_module_compile(module, source, path, nullptr, nullptr));

	// `value` decl starts at column 8 on line 1.
	ex_definition_location location = {};
	EXPECT_EQ(EX_RESULT_OK, ex_module_definition_at(module, path, 1, 9, &location));
	EXPECT_TRUE(equalStrings(location.source_name, path));
	EXPECT_EQ(1, location.line);
	EXPECT_EQ(8, location.column);
	EXPECT_EQ(5, location.length);

	ex_module_destroy(module);
	return true;
}

TEST(ModuleDefinitionAtTopLevelSymbolDeclaration) {
	TestContext context;
	const ex_string_view source = makeStringView("fn foo() : void {\n}\nfn main() : void {\n    foo();\n}\n");
	const ex_string_view path = makeStringView("toplevel_decl.evox");
	ex_module* module = ex_module_create(&context.host);
	EXPECT_TRUE(module != nullptr);
	EXPECT_EQ(EX_RESULT_OK, ex_module_compile(module, source, path, nullptr, nullptr));

	// `foo` decl starts at column 3 on line 0.
	ex_definition_location location = {};
	EXPECT_EQ(EX_RESULT_OK, ex_module_definition_at(module, path, 0, 4, &location));
	EXPECT_TRUE(equalStrings(location.source_name, path));
	EXPECT_EQ(0, location.line);
	EXPECT_EQ(3, location.column);
	EXPECT_EQ(3, location.length);

	ex_module_destroy(module);
	return true;
}

TEST(ModuleDefinitionAtBracketFieldAccess) {
	TestContext context;
	const ex_string_view source = makeStringView("struct Value { x : i32; y : f32; }\nfn main() : i32 {\n    var value : Value = Value { 0, 0.0 };\n    return value[\"x\"];\n}\n");
	const ex_string_view path = makeStringView("bracket_field.evox");
	ex_module* module = ex_module_create(&context.host);
	EXPECT_TRUE(module != nullptr);
	EXPECT_EQ(EX_RESULT_OK, ex_module_compile(module, source, path, nullptr, nullptr));

	// `value["x"]` on line 3 should resolve to field `x` declared at (0, 15).
	// Column 18 is the `x` inside the string literal.
	ex_definition_location location = {};
	EXPECT_EQ(EX_RESULT_OK, ex_module_definition_at(module, path, 3, 18, &location));
	EXPECT_TRUE(equalStrings(location.source_name, path));
	EXPECT_EQ(0, location.line);
	EXPECT_EQ(15, location.column);
	EXPECT_EQ(1, location.length);

	ex_module_destroy(module);
	return true;
}

TEST(ModuleDefinitionAtInvalidArgs) {
	TestContext context;
	ex_module* module = ex_module_create(&context.host);
	EXPECT_TRUE(module != nullptr);
	const ex_string_view text = makeStringView("fn main() : void {}\n");
	const ex_string_view path = makeStringView("invalid_args.evox");
	EXPECT_EQ(EX_RESULT_OK, ex_module_parse(module, text, path));
	EXPECT_EQ(EX_RESULT_OK, ex_module_typecheck(module));

	ex_definition_location location = {};
	EXPECT_EQ(EX_RESULT_FAILURE, ex_module_definition_at(nullptr, path, 0, 1, &location));
	EXPECT_EQ(EX_RESULT_FAILURE, ex_module_definition_at(module, path, 0, 1, nullptr));
	// Unknown source path matches no unit.
	EXPECT_EQ(EX_RESULT_FAILURE, ex_module_definition_at(module, makeStringView("nope.evox"), 0, 1, &location));
	// Whitespace (no token covers the cursor) has no definition.
	EXPECT_EQ(EX_RESULT_FAILURE, ex_module_definition_at(module, path, 0, 2, &location));

	ex_module_destroy(module);
	return true;
}

TEST(ModuleDefinitionAtSurvivesSourceFree) {
	TestContext context;
	ex_module* module = ex_module_create(&context.host);
	EXPECT_TRUE(module != nullptr);
	// The module retains the source text, so the caller's buffer may be
	// released right after parsing/typechecking.
	const char* literal = "fn foo() : void {\n}\nfn main() : void {\n    foo();\n}\n";
	const size_t len = strlen(literal);
	char* scratch = (char*)malloc(len);
	EXPECT_TRUE(scratch != nullptr);
	memcpy(scratch, literal, len);
	const ex_string_view tmp{scratch, (i64)len};
	const ex_string_view path = makeStringView("owned_source.evox");
	EXPECT_EQ(EX_RESULT_OK, ex_module_parse(module, tmp, path));
	EXPECT_EQ(EX_RESULT_OK, ex_module_typecheck(module));
	free(scratch);

	ex_definition_location location = {};
	EXPECT_EQ(EX_RESULT_OK, ex_module_definition_at(module, path, 3, 5, &location));
	EXPECT_TRUE(equalStrings(location.source_name, path));
	EXPECT_EQ(0, location.line);
	EXPECT_EQ(3, location.column);
	EXPECT_EQ(3, location.length);

	ex_module_destroy(module);
	return true;
}
