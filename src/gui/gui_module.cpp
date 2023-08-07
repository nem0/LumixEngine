#include "engine/engine.h"
#include "engine/allocator.h"
#include "engine/associative_array.h"
#include "engine/crt.h"
#include "engine/flag_set.h"
#include "engine/input_system.h"
#include "engine/log.h"
#include "engine/os.h"
#include "engine/reflection.h"
#include "engine/resource_manager.h"
#include "engine/string.h"
#include "engine/world.h"
#include "gui_module.h"
#include "gui_system.h"
#include "renderer/draw2d.h"
#include "renderer/font.h"
#include "renderer/pipeline.h"
#include "renderer/texture.h"
#include "sprite.h"
#include "imgui/IconsFontAwesome5.h"


namespace Lumix
{


static const ComponentType GUI_CANVAS_TYPE = reflection::getComponentType("gui_canvas");
static const ComponentType GUI_BUTTON_TYPE = reflection::getComponentType("gui_button");
static const ComponentType GUI_RECT_TYPE = reflection::getComponentType("gui_rect");
static const ComponentType GUI_RENDER_TARGET_TYPE = reflection::getComponentType("gui_render_target");
static const ComponentType GUI_IMAGE_TYPE = reflection::getComponentType("gui_image");
static const ComponentType GUI_TEXT_TYPE = reflection::getComponentType("gui_text");
static const ComponentType GUI_INPUT_FIELD_TYPE = reflection::getComponentType("gui_input_field");
static const float CURSOR_BLINK_PERIOD = 1.0f;
static gpu::TextureHandle EMPTY_RENDER_TARGET = gpu::INVALID_TEXTURE;

struct GUIText
{
	GUIText(IAllocator& allocator) : text("", allocator) {}
	~GUIText() { setFontResource(nullptr); }


	void setFontResource(FontResource* res)
	{
		if (m_font_resource)
		{
			if (m_font)
			{
				m_font_resource->removeRef(*m_font);
				m_font = nullptr;
			}
			m_font_resource->getObserverCb().unbind<&GUIText::onFontLoaded>(this);
			m_font_resource->decRefCount();
		}
		m_font_resource = res;
		if (res) res->onLoaded<&GUIText::onFontLoaded>(this);
	}


	void onFontLoaded(Resource::State old_state, Resource::State new_state, Resource&)
	{
		if (m_font && new_state != Resource::State::READY)
		{
			m_font_resource->removeRef(*m_font);
			m_font = nullptr;
		}
		if (new_state == Resource::State::READY) m_font = m_font_resource->addRef(m_font_size);
	}

	void setFontSize(int value)
	{
		m_font_size = value;
		if (m_font_resource && m_font_resource->isReady())
		{
			if(m_font) m_font_resource->removeRef(*m_font);
			m_font = m_font_resource->addRef(m_font_size);
		}
	}


	FontResource* getFontResource() const { return m_font_resource; }
	int getFontSize() const { return m_font_size; }
	Font* getFont() const { return m_font; }


	String text;
	GUIModule::TextHAlign horizontal_align = GUIModule::TextHAlign::LEFT;
	GUIModule::TextVAlign vertical_align = GUIModule::TextVAlign::TOP;
	u32 color = 0xff000000;

private:
	int m_font_size = 13;
	Font* m_font = nullptr;
	FontResource* m_font_resource = nullptr;
};

 
struct GUIButton {
	u32 hovered_color = 0xffFFffFF;
	os::CursorType hovered_cursor = os::CursorType::UNDEFINED;
};


struct GUIInputField {
	int cursor = 0;
	float anim = 0;
};


struct GUIImage {
	~GUIImage() {
		if (sprite) sprite->decRefCount();
	}

	enum Flags : u32
	{
		IS_ENABLED = 1 << 1
	};
	Sprite* sprite = nullptr;
	u32 color = 0xffffFFFF;
	FlagSet<Flags, u32> flags;
};


struct GUIRect {
	enum Flags : u32 {
		IS_VALID = 1 << 0,
		IS_ENABLED = 1 << 1,
		IS_CLIP = 1 << 2
	};

	struct Anchor {
		float points = 0;
		float relative = 0;
	};

	EntityRef entity;
	FlagSet<Flags, u32> flags;
	Anchor top;
	Anchor right = { 0, 1 };
	Anchor bottom = { 0, 1 };
	Anchor left;

	GUIImage* image = nullptr;
	GUIText* text = nullptr;
	GUIInputField* input_field = nullptr;
	gpu::TextureHandle* render_target = nullptr;
};


struct GUIModuleImpl final : GUIModule {
	enum class Version : i32 {
		CANVAS_3D,
		LATEST
	};

	GUIModuleImpl(GUISystem& system, World& world, IAllocator& allocator)
		: m_allocator(allocator)
		, m_world(world)
		, m_system(system)
		, m_rects(allocator)
		, m_buttons(allocator)
		, m_canvas(allocator)
		, m_rect_hovered(allocator)
		, m_draw_2d(allocator)
		, m_rect_hovered_out(allocator)
		, m_rect_mouse_down(allocator)
		, m_unhandled_mouse_button(allocator)
		, m_button_clicked(allocator)
		, m_buttons_down_count(0)
		, m_canvas_size(800, 600)
	{
		m_font_manager = (FontManager*)system.getEngine().getResourceManager().get(FontResource::TYPE);
	}
	
	i32 getVersion() const override { return (i32)Version::LATEST; }
	
	const char* getName() const override { return "gui"; }

	void renderTextCursor(GUIRect& rect, Draw2D& draw, const Vec2& pos)
	{
		if (!rect.input_field) return;
		if (m_focused_entity != rect.entity) return;
		if (rect.input_field->anim > CURSOR_BLINK_PERIOD * 0.5f) return;

		const char* text = rect.text->text.c_str();
		const char* text_end = text + rect.input_field->cursor;
		Font* font = rect.text->getFont();
		Vec2 text_size = measureTextA(*font, text, text_end);
		draw.addLine({ pos.x + text_size.x, pos.y }
			, { pos.x + text_size.x, pos.y + text_size.y }
			, *(Color*)&rect.text->color
			, 1);
	}

