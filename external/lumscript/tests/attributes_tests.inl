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

TEST(AttributesCanBeAttachedToExternStructsAndFields) {
	const char* source = R"(
		struct tag {
			value : i32;
		}

		#[tag { 42 }]
		extern struct NativeSettings {
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

TEST(UnusedAnnotatedStructIsAvailableThroughBytecodeTypeEnumeration) {
	const char* source = R"(
		struct component {}

		#[component {}]
		struct Inventory {
			capacity : i32;
		}

		fn main() : void {}
	)";

	CAPI_BEGIN(module, diagnostics);
	EXPECT_TRUE(ls_module_compile(module, toLs(source), makeStringView(__func__), nullptr, nullptr));
	CAPI_RUNTIME(module, runtime);

	const ls_type* inventory = nullptr;
	for (u32 i = 0, count = ls_bytecode_type_count(runtime.bytecode); i < count; ++i) {
		const ls_type* type = ls_bytecode_type(runtime.bytecode, i);
		if (ls_type_get_kind(type) == LS_TYPE_STRUCT && equalStrings(ls_type_get_name(type), toLs("Inventory"))) {
			inventory = type;
			break;
		}
	}
	EXPECT_TRUE(inventory != nullptr);
	EXPECT_EQ(1u, ls_type_attribute_count(inventory));
	const ls_attribute attribute = ls_type_attribute_value(inventory, 0);
	EXPECT_TRUE(attribute.type != nullptr);
	EXPECT_TRUE(equalStrings(ls_type_get_name(attribute.type), toLs("component")));
	EXPECT_EQ(0u, ls_bytecode_type_count(nullptr));
	EXPECT_TRUE(ls_bytecode_type(runtime.bytecode, ls_bytecode_type_count(runtime.bytecode)) == nullptr);
	EXPECT_TRUE(ls_bytecode_type(nullptr, 0) == nullptr);
	CAPI_END(module);
	return true;
}

TEST(BytecodeTypeMetadataExposesSizeAndAlignment) {
	const char* source = R"(
		struct component {}

		#[component {}]
		struct AlignedData {
			flag : i8;
			value : i64;
		}

		fn main() : void {}
	)";

	CAPI_BEGIN(module, diagnostics);
	EXPECT_TRUE(ls_module_compile(module, toLs(source), makeStringView(__func__), nullptr, nullptr));
	CAPI_RUNTIME(module, runtime);

	const ls_type* data = nullptr;
	for (u32 i = 0, count = ls_bytecode_type_count(runtime.bytecode); i < count; ++i) {
		const ls_type* type = ls_bytecode_type(runtime.bytecode, i);
		if (ls_type_get_kind(type) == LS_TYPE_STRUCT && equalStrings(ls_type_get_name(type), toLs("AlignedData"))) {
			data = type;
			break;
		}
	}
	EXPECT_TRUE(data != nullptr);
	EXPECT_EQ(16u, ls_type_get_size(data));
	EXPECT_EQ(8u, ls_type_get_alignment(data));
	EXPECT_EQ(0u, ls_type_get_alignment(nullptr));
	CAPI_END(module);
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

	i32 min = 0;
	i32 max = 0;
	memcpy(&min, (const u8*)type_attribute.value + ls_type_struct_field_offset(type_attribute.type, 0), sizeof(min));
	memcpy(&max, (const u8*)type_attribute.value + ls_type_struct_field_offset(type_attribute.type, 1), sizeof(max));
	EXPECT_EQ(1, min);
	EXPECT_EQ(9, max);

	EXPECT_EQ(1u, ls_type_struct_field_attribute_count(settings_type, 0));
	ls_attribute field_attribute = ls_type_struct_field_attribute_value(settings_type, 0, 0);
	EXPECT_TRUE(field_attribute.type != nullptr);
	EXPECT_TRUE(equalStrings(ls_type_get_name(field_attribute.type), toLs("range")));
	memcpy(&min, (const u8*)field_attribute.value + ls_type_struct_field_offset(field_attribute.type, 0), sizeof(min));
	memcpy(&max, (const u8*)field_attribute.value + ls_type_struct_field_offset(field_attribute.type, 1), sizeof(max));
	EXPECT_EQ(2, min);
	EXPECT_EQ(3, max);

	// Invalid handles and indices must be rejected without dereferencing them.
	EXPECT_EQ(0u, ls_type_attribute_count(nullptr));
	ls_attribute invalid = ls_type_attribute_value(nullptr, 0);
	EXPECT_TRUE(invalid.type == nullptr);
	EXPECT_TRUE(invalid.value == nullptr);
	invalid = ls_type_attribute_value(settings_type, 1);
	EXPECT_TRUE(invalid.type == nullptr);
	EXPECT_TRUE(invalid.value == nullptr);
	invalid = ls_type_struct_field_attribute_value(settings_type, 0, 1);
	EXPECT_TRUE(invalid.type == nullptr);
	EXPECT_TRUE(invalid.value == nullptr);
	EXPECT_EQ(0u, ls_type_struct_field_attribute_count(settings_type, 1));
	EXPECT_EQ(0u, ls_type_struct_field_attribute_count(nullptr, 0));
	invalid = ls_type_struct_field_attribute_value(nullptr, 0, 0);
	EXPECT_TRUE(invalid.type == nullptr);
	EXPECT_TRUE(invalid.value == nullptr);
	const ls_type* value_type = ls_type_struct_field_type(settings_type, 0);
	EXPECT_EQ(0u, ls_type_attribute_count(value_type));
	EXPECT_EQ(0u, ls_type_struct_field_attribute_count(value_type, 0));
	invalid = ls_type_struct_field_attribute_value(value_type, 0, 0);
	EXPECT_TRUE(invalid.type == nullptr);
	EXPECT_TRUE(invalid.value == nullptr);

	CAPI_END(module);
	return true;
}

