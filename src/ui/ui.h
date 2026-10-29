#pragma once

#include "core/arena_allocator.h"
#include "core/array.h"
#include "core/color.h"
#include "core/hash_map.h"
#include "core/math.h"
#include "core/stack_array.h"
#include "core/string.h"
#include "engine/input_system.h"
#include "renderer/draw2d.h"
#include "ui_tokenizer.h"

namespace Lumix {

struct Draw2D;
struct Sprite;
struct Texture;
struct SplitWord;

namespace ui {

struct Document;
struct Element;

//@ enum full ui::Tag
enum class Tag : u8 {
	BOX,
	IMAGE,
	SPAN,
	BR,
	CANVAS,
	INVALID
};

enum class AttributeName : u8 {
	ID,
	CLASS,
	VISIBLE,
	FONT_SIZE,
	FONT,
	COLOR,
	OPACITY,
	CLIPPING,
	WIDTH,
	HEIGHT,
	TOP,
	LEFT,
	PIVOT_X,
	PIVOT_Y,
	POSITION,
	MARGIN,
	PADDING,
	BG_IMAGE,
	BG_FIT,
	BG_COLOR,
	DIRECTION,
	WRAP,
	JUSTIFY_CONTENT,
	ALIGN_ITEMS,
	TEXT,
	ALIGN,
	SRC,
	FIT,
	GROW,
	ON_CLICK,
	PLACEHOLDER,
	MARGIN_TOP,
	MARGIN_RIGHT,
	MARGIN_BOTTOM,
	MARGIN_LEFT,
	PADDING_TOP,
	PADDING_RIGHT,
	PADDING_BOTTOM,
	PADDING_LEFT,

	INVALID
};

enum class AttributeSource : u8 {
	ELEMENT,
	STYLESHEET
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
	SPACE_AROUND,
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
	RIGHT,
};

enum PositionMode : u8 {
	RELATIVE,
	ABSOLUTE
};

enum class Unit : u8 {
	PIXELS,
	PERCENT,
	EM,
	FIT_CONTENT
};

struct ParsedUnit {
	float value;
	Unit unit;
};

AttributeName parseAttributeName(StringView str);
Tag parseTag(StringView str);
const char* attributeNameToString(AttributeName name);

struct IFontManager {
	using FontHandle = void*;
	virtual ~IFontManager() {}
	virtual FontHandle loadFont(StringView path, int font_size) = 0;
	virtual void unloadFont(FontHandle font) = 0;
	virtual Vec2 measureTextA(FontHandle font, StringView text) = 0;
	virtual float getHeight(FontHandle font) = 0;
	virtual float getAscender(FontHandle font) = 0;
	virtual bool isReady(FontHandle font) = 0;
	virtual SplitWord splitFirstWord(FontHandle font, StringView text) = 0;
};

struct IImageManager {
	using ImageHandle = void*;
	virtual ~IImageManager() {}
	virtual ImageHandle loadImage(StringView path) = 0;
	virtual void unloadImage(ImageHandle image) = 0;
	virtual bool isReady(ImageHandle image) = 0;
	virtual Vec2 getIntrinsicSize(ImageHandle image) = 0;
};

struct ISpriteManager {
	using SpriteHandle = void*;
	virtual ~ISpriteManager() {}
	virtual SpriteHandle loadSprite(StringView path) = 0;
	virtual void unloadSprite(SpriteHandle sprite) = 0;
	virtual bool isReady(SpriteHandle sprite) = 0;
};

enum class InternString : u16 { INVALID = 0 };

struct InternTable {
	IAllocator& allocator;
	ArenaAllocator arena;
	Array<StringView> strings;
	HashMap<StringView, u32> map;

	InternTable(IAllocator& allocator)
		: allocator(allocator)
		, arena(16 * 1024 * 1024, allocator, "UI intern table")
		, strings(allocator)
		, map(allocator)
	{}
	~InternTable();

	InternString intern(StringView s);
	StringView resolve(InternString id) const;
};

struct Attribute {
	Attribute() : parsed_unit{0, Unit::PIXELS} {}