	void renderRect(GUIRect& rect, Draw2D& draw, const Rect& parent_rect, bool is_main)
	{
		if (!rect.flags.isSet(GUIRect::IS_VALID)) return;
		if (!rect.flags.isSet(GUIRect::IS_ENABLED)) return;

		float l = parent_rect.x + rect.left.points + parent_rect.w * rect.left.relative;
		float r = parent_rect.x + rect.right.points + parent_rect.w * rect.right.relative;
		float t = parent_rect.y + rect.top.points + parent_rect.h * rect.top.relative;
		float b = parent_rect.y + rect.bottom.points + parent_rect.h * rect.bottom.relative;
			 
		if (rect.flags.isSet(GUIRect::IS_CLIP)) draw.pushClipRect({ l, t }, { r, b });

		auto button_iter = m_buttons.find(rect.entity);
		const Color* img_color = rect.image ? (Color*)&rect.image->color : nullptr;
		const Color* txt_color = rect.text ? (Color*)&rect.text->color : nullptr;
		if (is_main && button_iter.isValid()) {
			GUIButton& button = button_iter.value();
			if (m_cursor_pos.x >= l && m_cursor_pos.x <= r && m_cursor_pos.y >= t && m_cursor_pos.y <= b) {
				if (button.hovered_cursor != os::CursorType::UNDEFINED && !m_cursor_set) {
					m_cursor_type = button_iter.value().hovered_cursor;
					m_cursor_set = true;
				}
				img_color = (Color*)&button.hovered_color;
				txt_color = (Color*)&button.hovered_color;
			}
		}

		if (rect.image && rect.image->flags.isSet(GUIImage::IS_ENABLED))
		{
			const Color color = *img_color;
			if (rect.image->sprite && rect.image->sprite->getTexture())
			{
				Sprite* sprite = rect.image->sprite;
				Texture* tex = sprite->getTexture();
				if (sprite->type == Sprite::PATCH9)
				{
					struct Quad {
						float l, t, r, b;
					} pos = {
						l + sprite->left,
						t + sprite->top,
						r - tex->width + sprite->right,
						b - tex->height + sprite->bottom
					};
					if (pos.l > pos.r) {
						pos.l = pos.r = (pos.l + pos.r) * 0.5f;
					}
					if (pos.t > pos.b) {
						pos.t = pos.b = (pos.t + pos.b) * 0.5f;
					}
					Quad uvs = {
						sprite->left / (float)tex->width,
						sprite->top / (float)tex->height,
						sprite->right / (float)tex->width,
						sprite->bottom / (float)tex->height
					};

					draw.addImage(&tex->handle, { l, t }, { pos.l, pos.t }, { 0, 0 }, { uvs.l, uvs.t }, color);
					draw.addImage(&tex->handle, { pos.l, t }, { pos.r, pos.t }, { uvs.l, 0 }, { uvs.r, uvs.t }, color);
					draw.addImage(&tex->handle, { pos.r, t }, { r, pos.t }, { uvs.r, 0 }, { 1, uvs.t }, color);

					draw.addImage(&tex->handle, { l, pos.t }, { pos.l, pos.b }, { 0, uvs.t }, { uvs.l, uvs.b }, color);
					draw.addImage(&tex->handle, { pos.l, pos.t }, { pos.r, pos.b }, { uvs.l, uvs.t }, { uvs.r, uvs.b }, color);
					draw.addImage(&tex->handle, { pos.r, pos.t }, { r, pos.b }, { uvs.r, uvs.t }, { 1, uvs.b }, color);

					draw.addImage(&tex->handle, { l, pos.b }, { pos.l, b }, { 0, uvs.b }, { uvs.l, 1 }, color);
					draw.addImage(&tex->handle, { pos.l, pos.b }, { pos.r, b }, { uvs.l, uvs.b }, { uvs.r, 1 }, color);
					draw.addImage(&tex->handle, { pos.r, pos.b }, { r, b }, { uvs.r, uvs.b }, { 1, 1 }, color);
				}
				else
				{
					draw.addImage(&tex->handle, { l, t }, { r, b }, {0, 0}, {1, 1}, color);
				}
			}
			else
			{
				draw.addRectFilled({ l, t }, { r, b }, color);
			}
		}

		if (rect.render_target && *rect.render_target)
		{
			draw.addImage(rect.render_target, { l, t }, { r, b }, {0, 0}, {1, 1}, Color::WHITE);
		}

		if (rect.text) {
			Font* font = rect.text->getFont();
			if (font) {
				const char* text_cstr = rect.text->text.c_str();
				float ascender = getAscender(*font);
				Vec2 text_size = measureTextA(*font, text_cstr, nullptr);
				Vec2 text_pos(l, t + ascender);

				switch (rect.text->vertical_align) {
					case TextVAlign::TOP: break;
					case TextVAlign::MIDDLE: text_pos.y = (t + b + ascender + getDescender(*font)) * 0.5f; break;
					case TextVAlign::BOTTOM: text_pos.y = b + getDescender(*font); break;
				}

				switch (rect.text->horizontal_align) {
					case TextHAlign::LEFT: break;
					case TextHAlign::RIGHT: text_pos.x = r - text_size.x; break;
					case TextHAlign::CENTER: text_pos.x = (r + l - text_size.x) * 0.5f; break;
				}

				draw.addText(*font, text_pos, *txt_color, text_cstr);
				renderTextCursor(rect, draw, text_pos);
			}
		}

		for (EntityRef child : m_world.childrenOf(rect.entity)) {
			auto iter = m_rects.find(child);
			if (iter.isValid()) {
				renderRect(*iter.value(), draw, { l, t, r - l, b - t }, is_main);
			}
		}
		if (rect.flags.isSet(GUIRect::IS_CLIP)) draw.popClipRect();
	}

	IVec2 getCursorPosition() override { return m_cursor_pos; }

	void draw3D(GUICanvas& canvas, Pipeline& pipeline) {
		m_draw_2d.clear({2, 2});

		for (EntityRef child : m_world.childrenOf(canvas.entity)) {
			auto iter = m_rects.find(child);
			if (iter.isValid()) {
				renderRect(*iter.value(), m_draw_2d, { 0, 0, canvas.virtual_size.x, canvas.virtual_size.y }, false);
			}
		}

		pipeline.render3DUI(canvas.entity, m_draw_2d, canvas.virtual_size, canvas.orient_to_camera);
	}

