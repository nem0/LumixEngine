#pragma once


#include "engine/iplugin.h"

namespace bgfx { struct TextureHandle; }


namespace Lumix
{


class GUISystem;
class Path;
class Pipeline;
class string;
struct Vec2;
struct Vec4;
template <typename T> class DelegateList;


class GUIScene : public IScene
{
public:
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

	static GUIScene* createInstance(GUISystem& system,
		Universe& universe,
		struct IAllocator& allocator);
	static void destroyInstance(GUIScene* scene);

	virtual void render(Pipeline& pipeline, const Vec2& canvas_size) = 0;

	virtual bool hasGUI(EntityRef EntityRef) const = 0;
	virtual Rect getRectOnCanvas(EntityRef EntityRef, const Vec2& canva_size) const = 0;
	virtual Rect getRect(EntityRef EntityRef) const = 0;
	virtual EntityRef getRectAt(const Vec2& pos, const Vec2& canvas_size) const = 0;

	virtual void enableRect(EntityRef EntityRef, bool enable) = 0;
	virtual bool isRectEnabled(EntityRef EntityRef) = 0;
	virtual bool getRectClip(EntityRef EntityRef) = 0;
	virtual void setRectClip(EntityRef EntityRef, bool value) = 0;
	virtual float getRectLeftPoints(EntityRef EntityRef) = 0;
	virtual void setRectLeftPoints(EntityRef EntityRef, float value) = 0;
	virtual float getRectLeftRelative(EntityRef EntityRef) = 0;
	virtual void setRectLeftRelative(EntityRef EntityRef, float value) = 0;

	virtual float getRectRightPoints(EntityRef EntityRef) = 0;
	virtual void setRectRightPoints(EntityRef EntityRef, float value) = 0;
	virtual float getRectRightRelative(EntityRef EntityRef) = 0;
	virtual void setRectRightRelative(EntityRef EntityRef, float value) = 0;

	virtual float getRectTopPoints(EntityRef EntityRef) = 0;
	virtual void setRectTopPoints(EntityRef EntityRef, float value) = 0;
	virtual float getRectTopRelative(EntityRef EntityRef) = 0;
	virtual void setRectTopRelative(EntityRef EntityRef, float value) = 0;

	virtual float getRectBottomPoints(EntityRef EntityRef) = 0;
	virtual void setRectBottomPoints(EntityRef EntityRef, float value) = 0;
	virtual float getRectBottomRelative(EntityRef EntityRef) = 0;
	virtual void setRectBottomRelative(EntityRef EntityRef, float value) = 0;

	virtual Vec4 getButtonNormalColorRGBA(EntityRef EntityRef) = 0;
	virtual void setButtonNormalColorRGBA(EntityRef EntityRef, const Vec4& color) = 0;
	virtual Vec4 getButtonHoveredColorRGBA(EntityRef EntityRef) = 0;
	virtual void setButtonHoveredColorRGBA(EntityRef EntityRef, const Vec4& color) = 0;

	virtual void enableImage(EntityRef EntityRef, bool enable) = 0;
	virtual bool isImageEnabled(EntityRef EntityRef) = 0;
	virtual Vec4 getImageColorRGBA(EntityRef EntityRef) = 0;
	virtual void setImageColorRGBA(EntityRef EntityRef, const Vec4& color) = 0;
	virtual Path getImageSprite(EntityRef EntityRef) = 0;
	virtual void setImageSprite(EntityRef EntityRef, const Path& path) = 0;

	virtual void setText(EntityRef EntityRef, const char* text) = 0;
	virtual const char* getText(EntityRef EntityRef) = 0;
	virtual TextHAlign getTextHAlign(EntityRef EntityRef) = 0;
	virtual void setTextHAlign(EntityRef EntityRef, TextHAlign align) = 0;
	virtual void setTextFontSize(EntityRef EntityRef, int value) = 0;
	virtual int getTextFontSize(EntityRef EntityRef) = 0;
	virtual Vec4 getTextColorRGBA(EntityRef EntityRef) = 0;
	virtual void setTextColorRGBA(EntityRef EntityRef, const Vec4& color) = 0;
	virtual Path getTextFontPath(EntityRef EntityRef) = 0;
	virtual void setTextFontPath(EntityRef EntityRef, const Path& path) = 0;

	virtual void setRenderTarget(EntityRef EntityRef, bgfx::TextureHandle* texture_handle) = 0;

	virtual DelegateList<void(EntityRef)>& buttonClicked() = 0;
	virtual DelegateList<void(EntityRef)>& rectHovered() = 0;
	virtual DelegateList<void(EntityRef)>& rectHoveredOut() = 0;
};


} // namespace Lumix