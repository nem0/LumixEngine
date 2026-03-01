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

	WrappedText wrapText(FontHandle font, StringView text, float width) override {
		WrappedText result;
		size_t combined = (size_t)(uintptr)font;
		int font_size = (int)(combined & 0xFFFFFFFF);
		float char_width = font_size * 0.5f;
		float current_width = 0;
		const char* last_space = nullptr;
		const char* c = text.begin;
		while (c < text.end) {
			if (isWhitespace(*c)) {
				current_width += char_width;
				if (current_width > width) {
					if (last_space) {
						result.wrapped = StringView(text.begin, last_space);
						result.broken = WrappedText::SPACE;
					} else {
						result.wrapped = StringView(text.begin, c);
						result.broken = WrappedText::MIDWORD;
					}
					return result;
				}
				last_space = c;
				while (c < text.end && isWhitespace(*c)) ++c;
				continue;
			}
			current_width += char_width;
			if (current_width > width) {
				if (last_space) {
					result.wrapped = StringView(text.begin, last_space);
					result.broken = WrappedText::SPACE;
				} else {
					result.wrapped = StringView(text.begin, c);
					result.broken = WrappedText::MIDWORD;
				}
				return result;
			}
			++c;
		}
		result.wrapped = text;
		result.broken = WrappedText::NO;
		return result;
	}
};

struct MockDocument : ui::Document {
	MockFontManager m_font_manager;
	MockDocument() : ui::Document(&m_font_manager, getGlobalAllocator()) {}
};

extern int test_count;
extern int passed_count;

#define ASSERT_EQ(expected, actual, message) \
	if ((expected) != (actual)) { \
		logError("TEST FAILED at ", __FILE__, ":", __LINE__, ": ", message, " - Expected: ", expected, ", Actual: ", actual); \
		return false; \
	}

#define ASSERT_FLOAT_EQ(expected, actual, message) \
	{ \
		float diff = (expected) - (actual); \
		if (diff < 0) diff = -diff; \
		if (diff >= 0.01f) { \
			logError("TEST FAILED at ", __FILE__, ":", __LINE__, ": ", message, " - Expected: ", expected, ", Actual: ", actual); \
			return false; \
		} \
	}

#define ASSERT_TRUE(condition, message) \
	if (!(condition)) { \
		logError("TEST FAILED at ", __FILE__, ":", __LINE__, ": ", message); \
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
		ASSERT_TRUE(_res, "Failed to parse"); \
	} while(false)

#define ASSERT_TAG(elem, tag_enum) \
	ASSERT_EQ((int)ui::Tag::tag_enum, (int)(elem)->tag, "Expected tag " #tag_enum)

#define ASSERT_ATTRIBUTE(elem, index, attr_enum) \
	do { \
		Span<ui::Attribute> _attrs = (elem)->attributes; \
		ASSERT_TRUE(_attrs.size() > index, #elem " does not have attribute at index " #index); \
		ASSERT_EQ((int)ui::AttributeName::attr_enum, (int)_attrs[index].type, #elem "'s attribute at index " #index " should be " #attr_enum); \
	} while(false)


} // namespace Lumix