	void render(Pipeline& pipeline, const Vec2& canvas_size, bool is_main) override {
		m_canvas_size = canvas_size;
		if (is_main) {
			m_cursor_type = os::CursorType::DEFAULT;
			m_cursor_set = false;
		}
		for (GUICanvas& canvas : m_canvas) {
			if (canvas.is_3d) {
				draw3D(canvas, pipeline);
			}
			else {
				auto iter = m_rects.find(canvas.entity);
				if (iter.isValid()) {
					GUIRect* r = iter.value();
					renderRect(*r, pipeline.getDraw2D(), {0, 0, canvas_size.x, canvas_size.y}, is_main);
				}
			}
		}
	}

	Vec4 getButtonHoveredColorRGBA(EntityRef entity) override
	{
		return ABGRu32ToRGBAVec4(m_buttons[entity].hovered_color);
	}


	void setButtonHoveredColorRGBA(EntityRef entity, const Vec4& color) override
	{
		m_buttons[entity].hovered_color = RGBAVec4ToABGRu32(color);
	}

	os::CursorType getButtonHoveredCursor(EntityRef entity) override {
		return m_buttons[entity].hovered_cursor;
	}

	void setButtonHoveredCursor(EntityRef entity, os::CursorType cursor) override {
		m_buttons[entity].hovered_cursor = cursor;
	}

	void enableImage(EntityRef entity, bool enable) override { m_rects[entity]->image->flags.set(GUIImage::IS_ENABLED, enable); }
	bool isImageEnabled(EntityRef entity) override { return m_rects[entity]->image->flags.isSet(GUIImage::IS_ENABLED); }


	Vec4 getImageColorRGBA(EntityRef entity) override
	{
		GUIImage* image = m_rects[entity]->image;
		return ABGRu32ToRGBAVec4(image->color);
	}


	static Vec4 ABGRu32ToRGBAVec4(u32 value)
	{
		float inv = 1 / 255.0f;
		return {
			((value >> 0) & 0xFF) * inv,
			((value >> 8) & 0xFF) * inv,
			((value >> 16) & 0xFF) * inv,
			((value >> 24) & 0xFF) * inv,
		};
	}


	static u32 RGBAVec4ToABGRu32(const Vec4& value)
	{
		u8 r = u8(value.x * 255 + 0.5f);
		u8 g = u8(value.y * 255 + 0.5f);
		u8 b = u8(value.z * 255 + 0.5f);
		u8 a = u8(value.w * 255 + 0.5f);
		return (a << 24) + (b << 16) + (g << 8) + r;
	}


	Path getImageSprite(EntityRef entity) override
	{
		GUIImage* image = m_rects[entity]->image;
		return image->sprite ? image->sprite->getPath() : Path();
	}

	GUICanvas& getCanvas(EntityRef entity) override {
		return m_canvas[entity];
	}

	void setImageSprite(EntityRef entity, const Path& path) override
	{
		GUIImage* image = m_rects[entity]->image;
		if (image->sprite) {
			image->sprite->decRefCount();
		}

		ResourceManagerHub& manager = m_system.getEngine().getResourceManager();
		if (path.isEmpty()) {
			image->sprite = nullptr;
		} else {
			image->sprite = manager.load<Sprite>(path);
		}
	}


	void setImageColorRGBA(EntityRef entity, const Vec4& color) override
	{
		GUIImage* image = m_rects[entity]->image;
		image->color = RGBAVec4ToABGRu32(color);
	}


	bool hasGUI(EntityRef entity) const override
	{
		auto iter = m_rects.find(entity);
		if (!iter.isValid()) return false;
		return iter.value()->flags.isSet(GUIRect::IS_VALID);
	}


	EntityPtr getRectAt(const GUIRect& rect, const Vec2& pos, const Rect& parent_rect, EntityPtr limit) const
	{
		if (!rect.flags.isSet(GUIRect::IS_VALID)) return INVALID_ENTITY;
		if (!rect.flags.isSet(GUIRect::IS_ENABLED)) return INVALID_ENTITY;
		if (rect.entity.index == limit.index) return INVALID_ENTITY;

		Rect r;
		r.x = parent_rect.x + rect.left.points + parent_rect.w * rect.left.relative;
		r.y = parent_rect.y + rect.top.points + parent_rect.h * rect.top.relative;
		float right = parent_rect.x + rect.right.points + parent_rect.w * rect.right.relative;
		float bottom = parent_rect.y + rect.bottom.points + parent_rect.h * rect.bottom.relative;

		r.w = right - r.x;
		r.h = bottom - r.y;

		bool intersect = pos.x >= r.x && pos.y >= r.y && pos.x <= r.x + r.w && pos.y <= r.y + r.h;

		for (EntityRef child : m_world.childrenOf(rect.entity))
		{
			auto iter = m_rects.find(child);
			if (!iter.isValid()) continue;

			GUIRect* child_rect = iter.value();
			EntityPtr entity = getRectAt(*child_rect, pos, r, limit);
			if (entity.isValid()) return entity;
		}

		return intersect ? rect.entity : INVALID_ENTITY;
	}
	
	EntityPtr getRectAt(const Vec2& pos) const override { return getRectAtEx(pos, m_canvas_size, INVALID_ENTITY); }

	bool isOver(const Vec2& pos, EntityRef e) override {
		const Rect r =  getRect(e);
		return pos.x >= r.x && pos.y >= r.y && pos.x <= r.x + r.w && pos.y <= r.y + r.h;
	}

	EntityPtr getRectAtEx(const Vec2& pos, const Vec2& canvas_size, EntityPtr limit) const override
	{
		for (const GUICanvas& canvas : m_canvas) {
			auto iter = m_rects.find(canvas.entity);
			if (iter.isValid()) {
				const GUIRect* r = iter.value();
				const EntityPtr e = getRectAt(*r, pos, { 0, 0, canvas_size.x, canvas_size.y }, limit);
				if (e.isValid()) return e;
			}
		}
		return INVALID_ENTITY;
	}


	static Rect getRectOnCanvas(const Rect& parent_rect, const GUIRect& rect)
	{
		float l = parent_rect.x + parent_rect.w * rect.left.relative + rect.left.points;
		float r = parent_rect.x + parent_rect.w * rect.right.relative + rect.right.points;
		float t = parent_rect.y + parent_rect.h * rect.top.relative + rect.top.points;
		float b = parent_rect.y + parent_rect.h * rect.bottom.relative + rect.bottom.points;

		return { l, t, r - l, b - t };
	}


	Rect getRect(EntityRef entity) const override
	{
		return getRectEx(entity, m_canvas_size);
	}


