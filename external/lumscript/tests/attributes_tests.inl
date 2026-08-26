TEST(AttributesCanBeAttachedToStructsAndFields) {
	const char* source = R"(
		struct tag {
			value : i32;
		}

		#[tag { 42 }]
		struct Settings {
			#[tag { 7 }]
			radius : f32;
		}
	)";
	EXPECT_COMPILE(source);
	return true;
}

TEST(MultipleAttributesPreserveDeclarationOrder) {
	const char* source = R"(
		struct tag { value : i32; }
		struct label { value : []const u8; }

		#[tag { 1 }, label { "settings" }]
		struct Settings {
			#[label { "radius" }, tag { 2 }]
			radius : f32;
		}
	)";
	EXPECT_COMPILE(source);
	return true;
}

TEST(AttributeMetadataIsAvailableThroughCAPI) {
	const char* source = R"(
		struct range { min : i32; max : i32; }
		#[range { 1, 9 }]
		struct Settings {
			#[range { 2, 3 }]
			value : i32;
		}

		fn main() : void {
			var settings : Settings = undefined;
			settings.value = 4;
		}
	)";

	CAPI_BEGIN(module, diagnostics);
	EXPECT_TRUE(ls_module_compile(module, toLs(source), makeStringView(__func__), nullptr, nullptr));
	CAPI_RUNTIME(module, runtime);
	EXPECT_TRUE(ls_debug_set_breakpoint(runtime.bytecode, makeStringView(__func__), 11u, nullptr));
	EXPECT_EQ((int)LS_RESULT_SUSPENDED, (int)ls_call(runtime, toLs("main")));

	const ls_type* settings_type = nullptr;
	for (u32 i = 0; i < ls_debug_frame_local_count(runtime, 0); ++i) {
		if (equalStrings(ls_debug_local_name(runtime, 0, i), toLs("settings"))) {
			settings_type = ls_debug_local_type(runtime, 0, i);
			break;
		}
	}
	EXPECT_TRUE(settings_type != nullptr);
	EXPECT_EQ(1u, ls_type_attribute_count(settings_type));
	ls_attribute type_attribute = ls_type_attribute_value(settings_type, 0);
	EXPECT_TRUE(type_attribute.type != nullptr);
	EXPECT_TRUE(equalStrings(ls_type_get_name(type_attribute.type), toLs("range")));
	EXPECT_EQ(2u, ls_type_struct_field_count(type_attribute.type));

	EXPECT_EQ(0u, ls_type_struct_field_attribute_count(settings_type, 0));
	ls_attribute field_attribute = ls_type_struct_field_attribute_value(settings_type, 0, 0);
	EXPECT_TRUE(field_attribute.type == nullptr);
	EXPECT_TRUE(field_attribute.value == nullptr);

	CAPI_END(module);
	return true;
}

TEST(AttributePayloadMustMatchItsStructType) {
	const char* source = R"(
		struct tag { value : i32; }
		#[tag { "not an integer" }]
		struct Settings {}
	)";
	EXPECT_COMPILE_FAIL(source);
	return true;
}
