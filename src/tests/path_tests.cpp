#include "core/log.h"
#include "core/path.h"
#include "tests/common.h"

using namespace Lumix;

namespace {

bool testPathNormalize() {
	char output[64];
	const auto normalize = [&](StringView path) {
		Path::normalize(path, Span(output));
		return StringView(output);
	};

	ASSERT_TRUE(equalStrings(normalize("foo\\bar"), "foo/bar"));
	ASSERT_TRUE(equalStrings(normalize("./foo//bar"), "foo/bar"));
	ASSERT_TRUE(equalStrings(normalize(".\\foo\\\\bar"), "foo/bar"));
	ASSERT_TRUE(equalStrings(normalize("foo/bar/"), "foo/bar/"));
	return true;
}

bool testPathNormalizeBoundedInput() {
	char output[64];
	const char input[] = "foo\\bar";
	Path::normalize(StringView(input, sizeof(input) - 1), Span(output));
	ASSERT_TRUE(equalStrings(output, "foo/bar"));
	return true;
}

bool testPathExtension() {
	ASSERT_TRUE(equalStrings(Path::getExtension("dir/archive.tar.gz"), "gz"));
	return true;
}

bool testBoundedPathViews() {
	const char input[] = "dir/a.gz";
	const StringView path(input, sizeof(input) - 1);
	ASSERT_TRUE(equalStrings(Path::getDir(path), "dir/"));
	ASSERT_TRUE(equalStrings(Path::getBasename(path), "a"));
	ASSERT_TRUE(equalStrings(Path::getExtension(path), "gz"));
	return true;
}

bool testPathIsSame() {
	ASSERT_TRUE(Path::isSame("foo/bar", "foo/bar"));
	ASSERT_TRUE(Path::isSame("foo/bar/", "foo/bar"));
	ASSERT_TRUE(Path::isSame("foo/bar", "foo/bar/"));
	ASSERT_TRUE(Path::isSame(StringView(), "."));
	ASSERT_TRUE(Path::isSame(".", StringView()));
	ASSERT_TRUE(!Path::isSame("foo/bar", "foo/baz"));
	return true;
}

} // namespace

void runPathTests() {
	RUN_TEST(testPathNormalize);
	RUN_TEST(testPathNormalizeBoundedInput);
	RUN_TEST(testPathExtension);
	RUN_TEST(testBoundedPathViews);
	RUN_TEST(testPathIsSame);
}
