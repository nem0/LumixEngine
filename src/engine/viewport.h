#pragma once


#include "engine/lumix.h"
#include "engine/quat.h"
#include "engine/vec.h"


namespace Lumix
{

struct Matrix;
struct ShiftedFrustum;

struct LUMIX_ENGINE_API Viewport
{
	Matrix getProjection(bool homogenous_depth) const;
	Matrix getView(const DVec3& origin) const;
	Matrix getViewRotation() const;
	ShiftedFrustum getFrustum() const;
	// TODO
	//Frustum getFrustum(const Vec2& viewport_min_px, const Vec2& viewport_max_px) const;
	Vec2 worldToScreenPixels(const DVec3& world) const;
	void getRay(const Vec2& screen_pos, DVec3& origin, Vec3& dir) const;


	bool is_ortho;
	union {
		float fov;
		float ortho_size;
	};
	int w;
	int h;
	DVec3 pos;
	Quat rot;
	float near;
	float far;
};

}