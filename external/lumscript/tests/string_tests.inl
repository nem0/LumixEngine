TEST(StringConcatenationTypechecks) {
	const char* source = R"(
		fn join(a : string, b : string) : string {
			return a + " " + b;
		}

		fn main() : string {
			const hello : string = "Hello";
			const target = "Lumix";
			return join(hello, target);
		}
	)";
	EXPECT_COMPILE(source);
	return true;
}


TEST(StringConcatenationRejectsNonString) {
	const char* source = R"(
		fn main() : string {
			return "count: " + 42;
		}
	)";
	EXPECT_COMPILE_FAIL(source);

	const char* global_source = R"(
		const a = 1;
		fn main() : void {
			a = 2;
		}
	)";
	EXPECT_COMPILE_FAIL(global_source);
	return true;
}

TEST(StringIsReservedKeyword) {
	const char* source = R"(
		fn string() : void {
		}
	)";
	EXPECT_COMPILE_FAIL(source);
	return true;
}
