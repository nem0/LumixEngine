#pragma once


#include "engine/lumix.h"
#include "engine/vec.h"


namespace Lumix
{


struct Matrix;


struct LUMIX_ENGINE_API Quat
{
	struct AxisAngle
	{
		Vec3 axis;
		float angle;
	};

	Quat() {}
	Quat(const Vec3& axis, float angle);
	Quat(float _x, float _y, float _z, float _w) { x = _x; y = _y; z = _z; w = _w; } 

	void fromEuler(const Vec3& euler);
	Vec3 toEuler() const;
	AxisAngle getAxisAngle() const;
	void set(float _x, float _y, float _z, float _w) { x = _x; y = _y; z = _z; w = _w; } 
	void conjugate();
	Quat conjugated() const;
	void normalize();
	Quat normalized() const;
	Matrix toMatrix() const;

	Vec3 rotate(const Vec3& v) const
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

	Quat operator*(const Quat& q) const;
	Quat operator-() const;
	Quat operator+(const Quat& q) const;
	Quat operator*(float m) const;
	Vec3 operator*(const Vec3& q) const;

	static Quat vec3ToVec3(const Vec3& a, const Vec3& b);

	float x, y, z, w;
	
	static const Quat IDENTITY;
};


LUMIX_ENGINE_API void nlerp(const Quat& q1, const Quat& q2, Quat* out, float t);


} // namespace Lumix