TEST(TypeAttributeLookupReturnsTypedOptional) {
	const char* source = R"(
		struct tag { value : i32; }
		#[tag { 42 }]
		struct Settings {}

		comptime found = Settings::attribute(tag);
		fn main() : void {}
	)";
	EXPECT_COMPILE(source);
	return true;
}

TEST(TypeAttributeLookupReturnsNullWhenAbsent) {
	const char* source = R"(
		struct tag { value : i32; }
		struct Settings {}
		comptime missing = Settings::attribute(tag);
	)";
	EXPECT_COMPILE(source);
	return true;
}

TEST(TypeAttributeLookupCanSelectDifferentAttributeTypes) {
	const char* source = R"(
		struct tag { value : i32; }
		struct label { value : []const u8; }
		#[tag { 42 }, label { "settings" }]
		struct Settings {}

		comptime numeric = Settings::attribute(tag);
		comptime text = Settings::attribute(label);
	)";
	EXPECT_COMPILE(source);
	return true;
}

TEST(TypeAttributeLookupUsesTheReceiverValueType) {
	const char* source = R"(
		struct tag { value : i32; }
		#[tag { 42 }]
		struct Settings {}

		fn main() : void {
			var settings : Settings = undefined;
			comptime found = settings::attribute(tag);
		}
	)";
	EXPECT_COMPILE(source);
	return true;
}

TEST(TypeAttributeLookupCanBeUsedThroughTypeof) {
	const char* source = R"(
		struct tag { value : i32; }
		#[tag { 42 }]
		struct Settings {}

		fn main() : void {
			var settings : Settings = undefined;
			comptime found = typeof(settings)::attribute(tag);
		}
	)";
	EXPECT_COMPILE(source);
	return true;
}