	AttributeName type = AttributeName::INVALID;
	AttributeSource source = AttributeSource::ELEMENT;
	union {
		StringView value;
		ParsedUnit parsed_unit;
		Direction direction;
		Align align;
		JustifyContent justify;
		AlignItems align_items;
		Color color;
		float grow;
		float opacity;
		float font_size;
		bool visible;
		bool wrap;
		bool clip;
	};
};

struct StyleRule {
	Array<InternString> classes;
	Array<Attribute> attributes;

	StyleRule(IAllocator& allocator) : classes(allocator), attributes(allocator) {}
	StyleRule(StyleRule&& other) : classes(other.classes.move()), attributes(other.attributes.move()) {}
};

struct Stylesheet {
	Array<StyleRule> m_rules;
	HashMap<InternString, Array<u32>> m_class_index;
	IAllocator& m_allocator;

	Stylesheet(IAllocator& allocator) : m_rules(allocator), m_class_index(allocator), m_allocator(allocator) {}
	Span<StyleRule> getRules() { return Span(m_rules.begin(), m_rules.size()); }
	void buildIndex();
};

struct SpanLine {
	StringView text; // substring of span's value
	Vec2 pos; // pos.y is baseline
	float width;
};

// geometry vertex of a [canvas], the layout matches core:ui/element.Vertex in EVOX
//@ struct full ui::Vertex
struct Vertex {
	float x; // element-local UI units, origin at the canvas's top-left corner
	float y;
	float u; // texture coordinates, 0..1
	float v;
	u8 r; // color, 0..255
	u8 g;
	u8 b;
	u8 a;
};

// the geometry of a [canvas], allocated by the first setGeometry so other elements don't pay for it
struct CanvasGeometry {
	CanvasGeometry(IAllocator& allocator) : vertices(allocator), indices(allocator) {}

	Array<Draw2D::Vertex> vertices;
	Array<u32> indices;
};

struct BoxSpacing {
	float top, right, bottom, left;
};

//@ object full ui::Element
struct LUMIX_UI_API Element {
	Element() = delete;
	Element(Tag t, IAllocator& allocator, Document& doc)
		: tag(t)
		, children(allocator)
		, attributes(allocator)
		, classes(allocator)
		, lines(allocator)
		, m_document(doc)
	{}
	Element(const Element&) = delete;
	Element& operator=(const Element&) = delete;
	Element(Element&& rhs);
	Element& operator=(Element&& rhs) = delete;
	~Element();
		
	Document& m_document;
	Tag tag;
	i32 parent = -1;
	u32 index = 0xffFFffFF;
	Array<u32> children;
	Array<Attribute> attributes;
	Array<InternString> classes;
	StringView id;
	
	// runtime computed data
	PositionMode position_mode = PositionMode::RELATIVE;
	float left = 0;
	float top = 0;
	Vec2 position = {0, 0};
	Vec2 size = {0, 0};
	StringView text;
	Array<SpanLine> lines;
	// [canvas] only, null until setGeometry; owned by the element
	CanvasGeometry* canvas_geometry = nullptr;
	float font_size = 0;
	Color color = Color::WHITE;
	float opacity = 1.0f;
	Color bg_color = Color::TRANSPARENT;
	BoxSpacing margins = {};
	BoxSpacing paddings = {};
	Direction direction = Direction::COLUMN;
	JustifyContent justify_content = JustifyContent::START;
	AlignItems align_items = AlignItems::START;
	Align text_align = Align::LEFT;
	float grow = 0;
	bool wrap = true;
	bool visible = true;
	bool clipping = false;
	ParsedUnit width_unit = {0, Unit::FIT_CONTENT};
	ParsedUnit height_unit = {0, Unit::FIT_CONTENT};
	ParsedUnit pivot_x_unit = {0, Unit::PIXELS};
	ParsedUnit pivot_y_unit = {0, Unit::PIXELS};
	
