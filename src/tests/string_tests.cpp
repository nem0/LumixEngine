#include "core/log.h"
#include "core/string.h"
#include "tests/common.h"

using namespace Lumix;

namespace {

bool testEndsWith() {
	ASSERT_TRUE(endsWith("assets/material.shader", "shader"));
	ASSERT_TRUE(endsWithInsensitive("assets/MATERIAL.SHADER", "shader"));
	ASSERT_TRUE(!endsWith("assets/material.shader", "material"));
	ASSERT_TRUE(!endsWithInsensitive("assets/MATERIAL.SHADER", "material"));
	ASSERT_TRUE(!endsWithInsensitive("sh", "shader"));
	return true;
}

bool testBoundedStringView() {
	const char input[] = "material";
	const StringView view(input, sizeof(input) - 1);
	ASSERT_EQ(view.size(), 8u);
	ASSERT_TRUE(view.end() == input + sizeof(input) - 1);
	ASSERT_TRUE(endsWith(view, "rial"));
	return true;
}

bool testStringViewSlicesAndSearch() {
	const char input[] = "material";
	const StringView view(input, sizeof(input) - 1);

	StringView suffix = view;
	suffix.removePrefix(3);
	ASSERT_TRUE(equalStrings(suffix, "erial"));
	suffix.removeSuffix(2);
	ASSERT_TRUE(equalStrings(suffix, "eri"));
	ASSERT_TRUE(equalStrings(view.withoutLeft(4), "rial"));
	ASSERT_TRUE(startsWith(view, "mat"));
	ASSERT_TRUE(startsWithInsensitive(view, "MAT"));
	ASSERT_TRUE(find(view, "ter") == input + 2);
	ASSERT_TRUE(findInsensitive(view, "TER") == input + 2);
	ASSERT_TRUE(findInsensitive(view, "rial") == input + 4);
	ASSERT_TRUE(findInsensitive(view, "xyz") == nullptr);
	ASSERT_TRUE(find(view, 'e') == input + 3);
	ASSERT_TRUE(find(view, 'x') == nullptr);
	ASSERT_TRUE(reverseFind(view, 'i') == input + 5);

	char output[16];
	ASSERT_TRUE(makeLowercase(Span(output), StringView("MATERIAL")));
	ASSERT_TRUE(equalStrings(output, "material"));
	const char bounded_input[] = "UPPER";
	ASSERT_TRUE(makeLowercase(Span(output), StringView(bounded_input, sizeof(bounded_input) - 1)));
	ASSERT_TRUE(equalStrings(output, "upper"));
	char too_small[6];
	ASSERT_TRUE(!makeLowercase(Span(too_small), StringView("MATERIAL")));
	return true;
}

bool testSpanConstructionAndBoundedCopy() {
	const char input[] = "abc";
	const StringView view(Span<const u8>((const u8*)input, sizeof(input) - 1));
	ASSERT_EQ(view.size(), 3u);
	ASSERT_EQ(view.back(), 'c');

	char output[4];
	copyString(Span(output), view);
	ASSERT_TRUE(equalStrings(output, "abc"));
	return true;
}

bool testBoundedNumberParsing() {
	const char input[] = "123";
	u32 value = 0;
	const char* end = fromCString(StringView(input, sizeof(input) - 1), value);
	ASSERT_EQ(value, 123u);
	ASSERT_TRUE(end == input + sizeof(input) - 1);
	return true;
}

bool testStringConversionsAndPredicates() {
	char output[64];
	ASSERT_EQ(stringLength("abc"), 3);
	ASSERT_TRUE(toCString(true, Span(output)) != nullptr);
	ASSERT_TRUE(equalStrings(output, "true"));
	ASSERT_TRUE(toCString(i32(-12), Span(output)) != nullptr);
	ASSERT_TRUE(equalStrings(output, "-12"));
	ASSERT_TRUE(toCString(i64(-34), Span(output)) != nullptr);
	ASSERT_TRUE(equalStrings(output, "-34"));
	ASSERT_TRUE(toCString(u32(56), Span(output)) != nullptr);
	ASSERT_TRUE(equalStrings(output, "56"));
	ASSERT_TRUE(toCString(u64(78), Span(output)) != nullptr);
	ASSERT_TRUE(equalStrings(output, "78"));
	ASSERT_TRUE(toCString(1.25f, Span(output), 2) != nullptr);
	ASSERT_TRUE(equalStrings(output, "1.25"));
	ASSERT_TRUE(toCString(2.5, Span(output), 1) != nullptr);
	ASSERT_TRUE(equalStrings(output, "2.5"));

	toCStringHex(u8(0xab), Span(output));
	ASSERT_TRUE(equalStrings(StringView(output, 2), "AB"));
	ASSERT_TRUE(toCStringHex(u32(0x1234abcd), Span(output)) != nullptr);
	ASSERT_TRUE(equalStrings(output, "1234ABCD"));
	ASSERT_TRUE(toCStringHex(u64(0x1234567890abcdef), Span(output)) != nullptr);
	ASSERT_TRUE(equalStrings(output, "1234567890ABCDEF"));
	toCStringPretty(i32(-1234567), Span(output));
	ASSERT_TRUE(equalStrings(output, "-1 234 567"));
	toCStringPretty(u32(1234567), Span(output));
	ASSERT_TRUE(equalStrings(output, "1 234 567"));
	toCStringPretty(u64(1234567), Span(output));
	ASSERT_TRUE(equalStrings(output, "1 234 567"));

	bool bool_value = false;
	i32 i32_value = 0;
	i64 i64_value = 0;
	u16 u16_value = 0;
	u32 u32_value = 0;
	u64 u64_value = 0;
	float float_value = 0;
	ASSERT_TRUE(fromCString("-12", i32_value) && i32_value == -12);
	ASSERT_TRUE(fromCString("-34", i64_value) && i64_value == -34);
	ASSERT_TRUE(fromCString("12", u16_value) && u16_value == 12);
	ASSERT_TRUE(fromCString("34", u32_value) && u32_value == 34);
	ASSERT_TRUE(fromCString("56", u64_value) && u64_value == 56);
	ASSERT_TRUE(fromCString("1.25", float_value) && float_value == 1.25f);
	ASSERT_TRUE(fromCStringOctal("17", u32_value) && u32_value == 15);

	ASSERT_TRUE(isLetter('A') && isLetter('z') && !isLetter('1'));
	ASSERT_TRUE(isNumeric('7') && !isNumeric('x'));
	ASSERT_TRUE(isUpperCase('A') && !isUpperCase('a'));
	ASSERT_TRUE(isWhitespace(' ') && isWhitespace('\n') && !isWhitespace('x'));
	ASSERT_TRUE(contains("abc", 'b') && !contains("abc", 'x'));
	ASSERT_TRUE(equalStrings("abc", "abc"));
	ASSERT_TRUE(equalStrings(StringView("abc"), StringView("abc")));
	ASSERT_TRUE(equalIStrings("ABC", "abc"));
	ASSERT_TRUE(compareString("abc", "abd") < 0);
	ASSERT_TRUE(compareStringInsensitive("ABC", "abd") < 0);
	return true;
}

bool testEmptyStringViewEnd() {
	const StringView view;
	ASSERT_TRUE(view.empty());
	ASSERT_TRUE(view.end() == nullptr);
	return true;
}

} // namespace

void runStringTests() {
	RUN_TEST(testEndsWith);
	RUN_TEST(testBoundedStringView);
	RUN_TEST(testStringViewSlicesAndSearch);
	RUN_TEST(testSpanConstructionAndBoundedCopy);
	RUN_TEST(testBoundedNumberParsing);
	RUN_TEST(testStringConversionsAndPredicates);
	RUN_TEST(testEmptyStringViewEnd);
}
