#pragma once


#include "core/vec3.h"
#include "core/lumix.h"


namespace Lumix
{


struct Vec3;
struct Matrix;


struct LUX_CORE_API Quat
{
	Quat() {}
	Quat(const Vec3& axis, float angle);
	Quat(float _x, float _y, float _z, float _w) { x = _x; y = _y; z = _z; w = _w; } 


	float x, y, z, w;

	void set(float _x, float _y, float _z, float _w) { x = _x; y = _y; z = _z; w = _w; } 
	void conjugate();
	void conjugated(Quat& q);
	void normalize();
	void toMatrix(Matrix& mtx) const;
	
	inline Vec3 operator *(const Vec3& v) const;
	inline Quat operator *(const Quat& q) const;
	Quat operator -() const;
};


LUX_CORE_API void nlerp(const Quat& q1, const Quat& q2, Quat* out, float t);


Vec3 Quat::operator *(const Vec3& v) const
{
	// nVidia SDK implementation

	Vec3 uv, uuv;
	Vec3 qvec(x, y, z);
	uv = crossProduct(qvec, v);
	uuv = crossProduct(qvec, uv);
	uv *= (2.0f * w);
	uuv *= 2.0f;

	return v + uv + uuv;
}


} // !namespace Lumix
