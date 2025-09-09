#pragma once


#include "engine/plugin.h"
#include "renderer/gpu/gpu.h"

namespace Lumix
{

namespace os { enum class CursorType : u32; }
struct Path;

template <typename T> struct DelegateList;

//@ module GUIModule gui "GUI"

//@ component_struct id gui_canvas
struct Canvas {
	EntityRef entity;
	bool is_3d = false;				//@ property
	bool orient_to_camera = true;	//@ property
	Vec2 virtual_size = Vec2(1000);	//@ property
};
//@ end

struct GUIRayHit {
	EntityPtr entity = INVALID_ENTITY;
	float t = -1;
};

//@ enum 
enum class TextHAlign : i32 {
	LEFT,
	CENTER,
	RIGHT
};

//@ enum
enum class TextVAlign : i32 {
	TOP,
	MIDDLE,
	BOTTOM
};

struct GUIModule : IModule {


	struct Rect {
		float x, y, w, h;
	};

	static UniquePtr<GUIModule> createInstance(struct GUISystem& system, World& world, struct IAllocator& allocator);
	static void reflect();

	virtual void render(struct Pipeline& pipeline, const struct Vec2& canvas_size, bool is_main, bool is_3d_only) = 0;
	virtual void renderCanvas(Pipeline& pipeline, const struct Vec2& canvas_size, bool is_main, EntityRef canvas_entity) = 0;
	virtual IVec2 getCursorPosition() = 0;
	virtual GUIRayHit raycast(const struct Ray& ray) = 0;

	virtual void createText(EntityRef entity) = 0;
	virtual void createImage(EntityRef entity) = 0;
	virtual void createButton(EntityRef entity) = 0;
	virtual void createRect(EntityRef entity) = 0;
	virtual void createCanvas(EntityRef entity) = 0;
	virtual void createRenderTarget(EntityRef entity) = 0;
	virtual void createInputField(EntityRef entity) = 0;
	virtual void destroyText(EntityRef entity) = 0;
	virtual void destroyImage(EntityRef entity) = 0;
	virtual void destroyButton(EntityRef entity) = 0;
	virtual void destroyRect(EntityRef entity) = 0;
	virtual void destroyCanvas(EntityRef entity) = 0;
	virtual void destroyRenderTarget(EntityRef entity) = 0;
	virtual void destroyInputField(EntityRef entity) = 0;

	virtual bool hasGUI(EntityRef entity) const = 0;
	virtual Rect getRectEx(EntityPtr entity, const Vec2& canvas_size) const = 0;
	virtual Rect getRect(EntityRef entity) const = 0;
	virtual EntityPtr getRectAtEx(const Vec2& pos, const Vec2& canvas_size, EntityPtr limit) const = 0;
	virtual EntityPtr getRectAtEx(const Vec2& pos, const Vec2& canvas_size, EntityPtr limit, EntityRef canvas) const = 0;
	//@ functions
	virtual EntityPtr getRectAt(const Vec2& pos) const = 0;
	virtual bool isOver(const Vec2& pos, EntityRef e) = 0;
	virtual GUISystem* getSystemPtr() const = 0; //@ label "getSystem"
	//@ end

	//@ component RenderTarget id gui_render_target
	//@ end

	//@ component InputField id gui_input_field icon ICON_FA_KEYBOARD
	//@ end

	//@ component Rect id gui_rect
	virtual void enableRect(EntityRef entity, bool enable) = 0;
	virtual bool isRectEnabled(EntityRef entity) = 0;
	virtual bool getRectClip(EntityRef entity) = 0;					//@ label "Clip content"
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

	//@ component Button id gui_button
	virtual Vec4 getButtonHoveredColorRGBA(EntityRef entity) = 0; //@ label "Hovered color" color
	virtual void setButtonHoveredColorRGBA(EntityRef entity, const Vec4& color) = 0;
	virtual os::CursorType getButtonHoveredCursor(EntityRef entity) = 0;
	virtual void setButtonHoveredCursor(EntityRef entity, os::CursorType cursor) = 0;
	//@ end

	//@ component Image id gui_image icon ICON_FA_IMAGE
	virtual void enableImage(EntityRef entity, bool enable) = 0;
	virtual bool isImageEnabled(EntityRef entity) = 0;
	virtual Vec4 getImageColorRGBA(EntityRef entity) = 0;						//@ label "Color" color
	virtual void setImageColorRGBA(EntityRef entity, const Vec4& color) = 0;
	virtual Path getImageSprite(EntityRef entity) = 0;							//@ resource_type Sprite::TYPE
	virtual void setImageSprite(EntityRef entity, const Path& path) = 0;
	//@ end

	virtual Canvas& getCanvas(EntityRef entity) = 0;
	virtual HashMap<EntityRef, Canvas>& getCanvases() = 0;

	//@ component Text id gui_text icon ICON_FA_FONT
	virtual void setTextFontSize(EntityRef entity, int value) = 0;			//@ min 0
	virtual int getTextFontSize(EntityRef entity) = 0;
	virtual Vec4 getTextColorRGBA(EntityRef entity) = 0; 					//@ label "Color" color
	virtual void setTextColorRGBA(EntityRef entity, const Vec4& color) = 0;
	virtual Path getTextFontPath(EntityRef entity) = 0;						//@ label "Font" resource_type FontResource::TYPE
	virtual void setTextFontPath(EntityRef entity, const Path& path) = 0;
	virtual TextHAlign getTextHAlign(EntityRef entity) = 0;					//@ label "Horizontal align"
	virtual void setTextHAlign(EntityRef entity, TextHAlign align) = 0;
	virtual TextVAlign getTextVAlign(EntityRef entity) = 0;					//@ label "Vertical align"
	virtual void setTextVAlign(EntityRef entity, TextVAlign align) = 0;
	virtual const char* getText(EntityRef entity) = 0;						//@ getter Text 
	virtual void setText(EntityRef entity, const char* text) = 0;			//@ setter Text multiline
	//@ end

	virtual void setRenderTarget(EntityRef entity, gpu::TextureHandle* texture_handle) = 0;

	//@ events
	virtual DelegateList<void(EntityRef)>& buttonClicked() = 0;
	virtual DelegateList<void(EntityRef)>& rectHovered() = 0;
	virtual DelegateList<void(EntityRef)>& rectHoveredOut() = 0;
	virtual DelegateList<void(EntityRef, float, float)>& rectMouseDown() = 0;
	virtual DelegateList<void(bool, i32, i32)>& mousedButtonUnhandled() = 0;
	//@ end
};


} // namespace Lumix