TEST(TypeAttributeLookupRequiresACompileTimeTypeArgument) {
	const char* source = R"(
		struct tag { value : i32; }
		#[tag { 42 }]
		struct Settings {}

		fn main(tag : comptime type) : void {
			comptime found = Settings::attribute(tag);
		}
	)";
	EXPECT_COMPILE(source);
	return true;
}

TEST(AttributesSupportNestedStructPayloads) {
	const char* source = R"(
		struct point { x : i32; y : i32; }
		struct marker { position : point; }
		#[marker { point { 3, 4 } }]
		struct Widget {}
	)";
	EXPECT_COMPILE(source);
	return true;
}

TEST(AttributesSupportDifferentPayloadKinds) {
	const char* source = R"(
		struct flag { enabled : bool; }
		struct ratio { value : f32; }
		struct text { value : []const u8; }
		#[flag { true }, ratio { 0.5 }, text { "hello" }]
		struct Settings {}
	)";
	EXPECT_COMPILE(source);
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

TEST(AttributePayloadArityMustMatchItsStructType) {
	const char* source = R"(
		struct tag { first : i32; second : i32; }
		#[tag { 1 }]
		struct Settings {}
	)";
	EXPECT_COMPILE_FAIL(source);
	return true;
}

TEST(AttributeTypeMustBeADeclaredStruct) {
	const char* source = R"(
		enum Kind { A }
		#[Kind { 1 }]
		struct Settings {}
	)";
	EXPECT_COMPILE_FAIL(source);
	return true;
}

TEST(AttributeTypeMustExist) {
	const char* source = R"(
		#[missing { 1 }]
		struct Settings {}
	)";
	EXPECT_COMPILE_FAIL(source);
	return true;
}

TEST(AttributeListCannotBeEmpty) {
	const char* source = R"(
		#[]
		struct Settings {}
	)";
	EXPECT_COMPILE_FAIL(source);
	return true;
}

TEST(AttributeCannotBeAttachedToAFunction) {
	const char* source = R"(
		struct tag { value : i32; }
		#[tag { 1 }]
		fn main() : void {}
	)";
	EXPECT_COMPILE_FAIL(source);
	return true;
}

TEST(AttributeCannotBeAttachedToAnEnum) {
	const char* source = R"(
		struct tag { value : i32; }
		#[tag { 1 }]
		enum State { Idle }
	)";
	EXPECT_COMPILE_FAIL(source);
	return true;
}

TEST(TypeAttributeLookupArgumentMustBeAType) {
	const char* source = R"(
		struct tag { value : i32; }
		#[tag { 1 }]
		struct Settings {}
		comptime found = Settings::attribute(1);
	)";
	EXPECT_COMPILE_FAIL(source);
	return true;
}

TEST(AttributeLookupReturnsFirstMatchingAttribute) {
	const char* source = R"(
		struct tag { value : i32; }
		#[tag { 1 }, tag { 2 }]
		struct Settings {}
		comptime found = Settings::attribute(tag);
	)";
	EXPECT_COMPILE(source);
	return true;
}

TEST(AttributePayloadCanContainComptimeValues) {
	const char* source = R"(
		struct tag { value : i32; }
		comptime number = 42;
		#[tag { number }]
		struct Settings {}
	)";
	EXPECT_COMPILE(source);
	return true;
}

TEST(AttributePayloadRejectsRuntimeValues) {
	const char* source = R"(
		struct tag { value : i32; }
		var runtime : i32 = 1;
		#[tag { runtime }]
		struct Settings {}
	)";
	EXPECT_COMPILE_FAIL(source);
	return true;
}

TEST(AttributePayloadRejectsTooManyValues) {
	const char* source = R"(
		struct tag { value : i32; }
		#[tag { 1, 2 }]
		struct Settings {}
	)";
	EXPECT_COMPILE_FAIL(source);
	return true;
}

TEST(AttributePayloadRejectsMissingValues) {
	const char* source = R"(
		struct tag { value : i32; }
		#[tag {}]
		struct Settings {}
	)";
	EXPECT_COMPILE_FAIL(source);
	return true;
}

