#pragma once


#include "engine/plugin.h"
#include "renderer/gpu/gpu.h"

namespace Lumix
{

namespace os { enum class CursorType : u32; }
struct Path;

template <typename T> struct DelegateList;

//@ module GUIModuleImpl gui "GUI"

// TODO reflection
struct GUICanvas {
	EntityRef entity;
	bool is_3d = false;
	bool orient_to_camera = true;
	Vec2 virtual_size = Vec2(1000);
};

struct GUIRayHit {
	EntityPtr entity = INVALID_ENTITY;
	float t = -1;
};

struct GUIModule : IModule {
	enum class TextHAlign : i32 {
		LEFT,
		CENTER,
		RIGHT
	};

	enum class TextVAlign : i32 {
		TOP,
		MIDDLE,
		BOTTOM
	};

	struct Rect {
		float x, y, w, h;
	};

	static UniquePtr<GUIModule> createInstance(struct GUISystem& system, World& world, struct IAllocator& allocator);
	static void reflect();

	virtual void render(struct Pipeline& pipeline, const struct Vec2& canvas_size, bool is_main, bool is_3d_only) = 0;
	virtual void renderCanvas(Pipeline& pipeline, const struct Vec2& canvas_size, bool is_main, EntityRef canvas_entity) = 0;
	virtual IVec2 getCursorPosition() = 0;
	virtual GUIRayHit raycast(const struct Ray& ray) = 0;

	virtual bool hasGUI(EntityRef entity) const = 0;
	virtual Rect getRectEx(EntityPtr entity, const Vec2& canvas_size) const = 0;
	virtual Rect getRect(EntityRef entity) const = 0;
	virtual EntityPtr getRectAtEx(const Vec2& pos, const Vec2& canvas_size, EntityPtr limit) const = 0;
	virtual EntityPtr getRectAtEx(const Vec2& pos, const Vec2& canvas_size, EntityPtr limit, EntityRef canvas) const = 0;
	virtual EntityPtr getRectAt(const Vec2& pos) const = 0;
	virtual bool isOver(const Vec2& pos, EntityRef e) = 0;

	//@ component Rect gui_rect "Rect"
	virtual void enableRect(EntityRef entity, bool enable) = 0;
	virtual bool isRectEnabled(EntityRef entity) = 0;
	virtual bool getRectClip(EntityRef entity) = 0;
	virtual void setRectClip(EntityRef entity, bool value) = 0;
	virtual float getRectLeftPoints(EntityRef entity) = 0;
	virtual void setRectLeftPoints(EntityRef entity, float value) = 0;
	virtual float getRectLeftRelative(EntityRef entity) = 0;
	virtual void setRectLeftRelative(EntityRef entity, float value) = 0;

	virtual float getRectRightPoints(EntityRef entity) = 0;
	virtual void setRectRightPoints(EntityRef entity, float value) = 0;
	virtual float getRectRightRelative(EntityRef entity) = 0;
	virtual void setRectRightRelative(EntityRef entity, float value) = 0;

	virtual float getRectTopPoints(EntityRef entity) = 0;
	virtual void setRectTopPoints(EntityRef entity, float value) = 0;
	virtual float getRectTopRelative(EntityRef entity) = 0;
	virtual void setRectTopRelative(EntityRef entity, float value) = 0;

	virtual float getRectBottomPoints(EntityRef entity) = 0;
	virtual void setRectBottomPoints(EntityRef entity, float value) = 0;
	virtual float getRectBottomRelative(EntityRef entity) = 0;
	virtual void setRectBottomRelative(EntityRef entity, float value) = 0;
	//@ end

	//@ component Button gui_button "Button"
	virtual Vec4 getButtonHoveredColorRGBA(EntityRef entity) = 0;
	virtual void setButtonHoveredColorRGBA(EntityRef entity, const Vec4& color) = 0;
	//@ end
	virtual os::CursorType getButtonHoveredCursor(EntityRef entity) = 0;
	virtual void setButtonHoveredCursor(EntityRef entity, os::CursorType cursor) = 0;

	//@ component Image gui_image "Image"
	virtual void enableImage(EntityRef entity, bool enable) = 0;
	virtual bool isImageEnabled(EntityRef entity) = 0;
	virtual Vec4 getImageColorRGBA(EntityRef entity) = 0;
	virtual void setImageColorRGBA(EntityRef entity, const Vec4& color) = 0;
	virtual Path getImageSprite(EntityRef entity) = 0;
	virtual void setImageSprite(EntityRef entity, const Path& path) = 0;
	//@ end

	virtual GUICanvas& getCanvas(EntityRef entity) = 0;
	virtual HashMap<EntityRef, GUICanvas>& getCanvases() = 0;

	//@ component Text gui_text "Text"
	virtual void setTextFontSize(EntityRef entity, int value) = 0;
	virtual int getTextFontSize(EntityRef entity) = 0;
	virtual Vec4 getTextColorRGBA(EntityRef entity) = 0;
	virtual void setTextColorRGBA(EntityRef entity, const Vec4& color) = 0;
	virtual Path getTextFontPath(EntityRef entity) = 0;
	virtual void setTextFontPath(EntityRef entity, const Path& path) = 0;
	//@ end
	virtual TextHAlign getTextHAlign(EntityRef entity) = 0;
	virtual void setTextHAlign(EntityRef entity, TextHAlign align) = 0;
	virtual TextVAlign getTextVAlign(EntityRef entity) = 0;
	virtual void setTextVAlign(EntityRef entity, TextVAlign align) = 0;
	virtual const char* getText(EntityRef entity) = 0;
	virtual void setText(EntityRef entity, const char* text) = 0;

	virtual void setRenderTarget(EntityRef entity, gpu::TextureHandle* texture_handle) = 0;

	virtual DelegateList<void(EntityRef)>& buttonClicked() = 0;
	virtual DelegateList<void(EntityRef)>& rectHovered() = 0;
	virtual DelegateList<void(EntityRef)>& rectHoveredOut() = 0;
	virtual DelegateList<void(EntityRef, float, float)>& rectMouseDown() = 0;
	virtual DelegateList<void(bool, i32, i32)>& mousedButtonUnhandled() = 0;
};


} // namespace Lumix