	//@ function
	StringView getID() { return id; }
	//@ function 
	void setVisible(bool show);
	//@ function 
	void setText(StringView value);
	//@ function
	void setWidth(StringView value);
	//@ function 
	void setBGImage(const Path& path);
	// [canvas] only, ignored with an error log on other elements. Indices must be a multiple of 3 and point into vertices; otherwise the previous geometry stays
	//@ function
	void setGeometry(Span<const Vertex> vertices, Span<const u32> indices);
	//@ function
	void clearGeometry();
	// "left"/"top" of a relative element shift it (and its content) after layout without moving its siblings, e.g. "-120" (pixels), "10%" or "2em", for animations
	//@ function
	void setLeft(StringView value);
	//@ function
	void setTop(StringView value);
	//@ function
	void setOpacity(float value);
	// as laid out, in UI units (pixels before the DPI scale), without the shift of left/top; valid after the first layout with the element visible
	//@ function
	float getCenterX() const;
	//@ function
	float getCenterY() const;
	//@ function
	float getWidth() const;
	//@ function
	float getHeight() const;

	IFontManager::FontHandle getFontHandle() const { return font_handle; }
	void setFontHandle(IFontManager::FontHandle font_handle);
	// not owned: the document's sprite cache holds the reference (Document::acquireSprite)
	ISpriteManager::SpriteHandle getSpriteHandle() const { return sprite_handle; }
	void setSpriteHandle(ISpriteManager::SpriteHandle sprite_handle) { this->sprite_handle = sprite_handle; }
	IImageManager::ImageHandle getImageHandle() const { return image_handle; }
	void setImageHandle(IImageManager::ImageHandle image_handle);

private:
	ISpriteManager::SpriteHandle sprite_handle = nullptr;
	IImageManager::ImageHandle image_handle = nullptr;
	IFontManager::FontHandle font_handle = nullptr;
};
//@ end

//@ enum full ui::EventType
enum class EventType {
	MOUSE_DOWN,
	MOUSE_UP,
	MOUSE_MOVE,
	MOUSE_WHEEL,
	KEY_DOWN,
	KEY_UP,
	TEXT_INPUT,
	ACTION,
	MOUSE_ENTER,
	MOUSE_LEAVE,
	INVALID
};

//@ struct full ui::Event
struct Event {
	static constexpr u32 INVALID_ELEMENT_INDEX = 0xFFFF'FFFF;

	EventType type = EventType::INVALID;
	Vec2 position;
	u32 element_index = INVALID_ELEMENT_INDEX;
	StringView action;
	i32 key_code = 0;
	u32 text_utf8 = 0;
	float wheel_y = 0;
};

// where each element was parsed from, filled by Document::parse when a SourceMap is passed
struct SourceMap {
	struct Range {
		u32 begin; // byte offset into the parsed content
		u32 end;
	};
	// indexed like Document::m_elements, elements without a range have {0, 0}
	Array<Range> elements;

	SourceMap(IAllocator& allocator) : elements(allocator) {}

	void clear() { elements.clear(); }
	void set(u32 element_idx, u32 begin, u32 end) {
		if (element_idx >= (u32)elements.size()) elements.resize(element_idx + 1);
		elements[element_idx] = {begin, end};
	}
};

//@ object full ui::Document
struct LUMIX_UI_API Document {
	using Token = UITokenizer::Token;

	Element m_root;
	Array<Element> m_elements;
	Stylesheet m_stylesheet;
	InternTable m_intern_table;
	bool m_suppress_logging = false;
	UITokenizer m_tokenizer;
	IFontManager* m_font_manager;
	IImageManager* m_image_manager;
	ISpriteManager* m_sprite_manager;
	IAllocator& m_allocator;
	Vec2 m_canvas_size;
	String m_content;
	// scale applied to pixel and font-size values (em follows the font size): m_dpi_scale, or, with a [doc] tag, derived from the canvas size
	float m_scale = 1.0f;
	float m_dpi_scale = 1.0f; // OS DPI scale, set by setDPIScale()
	// set by the [doc] tag, 0 = not set
	float m_reference_width = 0;
	float m_reference_height = 0;
	float m_min_scale = 0;
	float m_max_scale = 0;
	enum class ScaleMatch : u8 { HEIGHT, WIDTH, MIN, MAX };
	ScaleMatch m_scale_match = ScaleMatch::HEIGHT;
	bool m_layout_dirty = false;
	bool m_styles_dirty = false;
	i32 m_first_free_element = -1; // head of the free list of destroyed elements, next link is stored in Element::parent
	float m_layout_duration = 0;
	float m_parse_duration = 0;
	float m_render_duration = 0;
	StackArray<u32, 16> m_hovered_elements;
	// every sprite the document uses, loaded once per path: named by an element, set at runtime, or by any stylesheet rule (hover and other state rules
	// included, so a state change never waits for a load). Elements only point into it; the document owns the references.
	HashMap<InternString, ISpriteManager::SpriteHandle> m_sprites;
	// element id -> index in m_elements, built at the end of a parse, which fails on duplicate ids
	HashMap<StringView, u32> m_id_index;