	Rect getRectEx(EntityPtr entity, const Vec2& canvas_size) const override
	{
		if (!entity.isValid()) return { 0, 0, canvas_size.x, canvas_size.y };
		auto iter = m_rects.find((EntityRef)entity);
		if (!iter.isValid()) return { 0, 0, canvas_size.x, canvas_size.y };

		EntityPtr parent = m_world.getParent((EntityRef)entity);
		Rect parent_rect = getRectEx(parent, canvas_size);
		GUIRect* gui = m_rects[(EntityRef)entity];
		float l = parent_rect.x + parent_rect.w * gui->left.relative + gui->left.points;
		float r = parent_rect.x + parent_rect.w * gui->right.relative + gui->right.points;
		float t = parent_rect.y + parent_rect.h * gui->top.relative + gui->top.points;
		float b = parent_rect.y + parent_rect.h * gui->bottom.relative + gui->bottom.points;

		return { l, t, r - l, b - t };
	}

	void setRectClip(EntityRef entity, bool enable) override { m_rects[entity]->flags.set(GUIRect::IS_CLIP, enable); }
	bool getRectClip(EntityRef entity) override { return m_rects[entity]->flags.isSet(GUIRect::IS_CLIP); }
	void enableRect(EntityRef entity, bool enable) override { return m_rects[entity]->flags.set(GUIRect::IS_ENABLED, enable); }
	bool isRectEnabled(EntityRef entity) override { return m_rects[entity]->flags.isSet(GUIRect::IS_ENABLED); }
	float getRectLeftPoints(EntityRef entity) override { return m_rects[entity]->left.points; }
	void setRectLeftPoints(EntityRef entity, float value) override { m_rects[entity]->left.points = value; }
	float getRectLeftRelative(EntityRef entity) override { return m_rects[entity]->left.relative; }
	void setRectLeftRelative(EntityRef entity, float value) override { m_rects[entity]->left.relative = value; }

	float getRectRightPoints(EntityRef entity) override { return m_rects[entity]->right.points; }
	void setRectRightPoints(EntityRef entity, float value) override { m_rects[entity]->right.points = value; }
	float getRectRightRelative(EntityRef entity) override { return m_rects[entity]->right.relative; }
	void setRectRightRelative(EntityRef entity, float value) override { m_rects[entity]->right.relative = value; }

	float getRectTopPoints(EntityRef entity) override { return m_rects[entity]->top.points; }
	void setRectTopPoints(EntityRef entity, float value) override { m_rects[entity]->top.points = value; }
	float getRectTopRelative(EntityRef entity) override { return m_rects[entity]->top.relative; }
	void setRectTopRelative(EntityRef entity, float value) override { m_rects[entity]->top.relative = value; }

	float getRectBottomPoints(EntityRef entity) override { return m_rects[entity]->bottom.points; }
	void setRectBottomPoints(EntityRef entity, float value) override { m_rects[entity]->bottom.points = value; }
	float getRectBottomRelative(EntityRef entity) override { return m_rects[entity]->bottom.relative; }
	void setRectBottomRelative(EntityRef entity, float value) override { m_rects[entity]->bottom.relative = value; }

	void setTextFontSize(EntityRef entity, int value) override
	{
		GUIText* gui_text = m_rects[entity]->text;
		gui_text->setFontSize(value);
	}
	
	
	int getTextFontSize(EntityRef entity) override
	{
		GUIText* gui_text = m_rects[entity]->text;
		return gui_text->getFontSize();
	}
	
	
	Vec4 getTextColorRGBA(EntityRef entity) override
	{
		GUIText* gui_text = m_rects[entity]->text;
		return ABGRu32ToRGBAVec4(gui_text->color);
	}


	void setTextColorRGBA(EntityRef entity, const Vec4& color) override
	{
		GUIText* gui_text = m_rects[entity]->text;
		gui_text->color = RGBAVec4ToABGRu32(color);
	}


	Path getTextFontPath(EntityRef entity) override
	{
		GUIText* gui_text = m_rects[entity]->text;
		return gui_text->getFontResource() == nullptr ? Path() : gui_text->getFontResource()->getPath();
	}


	void setTextFontPath(EntityRef entity, const Path& path) override
	{
		GUIText* gui_text = m_rects[entity]->text;
		FontResource* res = path.isEmpty() ? nullptr : m_font_manager->getOwner().load<FontResource>(path);
		gui_text->setFontResource(res);
	}


	TextHAlign getTextHAlign(EntityRef entity) override
	{
		GUIText* gui_text = m_rects[entity]->text;
		return gui_text->horizontal_align;
	}

	TextVAlign getTextVAlign(EntityRef entity) override {
		GUIText* gui_text = m_rects[entity]->text;
		return gui_text->vertical_align;
	}

	void setTextVAlign(EntityRef entity, TextVAlign align) override {
		GUIText* gui_text = m_rects[entity]->text;
		gui_text->vertical_align = align;
	}

	void setTextHAlign(EntityRef entity, TextHAlign value) override
	{
		GUIText* gui_text = m_rects[entity]->text;
		gui_text->horizontal_align = value;
	}


	void setText(EntityRef entity, const char* value) override
	{
		GUIText* gui_text = m_rects[entity]->text;
		gui_text->text = value;
	}


	const char* getText(EntityRef entity) override
	{
		GUIText* text = m_rects[entity]->text;
		return text->text.c_str();
	}


	~GUIModuleImpl() {
		for (GUIRect* rect : m_rects) {
			if (rect->flags.isSet(GUIRect::IS_VALID)) {
				LUMIX_DELETE(m_allocator, rect->input_field);
				LUMIX_DELETE(m_allocator, rect->image);
				LUMIX_DELETE(m_allocator, rect->text);
				LUMIX_DELETE(m_allocator, rect);
			}
		}
	}


	void hoverOut(const GUIRect& rect)
	{
		auto iter = m_buttons.find(rect.entity);
		if (!iter.isValid()) return;
		m_rect_hovered_out.invoke(rect.entity);
	}


	void hover(const GUIRect& rect)
	{
		auto iter = m_buttons.find(rect.entity);
		if (!iter.isValid()) return;
		m_rect_hovered.invoke(rect.entity);
	}


