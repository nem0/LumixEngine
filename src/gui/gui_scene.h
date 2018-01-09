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


class GUIScene : public IScene
{
public:
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
	virtual Entity getRectEntity(ComponentHandle cmp) const = 0;
	virtual ComponentHandle getRectAt(const Vec2& pos, const Vec2& canvas_size) const = 0;

	virtual void enableRect(ComponentHandle cmp, bool enable) = 0;
	virtual bool isRectEnabled(ComponentHandle cmp) = 0;
	virtual float getRectLeftPoints(ComponentHandle cmp) = 0;
	virtual void setRectLeftPoints(ComponentHandle cmp, float value) = 0;
	virtual float getRectLeftRelative(ComponentHandle cmp) = 0;
	virtual void setRectLeftRelative(ComponentHandle cmp, float value) = 0;

	virtual float getRectRightPoints(ComponentHandle cmp) = 0;
	virtual void setRectRightPoints(ComponentHandle cmp, float value) = 0;
	virtual float getRectRightRelative(ComponentHandle cmp) = 0;
	virtual void setRectRightRelative(ComponentHandle cmp, float value) = 0;

	virtual float getRectTopPoints(ComponentHandle cmp) = 0;
	virtual void setRectTopPoints(ComponentHandle cmp, float value) = 0;
	virtual float getRectTopRelative(ComponentHandle cmp) = 0;
	virtual void setRectTopRelative(ComponentHandle cmp, float value) = 0;

	virtual float getRectBottomPoints(ComponentHandle cmp) = 0;
	virtual void setRectBottomPoints(ComponentHandle cmp, float value) = 0;
	virtual float getRectBottomRelative(ComponentHandle cmp) = 0;
	virtual void setRectBottomRelative(ComponentHandle cmp, float value) = 0;

	virtual Vec4 getImageColorRGBA(ComponentHandle cmp) = 0;
	virtual void setImageColorRGBA(ComponentHandle cmp, const Vec4& color) = 0;
	virtual Path getImageSprite(ComponentHandle cmp) = 0;
	virtual void setImageSprite(ComponentHandle cmp, const Path& path) = 0;

	virtual void setText(ComponentHandle cmp, const char* text) = 0;
	virtual const char* getText(ComponentHandle cmp) = 0;
	virtual void setTextFontSize(ComponentHandle cmp, int value) = 0;
	virtual int getTextFontSize(ComponentHandle cmp) = 0;
	virtual Vec4 getTextColorRGBA(ComponentHandle cmp) = 0;
	virtual void setTextColorRGBA(ComponentHandle cmp, const Vec4& color) = 0;
	virtual Path getTextFontPath(ComponentHandle cmp) = 0;
	virtual void setTextFontPath(ComponentHandle cmp, const Path& path) = 0;
};


} // namespace Lumix