	Document(IFontManager* font_manager, IAllocator& allocator, IImageManager* image_manager = nullptr, ISpriteManager* sprite_manager = nullptr);
	~Document();

	// source_map, if not null, receives the source range of every element
	bool parse(StringView content, const char* filename, SourceMap* source_map = nullptr);
	// null for an out of range index, e.g. Event::INVALID_ELEMENT_INDEX of an event with no element under the cursor
	//@ function
	Element* getElement(u32 index) { return index < (u32)m_elements.size() ? &m_elements[index] : nullptr; }
	const Element* getElement(u32 index) const { return index < (u32)m_elements.size() ? &m_elements[index] : nullptr; }
	void computeLayout(Vec2 canvas_size);
	// mutators only mark the layout dirty, it's recomputed at most once, on flushLayout() (called before render and hit testing)
	void invalidateLayout() { m_layout_dirty = true; }
	void flushLayout() { if (m_layout_dirty) computeLayout(m_canvas_size); }
	void render(Draw2D& draw);
	Element* getElementAt(Vec2 pos);
	// the OS DPI scale, ignored while the document has a [doc] reference size
	void setDPIScale(float scale);
	// scale actually applied
	float getScale() const { return m_scale; }
	// scale for a canvas size (in pixels): m_dpi_scale, or canvas / reference size when a [doc] tag sets one
	float computeScale(Vec2 canvas_size) const;
	//@ function
	Element* getElementByID(const char* id);
	//@ function
	Element& createElement(Tag tag, i32 parent);
	//@ function
	void destroyElement(Element& el);

	void addClass(u32 element_index, StringView classname);
	void removeClass(u32 element_index, StringView classname);
	void recomputeStyles();

	//@ function
	Span<const Event> getEvents();
	void injectEvent(const InputSystem::Event& event);
	void clearEvents();
	bool areDependenciesReady() const;
	ISpriteManager::SpriteHandle acquireSprite(StringView path);

private:
	// drops every cached sprite reference, the sprite manager must be alive: a document has to be destroyed before the managers it was created with
	void unloadSprites();

	template <typename... Args>
	void error(StringView location, UITokenizer& tokenizer, Args&&... args) {
		if (!m_suppress_logging) {
			logError(tokenizer.m_filename, "(", tokenizer.getLine(location), "): ", args...);
		}
	}

	// doc_body: parsing the body of [doc], the root level that ends with a }
	bool parseElements(u32 parent_index, bool doc_body = false);
	bool parseStyleBlock();
	bool parseDocTag();
	// records the source range [begin, end) of an element, no-op without a source map
	void setSourceRange(u32 element_idx, const char* begin, const char* end);
	SourceMap* m_source_map = nullptr; // only set during parse
	// [path] inside the [canvas] at parent_index, appends its stroke to the canvas geometry
	bool parsePath(u32 parent_index, StringView location);
	bool consume(Token::Type type, Token* out_token = nullptr);
	bool tryConsume(Token::Type type, Token* out_token = nullptr);
	void addClassRaw(u32 element_index, StringView classname);
	void removeClassRaw(u32 element_index, StringView classname);
	bool parseAttributeValue(Element& elem, AttributeName name, StringView value);

	bool m_has_doc_tag = false;
	Array<Event> m_events;
};

} // namespace ui

template <>
struct HashFunc<ui::InternString> {
	static u32 get(const ui::InternString& key) {
		return HashFunc<u32>::get(static_cast<u32>(key));
	}
};

} // namespace Lumix
