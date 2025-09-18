#pragma once

#include "engine/lumix.h"

#include "core/array.h"
#include "core/math.h"



namespace Lumix
{


struct Transform;
struct WorldView;
template <typename T> struct Array;


namespace Gizmo {

struct StepConfig {
	Vec3 position = Vec3(1, 1, 1);
	float rotation = PI;
	Vec3 scale = Vec3(1, 1, 1);
};

struct LUMIX_EDITOR_API Config {
	enum Mode {
		TRANSLATE,
		ROTATE,
		SCALE
	} mode = TRANSLATE;

	enum CoordSystem {
		GLOBAL,
		LOCAL
	} coord_system = GLOBAL;

	bool autosnap = false;
	float steps[3] = {};
	bool is_step = false;
	Vec3 offset = {};
	float scale = 1.f;
	bool is_grab = false;
	bool anisotropic_scale = false;
	StepConfig step_config;

	void grab();
	void ungrab();
	void lockXAxis();
	void lockYAxis();
	void lockZAxis();
	
	bool isTranslateMode() const { return mode == TRANSLATE; }
	bool isRotateMode() const { return mode == ROTATE; }
	bool isScaleMode() const { return mode == SCALE; }
	bool isLocalCoordSystem() const { return coord_system == LOCAL; }
	bool isGlobalCoordSystem() const { return coord_system == GLOBAL; }
	void enableStep(bool enable) { is_step = enable; }
	void setAnisotropicScale(bool enable) { anisotropic_scale = enable; }
};

LUMIX_EDITOR_API bool manipulate(u64 id, WorldView& view, Transform& tr, const Config& cfg);
LUMIX_EDITOR_API bool box(u64 id, WorldView& view, Transform& tr, Vec3& half_extents, const Config& cfg, bool keep_center);
LUMIX_EDITOR_API void setDragged(u64 id);
LUMIX_EDITOR_API bool isActive();
LUMIX_EDITOR_API void frame();

}


} // namespace Lumix