	void handleMouseAxisEvent(const Rect& parent_rect, GUIRect& rect, const Vec2& mouse_pos, const Vec2& prev_mouse_pos)
	{
		if (!rect.flags.isSet(GUIRect::IS_ENABLED)) return;

		const Rect& r = getRectOnCanvas(parent_rect, rect);

		const bool is = contains(r, mouse_pos);
		const bool was = contains(r, prev_mouse_pos);
		if (is != was && m_buttons.find(rect.entity).isValid()) {
			is  ? hover(rect) : hoverOut(rect);
		}

		for (EntityRef e : m_world.childrenOf(rect.entity)) {
			auto iter = m_rects.find(e);
			if (!iter.isValid()) continue;
			handleMouseAxisEvent(r, *iter.value(), mouse_pos, prev_mouse_pos);
		}
	}

	static bool contains(const Rect& rect, const Vec2& pos)
	{
		return pos.x >= rect.x && pos.y >= rect.y && pos.x <= rect.x + rect.w && pos.y <= rect.y + rect.h;
	}


	bool isButtonDown(EntityRef e) const
	{
		for(int i = 0, c = m_buttons_down_count; i < c; ++i)
		{
			if (m_buttons_down[i] == e) return true;
		}
		return false;
	}


	bool handleMouseButtonEvent(const Rect& parent_rect, const GUIRect& rect, const InputSystem::Event& event)
	{
		if (!rect.flags.isSet(GUIRect::IS_ENABLED)) return false;
		const bool is_up = !event.data.button.down;

		Vec2 pos(event.data.button.x, event.data.button.y);
		const Rect& r = getRectOnCanvas(parent_rect, rect);
		bool handled = false;
		
		if (contains(r, pos)) {
			if (!is_up) m_rect_mouse_down.invoke(rect.entity, event.data.button.x, event.data.button.y);
			if (contains(r, m_mouse_down_pos)) {
				auto button_iter = m_buttons.find(rect.entity);
				if (button_iter.isValid())
				{
					handled = true;
					if (is_up && isButtonDown(rect.entity))
					{
						m_focused_entity = INVALID_ENTITY;
						m_button_clicked.invoke(rect.entity);
					}
					if (!is_up)
					{
						if (m_buttons_down_count < lengthOf(m_buttons_down))
						{
							m_buttons_down[m_buttons_down_count] = rect.entity;
							++m_buttons_down_count;
						}
						else
						{
							logError("Too many buttons pressed at once");
						}
					}
				}
			
				if (rect.input_field && is_up) {
					handled = true;
					m_focused_entity = rect.entity;
					if (rect.text)
					{
						rect.input_field->cursor = rect.text->text.length();
						rect.input_field->anim = 0;
					}
				}
			}
		}

		for (EntityRef e : m_world.childrenOf(rect.entity)) {
			auto iter = m_rects.find((EntityRef)e);
			if (!iter.isValid()) continue;
			handled = handleMouseButtonEvent(r, *iter.value(), event) || handled;
		}
		return handled;
	}


	GUIRect* getInput(EntityPtr e)
	{
		if (!e.isValid()) return nullptr;

		auto iter = m_rects.find((EntityRef)e);
		if (!iter.isValid()) return nullptr;

		GUIRect* rect = iter.value();
		if (!rect->text) return nullptr;
		if (!rect->input_field) return nullptr;

		return rect;
	}


	void handleTextInput(const InputSystem::Event& event)
	{
		const GUIRect* rect = getInput(m_focused_entity);
		if (!rect) return;
		char tmp[5] = {};
		memcpy(tmp, &event.data.text.utf8, sizeof(event.data.text.utf8));
		rect->text->text.insert(rect->input_field->cursor, tmp);
		++rect->input_field->cursor;
	}


	void handleKeyboardButtonEvent(const InputSystem::Event& event)
	{
		const GUIRect* rect = getInput(m_focused_entity);
		if (!rect) return;
		if (!event.data.button.down) return;

		rect->input_field->anim = 0;

		switch ((os::Keycode)event.data.button.key_id)
		{
		case os::Keycode::HOME: rect->input_field->cursor = 0; break;
			case os::Keycode::END: rect->input_field->cursor = rect->text->text.length(); break;
			case os::Keycode::BACKSPACE:
				if (rect->text->text.length() > 0 && rect->input_field->cursor > 0)
				{
					rect->text->text.eraseAt(rect->input_field->cursor - 1);
					--rect->input_field->cursor;
				}
				break;
			case os::Keycode::DEL:
				if (rect->input_field->cursor < rect->text->text.length())
				{
					rect->text->text.eraseAt(rect->input_field->cursor);
				}
				break;
			case os::Keycode::LEFT:
				if (rect->input_field->cursor > 0) --rect->input_field->cursor;
				break;
			case os::Keycode::RIGHT:
				if (rect->input_field->cursor < rect->text->text.length()) ++rect->input_field->cursor;
				break;
			default: break;
		}
	}


	void handleInput()
	{
		InputSystem& input = m_system.getEngine().getInputSystem();
		Span<const InputSystem::Event> events = input.getEvents();
		static Vec2 old_pos = {0, 0};
		for (const InputSystem::Event& event : events) {
			switch (event.type) {
				case InputSystem::Event::TEXT_INPUT:
					handleTextInput(event);
					break;
				case InputSystem::Event::AXIS:
					if (event.device->type == InputSystem::Device::MOUSE) {
						Vec2 pos(event.data.axis.x_abs, event.data.axis.y_abs);
						m_cursor_pos = IVec2((i32)pos.x, (i32)pos.y);
						for (const GUICanvas& canvas : m_canvas) {
							auto iter = m_rects.find(canvas.entity);
							if (iter.isValid()) {
								GUIRect* r = iter.value();
								handleMouseAxisEvent({0, 0,  m_canvas_size.x, m_canvas_size.y }, *r, pos, old_pos);
							}
						}
						old_pos = pos;
					}
					break;
				case InputSystem::Event::BUTTON:
					if (event.device->type == InputSystem::Device::MOUSE) {
						if (event.data.button.key_id != (u32)os::MouseButton::LEFT) break;
						if (event.data.button.down) {
							m_mouse_down_pos.x = event.data.button.x;
							m_mouse_down_pos.y = event.data.button.y;
						}
						bool handled = false;
						for (const GUICanvas& canvas : m_canvas) {
							auto iter = m_rects.find(canvas.entity);
							if (iter.isValid()) {
								GUIRect* r = iter.value();
								handled = handleMouseButtonEvent({ 0, 0, m_canvas_size.x, m_canvas_size.y }, *r, event);
								if (handled) break;
							}
						}
						if (!handled) {
							m_unhandled_mouse_button.invoke(event.data.button.down, (i32)event.data.button.x, (i32)event.data.button.y);
						}
						if (!event.data.button.down) m_buttons_down_count = 0;
					}
					else if (event.device->type == InputSystem::Device::KEYBOARD) {
						handleKeyboardButtonEvent(event);
					}
					break;
				case InputSystem::Event::DEVICE_ADDED:	
				case InputSystem::Event::DEVICE_REMOVED:
					break;
			}
		}
	}


