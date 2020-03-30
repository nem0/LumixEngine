#pragma once


#include "engine/plugin.h"


namespace Lumix
{

namespace gpu { struct TextureHandle; }

template <typename T> struct DelegateList;


struct GUIScene : IScene
{
	enum class TextHAlign : int
	{
		LEFT,
		CENTER,
		RIGHT
	};

	struct Rect
	{
		float x, y, w, h;
	};

	static GUIScene* createInstance(struct GUISystem& system,
		Universe& universe,
		struct IAllocator& allocator);
	static void destroyInstance(GUIScene* scene);

	virtual void render(struct Pipeline& pipeline, const struct Vec2& canvas_size) = 0;
	virtual IVec2 getCursorPosition() = 0;

	virtual bool hasGUI(EntityRef entity) const = 0;
	virtual Rect getRectOnCanvas(EntityPtr entity, const Vec2& canva_size) const = 0;
	virtual Rect getRect(EntityRef entity) const = 0;
	virtual EntityPtr getRectAt(const Vec2& pos, const Vec2& canvas_size) const = 0;

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

	virtual struct Vec4 getButtonNormalColorRGBA(EntityRef entity) = 0;
	virtual void setButtonNormalColorRGBA(EntityRef entity, const Vec4& color) = 0;
	virtual Vec4 getButtonHoveredColorRGBA(EntityRef entity) = 0;
	virtual void setButtonHoveredColorRGBA(EntityRef entity, const Vec4& color) = 0;

	virtual void enableImage(EntityRef entity, bool enable) = 0;
	virtual bool isImageEnabled(EntityRef entity) = 0;
	virtual Vec4 getImageColorRGBA(EntityRef entity) = 0;
	virtual void setImageColorRGBA(EntityRef entity, const Vec4& color) = 0;
	virtual struct Path getImageSprite(EntityRef entity) = 0;
	virtual void setImageSprite(EntityRef entity, const Path& path) = 0;

	virtual void setText(EntityRef entity, const char* text) = 0;
	virtual const char* getText(EntityRef entity) = 0;
	virtual TextHAlign getTextHAlign(EntityRef entity) = 0;
	virtual void setTextHAlign(EntityRef entity, TextHAlign align) = 0;
	virtual void setTextFontSize(EntityRef entity, int value) = 0;
	virtual int getTextFontSize(EntityRef entity) = 0;
	virtual Vec4 getTextColorRGBA(EntityRef entity) = 0;
	virtual void setTextColorRGBA(EntityRef entity, const Vec4& color) = 0;
	virtual Path getTextFontPath(EntityRef entity) = 0;
	virtual void setTextFontPath(EntityRef entity, const Path& path) = 0;

	virtual void setRenderTarget(EntityRef entity, gpu::TextureHandle* texture_handle) = 0;

	virtual DelegateList<void(EntityRef)>& buttonClicked() = 0;
	virtual DelegateList<void(EntityRef)>& rectHovered() = 0;
	virtual DelegateList<void(EntityRef)>& rectHoveredOut() = 0;
	virtual DelegateList<void(bool, i32, i32)>& mousedButtonUnhandled() = 0;
};


} // namespace Lumix