TEST(AttributeLookupRejectsMultipleArguments) {
	const char* source = R"(
		struct tag { value : i32; }
		#[tag { 1 }]
		struct Settings {}
		comptime found = Settings::attribute(tag, tag);
	)";
	EXPECT_COMPILE_FAIL(source);
	return true;
}

TEST(AttributeLookupRejectsRuntimeUse) {
	const char* source = R"(
		struct tag { value : i32; }
		#[tag { 1 }]
		struct Settings {}
		fn main() : void {
			var found = Settings::attribute(tag);
		}
	)";
	EXPECT_COMPILE_FAIL(source);
	return true;
}

TEST(AttributeDeclarationRequiresPayload) {
	const char* source = R"(
		struct tag { value : i32; }
		#[tag]
		struct Settings {}
	)";
	EXPECT_COMPILE_FAIL(source);
	return true;
}

TEST(AttributeSyntaxRequiresClosingBracket) {
	const char* source = R"(
		struct tag { value : i32; }
		#[tag { 1 }
		struct Settings {}
	)";
	EXPECT_COMPILE_FAIL(source);
	return true;
}

TEST(TypeAttributeLookupChecksTheReturnedValue) {
	const char* source = R"(
		struct tag { value : i32; }
		#[tag { 42 }]
		struct Settings {}
		comptime found = Settings::attribute(tag);

		fn read(v : ?tag) : i32 {
			if v != null { return v.value; }
			return 0;
		}
		fn main() : void {
			comptime value = read(found);
			if value != 42 { var bad : MissingType = undefined; }
		}
	)";
	EXPECT_COMPILE(source);
	return true;
}

TEST(TypeAttributeLookupRequiresNullCheckBeforePayloadAccess) {
	const char* source = R"(
		struct tag { value : i32; }
		#[tag { 42 }]
		struct Settings {}
		comptime found = Settings::attribute(tag);
		comptime value = found.value;
	)";
	EXPECT_COMPILE_FAIL(source);
	return true;
}

TEST(TypeAttributeLookupAbsentValueIsNull) {
	const char* source = R"(
		struct tag { value : i32; }
		struct Settings {}
		comptime missing = Settings::attribute(tag);

		fn main() : void {
			if missing == null { }
			else { var bad : MissingType = undefined; }
		}
	)";
	EXPECT_COMPILE(source);
	return true;
}

TEST(TypeAttributeLookupPreservesFirstMatch) {
	const char* source = R"(
		struct tag { value : i32; }
		#[tag { 1 }, tag { 2 }]
		struct Settings {}
		comptime found = Settings::attribute(tag);

		fn read(v : ?tag) : i32 {
			if v != null { return v.value; }
			return 0;
		}
		fn main() : void {
			comptime value = read(found);
			if value != 1 { var bad : MissingType = undefined; }
		}
	)";
	EXPECT_COMPILE(source);
	return true;
}

TEST(TypeAttributeLookupSelectsTheRequestedType) {
	const char* source = R"(
		struct tag { value : i32; }
		struct label { value : []const u8; }
		#[tag { 42 }, label { "settings" }]
		struct Settings {}
		comptime numeric = Settings::attribute(tag);
		comptime text = Settings::attribute(label);

		fn main() : void {
			if numeric == null { var bad : MissingType = undefined; }
			if text == null { var bad : MissingType = undefined; }
		}
	)";
	EXPECT_COMPILE(source);
	return true;
}

TEST(FieldAttributesCanBeDifferentFromTypeAttributes) {
	const char* source = R"(
		struct type_tag { value : i32; }
		struct field_tag { value : i32; }
		#[type_tag { 1 }]
		struct Settings {
			#[field_tag { 2 }]
			value : i32;
		}
	)";
	EXPECT_COMPILE(source);
	return true;
}