	void blinkCursor(float time_delta)
	{
		GUIRect* rect = getInput(m_focused_entity);
		if (!rect) return;

		rect->input_field->anim += time_delta;
		rect->input_field->anim = fmodf(rect->input_field->anim, CURSOR_BLINK_PERIOD);
	}


	void update(float time_delta) override
	{
		handleInput();
		m_system.setCursor(m_cursor_type);
		blinkCursor(time_delta);
	}


	void createRect(EntityRef entity)
	{
		auto iter = m_rects.find(entity);
		GUIRect* rect;
		if (iter.isValid()) {
			rect = iter.value();
		}
		else {
			rect = LUMIX_NEW(m_allocator, GUIRect);
			m_rects.insert(entity, rect);
		}
		rect->top = {0, 0};
		rect->right = {0, 1};
		rect->bottom = {0, 1};
		rect->left = {0, 0};
		rect->entity = entity;
		rect->flags.set(GUIRect::IS_VALID);
		rect->flags.set(GUIRect::IS_ENABLED);
		m_world.onComponentCreated(entity, GUI_RECT_TYPE, this);
	}


	void createText(EntityRef entity)
	{
		auto iter = m_rects.find(entity);
		if (!iter.isValid())
		{
			createRect(entity);
			iter = m_rects.find(entity);
		}
		GUIRect& rect = *iter.value();
		rect.text = LUMIX_NEW(m_allocator, GUIText)(m_allocator);

		m_world.onComponentCreated(entity, GUI_TEXT_TYPE, this);
	}


	void createRenderTarget(EntityRef entity)
	{
		auto iter = m_rects.find(entity);
		if (!iter.isValid())
		{
			createRect(entity);
			iter = m_rects.find(entity);
		}
		iter.value()->render_target = &EMPTY_RENDER_TARGET;
		m_world.onComponentCreated(entity, GUI_RENDER_TARGET_TYPE, this);
	}


	void createButton(EntityRef entity)
	{
		auto iter = m_rects.find(entity);
		if (!iter.isValid())
		{
			createRect(entity);
			iter = m_rects.find(entity);
		}
		GUIImage* image = iter.value()->image;
		GUIButton& button = m_buttons.insert(entity);
		if (image) {
			button.hovered_color = image->color;
		}
		m_world.onComponentCreated(entity, GUI_BUTTON_TYPE, this);
	}
	

	void createCanvas(EntityRef entity)
	{
		GUICanvas& canvas = m_canvas.insert(entity);
		canvas.entity = entity;
		m_world.onComponentCreated(entity, GUI_CANVAS_TYPE, this);
	}

	void createInputField(EntityRef entity)
	{
		auto iter = m_rects.find(entity);
		if (!iter.isValid())
		{
			createRect(entity);
			iter = m_rects.find(entity);
		}
		GUIRect& rect = *iter.value();
		rect.input_field = LUMIX_NEW(m_allocator, GUIInputField);

		m_world.onComponentCreated(entity, GUI_INPUT_FIELD_TYPE, this);
	}


	void createImage(EntityRef entity)
	{
		auto iter = m_rects.find(entity);
		if (!iter.isValid())
		{
			createRect(entity);
			iter = m_rects.find(entity);
		}
		GUIRect& rect = *iter.value();
		rect.image = LUMIX_NEW(m_allocator, GUIImage);
		rect.image->flags.set(GUIImage::IS_ENABLED);

		m_world.onComponentCreated(entity, GUI_IMAGE_TYPE, this);
	}


	void destroyRect(EntityRef entity)
	{
		GUIRect* rect = m_rects[entity];
		rect->flags.set(GUIRect::IS_VALID, false);
		if (!rect->image && !rect->text && !rect->input_field && !rect->render_target)
		{
			LUMIX_DELETE(m_allocator, rect);
			m_rects.erase(entity);
		}
		m_world.onComponentDestroyed(entity, GUI_RECT_TYPE, this);
	}


	void destroyButton(EntityRef entity)
	{
		m_buttons.erase(entity);
		m_world.onComponentDestroyed(entity, GUI_BUTTON_TYPE, this);
	}

	void destroyCanvas(EntityRef entity) {
		m_canvas.erase(entity);
		m_world.onComponentDestroyed(entity, GUI_CANVAS_TYPE, this);
	}

	void destroyRenderTarget(EntityRef entity)
	{
		GUIRect* rect = m_rects[entity];
		rect->render_target = nullptr;
		m_world.onComponentDestroyed(entity, GUI_RENDER_TARGET_TYPE, this);
		checkGarbage(*rect);
	}


	void destroyInputField(EntityRef entity)
	{
		GUIRect* rect = m_rects[entity];
		LUMIX_DELETE(m_allocator, rect->input_field);
		rect->input_field = nullptr;
		m_world.onComponentDestroyed(entity, GUI_INPUT_FIELD_TYPE, this);
		checkGarbage(*rect);
	}


	void checkGarbage(GUIRect& rect) {
		if (rect.image) return;
		if (rect.text) return;
		if (rect.input_field) return;
		if (rect.render_target) return;
		if (rect.flags.isSet(GUIRect::IS_VALID)) return;
			
		const EntityRef e = rect.entity;
		LUMIX_DELETE(m_allocator, &rect);
		m_rects.erase(e);
	}


	void destroyImage(EntityRef entity)
	{
		GUIRect* rect = m_rects[entity];
		LUMIX_DELETE(m_allocator, rect->image);
		rect->image = nullptr;
		m_world.onComponentDestroyed(entity, GUI_IMAGE_TYPE, this);
		checkGarbage(*rect);
	}


	void destroyText(EntityRef entity)
	{
		GUIRect* rect = m_rects[entity];
		LUMIX_DELETE(m_allocator, rect->text);
		rect->text = nullptr;
		m_world.onComponentDestroyed(entity, GUI_TEXT_TYPE, this);
		checkGarbage(*rect);
	}


