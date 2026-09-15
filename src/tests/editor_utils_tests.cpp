#include "core/log.h"
#include "editor/studio_app.h"
#include "editor/text_filter.h"
#include "editor/utils.h"
#include "engine/resource.h"
#include "tests/common.h"

using namespace Lumix;

namespace {

bool testResourceLocator() {
	ResourceLocator locator("subresource:dir/file.ext");
	ASSERT_EQ(locator.subresource, "subresource");
	ASSERT_EQ(locator.dir, "dir");
	ASSERT_EQ(locator.basename, "file");
	ASSERT_EQ(locator.ext, "ext");
	ASSERT_EQ(locator.resource, "dir/file.ext");
	return true;
}

bool testResourcePathWithoutSubresource() {
	// The asset browser must not add a ':' for ordinary resource paths.
	const StringView subresource = ResourcePath::getSubresource("dir/file.ext");
	ASSERT_TRUE(!*subresource.end()); // no ':' follows an ordinary resource path
	return true;
}

bool testResourcePathGetResource() {
	ASSERT_EQ(ResourcePath::getResource("subresource:dir/file.ext"), "dir/file.ext");
	ASSERT_EQ(ResourcePath::getResource("dir/file.ext"), "dir/file.ext");

	const char bounded_path[] = "subresource:dir/file.ext";
	const StringView resource = ResourcePath::getResource(StringView(bounded_path, sizeof(bounded_path) - 1));
	ASSERT_EQ(resource, "dir/file.ext");
	ASSERT_TRUE(ResourcePath::getResource(StringView()).empty());
	return true;
}

bool testResourceLocatorWithoutSubresource() {
	ResourceLocator locator("dir/file.ext");
	ASSERT_TRUE(locator.subresource.empty());
	ASSERT_EQ(locator.dir, "dir");
	ASSERT_EQ(locator.basename, "file");
	ASSERT_EQ(locator.ext, "ext");
	ASSERT_EQ(locator.resource, "dir/file.ext");
	return true;
}

bool testBoundedResourceLocator() {
	const char path[] = "subresource:dir/file.ext";
	ResourceLocator locator(StringView(path, sizeof(path) - 1));
	ASSERT_EQ(locator.subresource, "subresource");
	ASSERT_EQ(locator.dir, "dir");
	ASSERT_EQ(locator.basename, "file");
	ASSERT_EQ(locator.ext, "ext");
	ASSERT_EQ(locator.resource, "dir/file.ext");
	return true;
}

bool testBoundedResourcePaths() {
	const char with_subresource[] = "sub:file";
	const StringView subresource = ResourcePath::getSubresource(StringView(with_subresource, sizeof(with_subresource) - 1));
	ASSERT_TRUE(equalStrings(subresource, "sub"));
	ASSERT_TRUE(subresource.end() == with_subresource + 3);

	const char without_subresource[] = "file";
	const StringView no_subresource = ResourcePath::getSubresource(StringView(without_subresource, sizeof(without_subresource) - 1));
	ASSERT_TRUE(equalStrings(no_subresource, "file"));
	ASSERT_TRUE(no_subresource.end() == without_subresource + sizeof(without_subresource) - 1);
	return true;
}

bool testEmptyResourcePath() {
	const StringView subresource = ResourcePath::getSubresource(StringView());
	ASSERT_TRUE(subresource.empty());
	ASSERT_TRUE(subresource.end() == nullptr);
	return true;
}

bool testEmptyResourceLocator() {
	const StringView empty;
	ResourceLocator locator(empty);
	ASSERT_TRUE(locator.subresource.empty());
	ASSERT_TRUE(locator.dir.empty());
	ASSERT_TRUE(locator.basename.empty());
	ASSERT_TRUE(locator.ext.empty());
	ASSERT_TRUE(locator.resource.empty());
	return true;
}

bool testTextFilterBuildAndPass() {
	TextFilter filter;
	copyString(filter.filter, "material shader");
	filter.build();

	ASSERT_EQ(filter.count, 2u);
	ASSERT_TRUE(filter.pass("wood material shader"));
	ASSERT_TRUE(!filter.pass("wood material"));
	ASSERT_TRUE(!filter.pass("wood shader"));
	ASSERT_TRUE(filter.passWithScore("wood material shader") > 0);
	ASSERT_EQ(filter.passWithScore("wood material"), 0u);
	return true;
}

bool testTextFilterNegativeTerms() {
	TextFilter filter;
	copyString(filter.filter, "material -preview");
	filter.build();

	ASSERT_EQ(filter.count, 2u);
	ASSERT_TRUE(filter.pass("material final"));
	ASSERT_TRUE(!filter.pass("material preview"));
	ASSERT_EQ(filter.passWithScore("material preview"), 0u);
	return true;
}

bool testTextFilterEmptyAndRepeatedSpaces() {
	TextFilter filter;
	copyString(filter.filter, "  material   ");
	filter.build();

	ASSERT_EQ(filter.count, 1u);
	ASSERT_TRUE(filter.pass("material"));
	ASSERT_TRUE(!filter.pass("metal"));

	filter.clear();
	ASSERT_EQ(filter.count, 0u);
	ASSERT_TRUE(filter.pass("anything"));
	ASSERT_EQ(filter.passWithScore("anything"), 1u);
	return true;
}

} // namespace

void runEditorUtilsTests() {
	RUN_TEST(testResourceLocator);
	RUN_TEST(testResourcePathWithoutSubresource);
	RUN_TEST(testResourcePathGetResource);
	RUN_TEST(testResourceLocatorWithoutSubresource);
	RUN_TEST(testBoundedResourceLocator);
	RUN_TEST(testBoundedResourcePaths);
	RUN_TEST(testEmptyResourcePath);
	RUN_TEST(testEmptyResourceLocator);
	RUN_TEST(testTextFilterBuildAndPass);
	RUN_TEST(testTextFilterNegativeTerms);
	RUN_TEST(testTextFilterEmptyAndRepeatedSpaces);
}
