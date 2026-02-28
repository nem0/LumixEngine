#pragma once

#include "core/array.h"
#include "core/color.h"
#include "core/math.h"
#include "core/string.h"
#include "engine/input_system.h"
#include "ui_tokenizer.h"

namespace Lumix {

struct Draw2D;
struct Sprite;

namespace ui {

enum class Tag : u8 {
	PANEL,
	IMAGE,
	SPAN,
	INVALID
};

enum class AttributeName : u8 {
	ID,
	CLASS,
	VISIBLE,
	FONT_SIZE,
	FONT,
	COLOR,
	WIDTH,
	HEIGHT,
	MARGIN,
	PADDING,
	BACKGROUND_IMAGE,
	BACKGROUND_FIT,
	BG_COLOR,
	DIRECTION,
	WRAP,
	JUSTIFY_CONTENT,
	ALIGN_ITEMS,
	VALUE,
	ALIGN,
	SRC,
	FIT,
	GROW,
	PLACEHOLDER,

	INVALID
};

enum class Direction : u8 {
	ROW,
	COLUMN
};

enum class JustifyContent : u8 {
	START,
	CENTER,
	END,
	SPACE_BETWEEN,
	SPACE_AROUND
};

enum class AlignItems : u8 {
	START,
	CENTER,
	END,
	STRETCH
};

enum class Align : u8 {
	LEFT,
	CENTER,
	RIGHT
};

enum class Unit : u8 {
	PIXELS,
	PERCENT,
	EM,
	FIT_CONTENT
};

struct IFontManager {
	using FontHandle = void*;
	virtual FontHandle loadFont(StringView path, int font_size) = 0;
	virtual Vec2 measureTextA(FontHandle font, StringView text) = 0;
	virtual float getHeight(FontHandle font) = 0;
	virtual float getAscender(FontHandle font) = 0;
};

struct Attribute {
	AttributeName type;
	StringView value;
};

struct StyleRule {
	StringView selector;
	Array<Attribute> attributes;

	StyleRule(StringView sel, Array<Attribute>&& attrs) : selector(sel), attributes(attrs.move()) {}
};

struct Stylesheet {
	Array<StyleRule> m_rules;

	Stylesheet(IAllocator& allocator) : m_rules(allocator) {}
	Span<StyleRule> getRules() { return Span(m_rules.begin(), m_rules.size()); }
};

struct SpanLine {
	StringView text; // substring of span's value
	Vec2 pos; // pos.y is baseline
};

struct Element {
	Element() = default;
	Element(Tag t, IAllocator& allocator) : tag(t), children(allocator), attributes(allocator), lines(allocator) {}

	Tag tag;
	Array<u32> children;
	Array<Attribute> attributes;
	Array<SpanLine> lines;

	// runtime computed data
	StringView value;
	StringView style_class;
	StringView bg_image;
	Sprite* bg_sprite = nullptr;
	IFontManager::FontHandle font_handle = nullptr;
	Vec2 position;
	Vec2 size;
	float font_size = 0;
	Color color = Color::WHITE;
	Color bg_color = Color::BLACK;
	float margins[4]; // top, right, bottom, left
	float paddings[4]; // top, right, bottom, left
	Direction direction = Direction::COLUMN;
	JustifyContent justify_content = JustifyContent::START;
	AlignItems align_items = AlignItems::STRETCH;
	Align text_align = Align::LEFT;
	float grow = 0;
	bool wrap = false;
};

//@ enum full ui::EventType
enum class EventType {
	MOUSE_DOWN,
	MOUSE_UP,
	MOUSE_MOVE,
	MOUSE_WHEEL,
	KEY_DOWN,
	KEY_UP,
	TEXT_INPUT,
	CLICK,
	INVALID
};

//@ struct full ui::Event
struct Event {
	EventType type = EventType::INVALID;
	Vec2 position;
	u32 element_index = 0;
	i32 key_code = 0;
	u32 text_utf8 = 0;
	float wheel_y = 0;
};

//@ object full ui::Document
struct Document {
	using Token = UITokenizer::Token;

	Array<Element> m_elements;
	Array<u32> m_roots;
	Stylesheet m_stylesheet;
	bool m_suppress_logging = false;
	UITokenizer m_tokenizer;
	IFontManager* m_font_manager;
	struct ResourceManagerHub* m_resource_manager;
	IAllocator& m_allocator;
	Vec2 m_canvas_size;
	String m_content;

	Document(IFontManager* font_manager, IAllocator& allocator)
		: m_elements(allocator)
		, m_roots(allocator)
		, m_stylesheet(allocator)
		, m_suppress_logging(false)
		, m_tokenizer()
		, m_font_manager(font_manager)
		, m_resource_manager(nullptr)
		, m_allocator(allocator)
		, m_canvas_size(0, 0)
		, m_content(allocator)
		, m_events(allocator)
	{}

	bool parse(StringView content, const char* filename);
	Element* getElement(u32 index) { return &m_elements[index]; }
	const Element* getElement(u32 index) const { return &m_elements[index]; }
	void computeLayout(Vec2 canvas_size);
	void render(Draw2D& draw) const;
	Element* getElementAt(Vec2 pos);

	//@ function
	Span<const Event> getEvents();
	void injectEvent(const InputSystem::Event& event);
	void clearEvents();

private:
	template <typename... Args>
	void error(StringView location, UITokenizer& tokenizer, Args&&... args) {
		if (!m_suppress_logging) {
			logError(tokenizer.m_filename, "(", tokenizer.getLine(location), "): ", args...);
		}
	}

	bool parseElements(u32 parent_index);
	bool parseStyleBlock();
	bool consume(Token::Type type, Token* out_token = nullptr);
	bool tryConsume(Token::Type type, Token* out_token = nullptr);

	Array<Event> m_events;
};

} // namespace ui
} // namespace Lumix
