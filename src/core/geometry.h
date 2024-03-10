#pragma once

#include "core/core.h"
#include "core/math.h"

namespace Lumix {

struct AABB;
struct Matrix;

struct LUMIX_CORE_API Ray {
	DVec3 origin;
	Vec3 dir;
};

struct LUMIX_CORE_API Sphere {
	Sphere();
	Sphere(float x, float y, float z, float _radius);
	Sphere(const Vec3& point, float _radius);

	explicit Sphere(const Vec4& sphere);

	Vec3 position;
	float radius;
};


struct alignas(16) LUMIX_CORE_API Frustum {
	Frustum();

	void computeOrtho(const Vec3& position,
		const Vec3& direction,
		const Vec3& up,
		float width,
		float height,
		float near_distance,
		float far_distance);

	void computeOrtho(const Vec3& position,
		const Vec3& direction,
		const Vec3& up,
		float width,
		float height,
		float near_distance,
		float far_distance,
		const Vec2& viewport_min,
		const Vec2& viewport_max);

	void computePerspective(const Vec3& position,
		const Vec3& direction,
		const Vec3& up,
		float fov,
		float ratio,
		float near_distance,
		float far_distance);

	void computePerspective(const Vec3& position,
		const Vec3& direction,
		const Vec3& up,
		float fov,
		float ratio,
		float near_distance,
		float far_distance,
		const Vec2& viewport_min,
		const Vec2& viewport_max);

	bool intersectNearPlane(const Vec3& center, float radius) const;
	bool intersectAABB(const AABB& aabb) const;
	bool intersectAABBWithOffset(const AABB& aabb, float size_offset) const;
	bool isSphereInside(const Vec3& center, float radius) const;
	Sphere computeBoundingSphere() const;
	void transform(const Matrix& mtx);
	Frustum transformed(const Matrix& mtx) const;

	enum class Planes : u32 { 
		NEAR,
		FAR,
		LEFT,
		RIGHT,
		TOP,
		BOTTOM,
		EXTRA0,
		EXTRA1,
		COUNT
	};

	void setPlanesFromPoints();
	Vec3 getNormal(Planes side) const { return Vec3(xs[(int)side], ys[(int)side], zs[(int)side]); }
	void setPlane(Planes side, const Vec3& normal, const Vec3& point);
	void setPlane(Planes side, const Vec3& normal, float d);

	float xs[(int)Planes::COUNT];
	float ys[(int)Planes::COUNT];
	float zs[(int)Planes::COUNT];
	float ds[(int)Planes::COUNT];

	Vec3 points[8];
};


struct alignas(16) LUMIX_CORE_API ShiftedFrustum {
	void computeOrtho(const DVec3& position,
		const Vec3& direction,
		const Vec3& up,
		float width,
		float height,
		float near_distance,
		float far_distance,
		const Vec2& viewport_min,
		const Vec2& viewport_max);
	
	void computePerspective(const DVec3& position,
		const Vec3& direction,
		const Vec3& up,
		float fov,
		float ratio,
		float near_distance,
		float far_distance,
		const Vec2& viewport_min,
		const Vec2& viewport_max);
	
	void computeOrtho(const DVec3& position,
		const Vec3& direction,
		const Vec3& up,
		float width,
		float height,
		float near_distance,
		float far_distance);
	
	void computePerspective(const DVec3& position,
		const Vec3& direction,
		const Vec3& up,
		float fov,
		float ratio,
		float near_distance,
		float far_distance);

	bool containsAABB(const DVec3& pos, const Vec3& size) const;
	bool intersectsAABB(const DVec3& pos, const Vec3& size) const;
	Frustum getRelative(const DVec3& origin) const;
	void setPlanesFromPoints();
	void setPlane(Frustum::Planes side, const Vec3& normal, const Vec3& point);
	bool intersectNearPlane(const DVec3& center, float radius) const;
	Vec3 getNormal(Frustum::Planes plane) const;
	
	float xs[(int)Frustum::Planes::COUNT];
	float ys[(int)Frustum::Planes::COUNT];
	float zs[(int)Frustum::Planes::COUNT];
	float ds[(int)Frustum::Planes::COUNT];
	Vec3 points[8];
	DVec3 origin;
};

struct LUMIX_CORE_API AABB {
	AABB();
	AABB(const Vec3& _min, const Vec3& _max);
	
	void merge(const AABB& rhs);
	void addPoint(const Vec3& point);
	bool overlaps(const AABB& aabb) const;
	bool contains(const Vec3& point) const;
	void transform(const Matrix& matrix);
	void translate(const Vec3& v);
	void getCorners(const Transform& tr, DVec3* points) const;
	void getCorners(const Matrix& matrix, Vec3* points) const;
	static Vec3 minCoords(const Vec3& a, const Vec3& b);
	static Vec3 maxCoords(const Vec3& a, const Vec3& b);
	void shrink(float x);
	AABB intersection(const AABB& rhs) const;
	AABB operator*(float scale) { return {min * scale, max * scale}; }

	Vec3 min;
	Vec3 max;
};


struct LUMIX_CORE_API Viewport {
	Matrix getProjectionNoJitter() const;
	Matrix getProjectionWithJitter() const;
	Matrix getView(const DVec3& origin) const;
	Matrix getViewRotation() const;
	ShiftedFrustum getFrustum() const;
	ShiftedFrustum getFrustum(const Vec2& viewport_min_px, const Vec2& viewport_max_px) const;
	Vec2 worldToScreenPixels(const DVec3& world) const;
	Ray getRay(const Vec2& screen_pos) const;

	bool is_ortho;
	float fov;
	float ortho_size = 100.f;
	int w;
	int h;
	DVec3 pos;
	Quat rot;
	float near;
	float far;
	Vec2 pixel_offset = Vec2(0);
};

LUMIX_CORE_API Vec4 makePlane(const Vec3& normal, const Vec3& point);
LUMIX_CORE_API float planeDist(const Vec4& plane, const Vec3& point);

LUMIX_CORE_API bool getRayPlaneIntersecion(const Vec3& origin, const Vec3& dir, const Vec3& plane_point, const Vec3& normal, float& out);
LUMIX_CORE_API bool getRaySphereIntersection(const Vec3& origin, const Vec3& dir, const Vec3& center, float radius, float& out);
LUMIX_CORE_API bool getRayAABBIntersection(const Vec3& origin, const Vec3& dir, const Vec3& min, const Vec3& size, Vec3& out);
LUMIX_CORE_API float getLineSegmentDistance(const Vec3& origin, const Vec3& dir, const Vec3& a, const Vec3& b);
LUMIX_CORE_API bool getRayTriangleIntersection(const Vec3& origin, const Vec3& dir, const Vec3& a, const Vec3& b, const Vec3& c, float* out_t);
LUMIX_CORE_API bool getSphereTriangleIntersection(const Vec3& center, float radius, const Vec3& v0, const Vec3& v1, const Vec3& v2);
LUMIX_CORE_API bool testOBBCollision(const AABB& a, const Matrix& mtx_b, const AABB& b);
LUMIX_CORE_API bool testAABBTriangleCollision(const AABB& aabb, const Vec3& a, const Vec3& b, const Vec3& c);

} // namespace Lumix
