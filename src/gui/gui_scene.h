#pragma once


#include "engine/iplugin.h"


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

	virtual bool hasGUI(Entity entity) const = 0;
	virtual Rect getRectOnCanvas(Entity entity, const Vec2& canva_size) const = 0;
	virtual Rect getRect(Entity entity) const = 0;
	virtual Entity getRectAt(const Vec2& pos, const Vec2& canvas_size) const = 0;

	virtual void enableRect(Entity entity, bool enable) = 0;
	virtual bool isRectEnabled(Entity entity) = 0;
	virtual bool getRectClip(Entity entity) = 0;
	virtual void setRectClip(Entity entity, bool value) = 0;
	virtual float getRectLeftPoints(Entity entity) = 0;
	virtual void setRectLeftPoints(Entity entity, float value) = 0;
	virtual float getRectLeftRelative(Entity entity) = 0;
	virtual void setRectLeftRelative(Entity entity, float value) = 0;

	virtual float getRectRightPoints(Entity entity) = 0;
	virtual void setRectRightPoints(Entity entity, float value) = 0;
	virtual float getRectRightRelative(Entity entity) = 0;
	virtual void setRectRightRelative(Entity entity, float value) = 0;

	virtual float getRectTopPoints(Entity entity) = 0;
	virtual void setRectTopPoints(Entity entity, float value) = 0;
	virtual float getRectTopRelative(Entity entity) = 0;
	virtual void setRectTopRelative(Entity entity, float value) = 0;

	virtual float getRectBottomPoints(Entity entity) = 0;
	virtual void setRectBottomPoints(Entity entity, float value) = 0;
	virtual float getRectBottomRelative(Entity entity) = 0;
	virtual void setRectBottomRelative(Entity entity, float value) = 0;

	virtual Vec4 getButtonNormalColorRGBA(Entity entity) = 0;
	virtual void setButtonNormalColorRGBA(Entity entity, const Vec4& color) = 0;
	virtual Vec4 getButtonHoveredColorRGBA(Entity entity) = 0;
	virtual void setButtonHoveredColorRGBA(Entity entity, const Vec4& color) = 0;

	virtual void enableImage(Entity entity, bool enable) = 0;
	virtual bool isImageEnabled(Entity entity) = 0;
	virtual Vec4 getImageColorRGBA(Entity entity) = 0;
	virtual void setImageColorRGBA(Entity entity, const Vec4& color) = 0;
	virtual Path getImageSprite(Entity entity) = 0;
	virtual void setImageSprite(Entity entity, const Path& path) = 0;

	virtual void setText(Entity entity, const char* text) = 0;
	virtual const char* getText(Entity entity) = 0;
	virtual TextHAlign getTextHAlign(Entity entity) = 0;
	virtual void setTextHAlign(Entity entity, TextHAlign align) = 0;
	virtual void setTextFontSize(Entity entity, int value) = 0;
	virtual int getTextFontSize(Entity entity) = 0;
	virtual Vec4 getTextColorRGBA(Entity entity) = 0;
	virtual void setTextColorRGBA(Entity entity, const Vec4& color) = 0;
	virtual Path getTextFontPath(Entity entity) = 0;
	virtual void setTextFontPath(Entity entity, const Path& path) = 0;

	virtual DelegateList<void(Entity)>& buttonClicked() = 0;
	virtual DelegateList<void(Entity)>& rectHovered() = 0;
	virtual DelegateList<void(Entity)>& rectHoveredOut() = 0;
};


} // namespace Lumix