	void serialize(OutputMemoryStream& serializer) override
	{
		serializer.write(m_rects.size());
		for (GUIRect* rect : m_rects)
		{
			serializer.write(rect->flags);
			serializer.write(rect->entity);
			serializer.write(rect->top);
			serializer.write(rect->right);
			serializer.write(rect->bottom);
			serializer.write(rect->left);

			serializer.write(rect->image != nullptr);
			if (rect->image)
			{
				serializer.writeString(rect->image->sprite ? rect->image->sprite->getPath().c_str() : "");
				serializer.write(rect->image->color);
				serializer.write(rect->image->flags);
			}

			serializer.write(rect->input_field != nullptr);

			serializer.write(rect->text != nullptr);
			if (rect->text)
			{
				serializer.writeString(rect->text->getFontResource() ? rect->text->getFontResource()->getPath().c_str() : "");
				serializer.write(rect->text->horizontal_align);
				serializer.write(rect->text->vertical_align);
				serializer.write(rect->text->color);
				serializer.write(rect->text->getFontSize());
				serializer.write(rect->text->text);
			}
		}

		serializer.write(m_buttons.size());
		
		for (auto iter = m_buttons.begin(); iter.isValid(); ++iter)
		{
			serializer.write(iter.key());
			const GUIButton& button = iter.value();
			serializer.write(button.hovered_color);
			serializer.write(button.hovered_cursor);
		}

		serializer.write(m_canvas.size());
		
		for (GUICanvas& c : m_canvas) {
			serializer.write(c.entity);
			serializer.write(c.is_3d);
			serializer.write(c.orient_to_camera);
			serializer.write(c.virtual_size);
		}
	}


	void deserialize(InputMemoryStream& serializer, const EntityMap& entity_map, i32 version) override
	{
		u32 count = serializer.read<u32>();
		for (u32 i = 0; i < count; ++i)
		{
			GUIRect::Flags flags;
			EntityRef entity;
			serializer.read(flags);
			serializer.read(entity);
			entity = entity_map.get(entity);
			auto iter = m_rects.find(entity);
			if (!iter.isValid()) {
				GUIRect* rect = LUMIX_NEW(m_allocator, GUIRect);
				iter = m_rects.insert(entity, rect);
			}
			GUIRect* rect = iter.value();
			rect->entity = entity;
			rect->flags.clear();
			rect->flags.set(flags);

			serializer.read(rect->top);
			serializer.read(rect->right);
			serializer.read(rect->bottom);
			serializer.read(rect->left);
			if (rect->flags.isSet(GUIRect::IS_VALID)) {
				m_world.onComponentCreated(rect->entity, GUI_RECT_TYPE, this);
			}

			bool has_image = serializer.read<bool>();
			if (has_image)
			{
				rect->image = LUMIX_NEW(m_allocator, GUIImage);
				const char* tmp = serializer.readString();
				if (tmp[0] == '\0')
				{
					rect->image->sprite = nullptr;
				}
				else
				{
					ResourceManagerHub& manager = m_system.getEngine().getResourceManager();
					rect->image->sprite = manager.load<Sprite>(Path(tmp));
				}
				serializer.read(rect->image->color);
				serializer.read(rect->image->flags);
				m_world.onComponentCreated(rect->entity, GUI_IMAGE_TYPE, this);

			}
			bool has_input_field = serializer.read<bool>();
			if (has_input_field)
			{
				rect->input_field = LUMIX_NEW(m_allocator, GUIInputField);
				m_world.onComponentCreated(rect->entity, GUI_INPUT_FIELD_TYPE, this);
			}
			bool has_text = serializer.read<bool>();
			if (has_text)
			{
				rect->text = LUMIX_NEW(m_allocator, GUIText)(m_allocator);
				GUIText& text = *rect->text;
				const char* tmp = serializer.readString();
				serializer.read(text.horizontal_align);
				serializer.read(text.vertical_align);
				serializer.read(text.color);
				int font_size;
				serializer.read(font_size);
				text.setFontSize(font_size);
				serializer.read(text.text);
				FontResource* res = tmp[0] == 0 ? nullptr : m_font_manager->getOwner().load<FontResource>(Path(tmp));
				text.setFontResource(res);
				m_world.onComponentCreated(rect->entity, GUI_TEXT_TYPE, this);
			}
		}
		
		count = serializer.read<u32>();
		for (u32 i = 0; i < count; ++i) {
			EntityRef e;
			serializer.read(e);
			e = entity_map.get(e);
			GUIButton& button = m_buttons.insert(e);
			serializer.read(button.hovered_color);
			serializer.read(button.hovered_cursor);
			m_world.onComponentCreated(e, GUI_BUTTON_TYPE, this);
		}
		
		count = serializer.read<u32>();
		for (u32 i = 0; i < count; ++i) {
			GUICanvas canvas;
			serializer.read(canvas.entity);
			serializer.read(canvas.is_3d);
			if (version > (i32)Version::CANVAS_3D) {
				serializer.read(canvas.orient_to_camera);
				serializer.read(canvas.virtual_size);
			}

			canvas.entity = entity_map.get(canvas.entity);
			m_canvas.insert(canvas.entity, canvas);
			
			m_world.onComponentCreated(canvas.entity, GUI_CANVAS_TYPE, this);
		}
	}
	
	void setRenderTarget(EntityRef entity, gpu::TextureHandle* texture_handle) override
	{
		m_rects[entity]->render_target = texture_handle;
	}

	DelegateList<void(EntityRef)>& buttonClicked() override { return m_button_clicked; }
	DelegateList<void(EntityRef)>& rectHovered() override { return m_rect_hovered; }
	DelegateList<void(EntityRef)>& rectHoveredOut() override { return m_rect_hovered_out; }
	DelegateList<void(EntityRef, float, float)>& rectMouseDown() override { return m_rect_mouse_down; }
	DelegateList<void(bool, int, int)>& mousedButtonUnhandled() override { return m_unhandled_mouse_button; }

	World& getWorld() override { return m_world; }
	ISystem& getSystem() const override { return m_system; }

	IAllocator& m_allocator;
	World& m_world;
	GUISystem& m_system;
	
