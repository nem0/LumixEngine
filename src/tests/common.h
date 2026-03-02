#pragma once

#include "core/string.h"
#include "renderer/font.h"
#include "gui_ng/ui.h"

namespace Lumix {

struct MockFontManager : ui::IFontManager {
	bool isReady(FontHandle) override { return true; }

	ui::IFontManager::FontHandle loadFont(StringView path, int font_size) override {
		// Return a dummy handle that includes path hash and font_size for uniqueness
		size_t hash = 0;
		for (u32 i = 0; i < path.size(); ++i) {
			char c = path[i];
			hash = hash * 31 + (unsigned char)c;
		}
		size_t combined = (hash << 32) | (size_t)font_size;
		return (ui::IFontManager::FontHandle)(uintptr)combined;
	}

	Vec2 measureTextA(FontHandle font, StringView text) override {
		// Dummy measurement using font_size for height, skip \r, treat \n as space, collapse spaces and tabs
		size_t combined = (size_t)(uintptr)font;
		int font_size = (int)(combined & 0xFFFFFFFF);
		float width = 0;
		bool in_whitespace = false;
		for (const char* c = text.begin; c != text.end; ++c) {
			if (*c == '\r') continue;
			if (isWhitespace(*c)) {
				if (!in_whitespace) {
					width += font_size * 0.5f;
					in_whitespace = true;
				}
			} else {
				width += font_size * 0.5f;
				in_whitespace = false;
			}
		}
		return Vec2(width, (float)font_size); // TODO: use proper height
	}

	float getHeight(FontHandle font) override {
		size_t combined = (size_t)(uintptr)font;
		int font_size = (int)(combined & 0xFFFFFFFF);
		return (float)font_size; // Mock height same as font_size
	}

	float getAscender(FontHandle font) override {
		size_t combined = (size_t)(uintptr)font;
		int font_size = (int)(combined & 0xFFFFFFFF);
		return (float)font_size * 0.8f; // Mock ascender
	}

	SplitWord splitFirstWord(FontHandle font, StringView text) override {
		// Trim leading whitespace
		while (text.begin != text.end && isWhitespace(*text.begin)) ++text.begin;

		const char* head_start = text.begin;
		const char* head_end = head_start;
		while (head_end != text.end && !isWhitespace(*head_end)) ++head_end;

		StringView head(head_start, head_end);
		StringView tail(head_end, text.end);

		float head_width = measureTextA(font, head).x;

		SplitWord res;
		res.head = head;
		res.tail = tail;
		res.head_width = head_width;
		return res;
	}
};

struct MockDocument : ui::Document {
	MockFontManager m_font_manager;
	MockDocument() : ui::Document(&m_font_manager, getGlobalAllocator()) {}
};

extern int test_count;
extern int passed_count;

#define ASSERT_EQ(expected, actual) \
	if ((expected) != (actual)) { \
		logError("TEST FAILED at ", __FILE__, ":", __LINE__, ": Expected: ", expected, ", Actual: ", actual); \
		return false; \
	}

#define ASSERT_FLOAT_EQ(expected, actual) \
	{ \
		float diff = (expected) - (actual); \
		if (diff < 0) diff = -diff; \
		if (diff >= 0.01f) { \
			logError("TEST FAILED at ", __FILE__, ":", __LINE__, ": Expected: ", expected, ", Actual: ", actual); \
			return false; \
		} \
	}

#define ASSERT_TRUE(condition) \
	if (!(condition)) { \
		logError("TEST FAILED at ", __FILE__, ":", __LINE__, ": ", #condition); \
		return false; \
	}

#define RUN_TEST(test_func) \
	do { \
		++test_count; \
		if (test_func()) { \
			++passed_count; \
		} else { \
			logError("FAILED: ", #test_func); \
		} \
	} while(0)

#define ASSERT_PARSE(doc, s) \
	do { \
		bool _res = (doc).parse(s, "test.ui"); \
		ASSERT_TRUE(_res); \
	} while(false)

#define ASSERT_TAG(elem, tag_enum) \
	ASSERT_EQ((int)ui::Tag::tag_enum, (int)(elem)->tag)

#define ASSERT_ATTRIBUTE(elem, index, attr_enum) \
	do { \
		Span<ui::Attribute> _attrs = (elem)->attributes; \
		ASSERT_TRUE(_attrs.size() > index); \
		ASSERT_EQ((int)ui::AttributeName::attr_enum, (int)_attrs[index].type); \
	} while(false)


} // namespace Lumix
