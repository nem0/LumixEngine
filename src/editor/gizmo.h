#pragma once


#include "lumix.h"


namespace Lumix
{


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
		virtual void toggleMode() = 0;
		virtual void togglePivot() = 0;
		virtual void toggleCoordSystem() = 0;
		virtual int getStep() const = 0;
		virtual void setStep(int step) = 0;
		virtual bool isAutosnapDown() const = 0;
		virtual void setAutosnapDown(bool snap) = 0;
		virtual bool isTranslateMode() const = 0;
};


} // !namespace Lumix
