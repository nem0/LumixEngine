#pragma once


#include "engine/lumix.h"


namespace Lumix
{


struct Matrix;
struct Transform;
class WorldEditor;


class LUMIX_EDITOR_API Gizmo
{
public:
	static Gizmo* create(WorldEditor& editor);
	static void destroy(Gizmo& gizmo);

	virtual ~Gizmo() {}

	virtual bool isActive() const = 0;
	virtual void add(Entity entity) = 0;
	virtual void render() = 0;
	virtual void setTranslateMode() = 0;
	virtual void setRotateMode() = 0;
	virtual void setGlobalCoordSystem() = 0;
	virtual void setLocalCoordSystem() = 0;
	virtual int getStep() const = 0;
	virtual void setStep(int step) = 0;
	virtual void enableStep(bool enable) = 0;
	virtual bool isAutosnapDown() const = 0;
	virtual void setAutosnapDown(bool snap) = 0;
	virtual bool isTranslateMode() const = 0;
	virtual bool isRotateMode() const = 0;
	virtual bool isLocalCoordSystem() const = 0;
	virtual bool isGlobalCoordSystem() const = 0;
	virtual void setPivotCenter() = 0;
	virtual void setPivotOrigin() = 0;
	virtual bool isPivotCenter() const = 0;
	virtual bool isPivotOrigin() const = 0;
	virtual void clearEntities() = 0;

	virtual bool immediate(Transform& frame) = 0;
};


} // !namespace Lumix