	HashMap<EntityRef, GUIRect*> m_rects;
	HashMap<EntityRef, GUIButton> m_buttons;
	HashMap<EntityRef, GUICanvas> m_canvas;
	EntityRef m_buttons_down[16];
	u32 m_buttons_down_count;
	EntityPtr m_focused_entity = INVALID_ENTITY;
	IVec2 m_cursor_pos = {-10000, -10000};
	os::CursorType m_cursor_type = os::CursorType::DEFAULT;
	bool m_cursor_set;
	FontManager* m_font_manager = nullptr;
	Vec2 m_canvas_size;
	Vec2 m_mouse_down_pos;
	DelegateList<void(EntityRef)> m_button_clicked;
	DelegateList<void(EntityRef)> m_rect_hovered;
	DelegateList<void(EntityRef)> m_rect_hovered_out;
	DelegateList<void(EntityRef, float, float)> m_rect_mouse_down;
	DelegateList<void(bool, i32, i32)> m_unhandled_mouse_button;
	Draw2D m_draw_2d;
};


UniquePtr<GUIModule> GUIModule::createInstance(GUISystem& system,
	World& world,
	IAllocator& allocator)
{
	return UniquePtr<GUIModuleImpl>::create(allocator, system, world, allocator);
}

void GUIModule::reflect() {
	struct TextHAlignEnum : reflection::EnumAttribute {
		u32 count(ComponentUID cmp) const override { return 3; }
		const char* name(ComponentUID cmp, u32 idx) const override {
			switch((GUIModule::TextHAlign)idx) {
				case GUIModule::TextHAlign::LEFT: return "Left";
				case GUIModule::TextHAlign::RIGHT: return "Right";
				case GUIModule::TextHAlign::CENTER: return "Center";
			}
			ASSERT(false);
			return "N/A";
		}
	};

	struct TextVAlignEnum : reflection::EnumAttribute {
		u32 count(ComponentUID cmp) const override { return 3; }
		const char* name(ComponentUID cmp, u32 idx) const override {
			switch((GUIModule::TextVAlign)idx) {
				case GUIModule::TextVAlign::TOP: return "Top";
				case GUIModule::TextVAlign::MIDDLE: return "Middle";
				case GUIModule::TextVAlign::BOTTOM: return "Bottom";
			}
			ASSERT(false);
			return "N/A";
		}
	};
		
	struct CursorEnum : reflection::EnumAttribute {
		u32 count(ComponentUID cmp) const override { return 7; }
		const char* name(ComponentUID cmp, u32 idx) const override {
			switch((os::CursorType)idx) {
				case os::CursorType::UNDEFINED: return "Ignore";
				case os::CursorType::DEFAULT: return "Default";
				case os::CursorType::LOAD: return "Load";
				case os::CursorType::SIZE_NS: return "Size NS";
				case os::CursorType::SIZE_NWSE: return "Size NWSE";
				case os::CursorType::SIZE_WE: return "Size WE";
				case os::CursorType::TEXT_INPUT: return "Text input";
			}
			ASSERT(false);
			return "N/A";
		}
	};

	LUMIX_MODULE(GUIModuleImpl, "gui")
		.LUMIX_EVENT(GUIModule::buttonClicked)
		.LUMIX_EVENT(GUIModule::rectHovered)
		.LUMIX_EVENT(GUIModule::rectHoveredOut)
		.LUMIX_EVENT(GUIModule::rectMouseDown)
		.LUMIX_EVENT(GUIModule::mousedButtonUnhandled)
		.LUMIX_FUNC(GUIModule::getRectAt)
		.LUMIX_FUNC(GUIModule::isOver)
		.LUMIX_CMP(RenderTarget, "gui_render_target", "GUI / Render taget")
		.LUMIX_CMP(Text, "gui_text", "GUI / Text")
			.icon(ICON_FA_FONT)
			.LUMIX_PROP(Text, "Text").multilineAttribute()
			.LUMIX_PROP(TextFontPath, "Font").resourceAttribute(FontResource::TYPE)
			.LUMIX_PROP(TextFontSize, "Font Size")
			.LUMIX_ENUM_PROP(TextHAlign, "Horizontal align").attribute<TextHAlignEnum>()
			.LUMIX_ENUM_PROP(TextVAlign, "Vertical align").attribute<TextVAlignEnum>()
			.LUMIX_PROP(TextColorRGBA, "Color").colorAttribute()
		.LUMIX_CMP(InputField, "gui_input_field", "GUI / Input field").icon(ICON_FA_KEYBOARD)
		.LUMIX_CMP(Canvas, "gui_canvas", "GUI / Canvas")
			.var_prop<&GUIModule::getCanvas, &GUICanvas::is_3d>("Is 3D")
			.var_prop<&GUIModule::getCanvas, &GUICanvas::orient_to_camera>("Orient to camera")
			.var_prop<&GUIModule::getCanvas, &GUICanvas::virtual_size>("Virtual size")
		.LUMIX_CMP(Button, "gui_button", "GUI / Button")
			.LUMIX_PROP(ButtonHoveredColorRGBA, "Hovered color").colorAttribute()
			.LUMIX_ENUM_PROP(ButtonHoveredCursor, "Cursor").attribute<CursorEnum>()
		.LUMIX_CMP(Image, "gui_image", "GUI / Image")
			.icon(ICON_FA_IMAGE)
			.prop<&GUIModule::isImageEnabled, &GUIModule::enableImage>("Enabled")
			.LUMIX_PROP(ImageColorRGBA, "Color").colorAttribute()
			.LUMIX_PROP(ImageSprite, "Sprite").resourceAttribute(Sprite::TYPE)
		.LUMIX_CMP(Rect, "gui_rect", "GUI / Rect")
			.prop<&GUIModule::isRectEnabled, &GUIModule::enableRect>("Enabled")
			.LUMIX_PROP(RectClip, "Clip content")
			.LUMIX_PROP(RectTopPoints, "Top Points")
			.LUMIX_PROP(RectTopRelative, "Top Relative")
			.LUMIX_PROP(RectRightPoints, "Right Points")
			.LUMIX_PROP(RectRightRelative, "Right Relative")
			.LUMIX_PROP(RectBottomPoints, "Bottom Points")
			.LUMIX_PROP(RectBottomRelative, "Bottom Relative")
			.LUMIX_PROP(RectLeftPoints, "Left Points")
			.LUMIX_PROP(RectLeftRelative, "Left Relative")
	;
}

} // namespace Lumix
