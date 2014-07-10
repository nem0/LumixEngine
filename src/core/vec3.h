#pragma once


#include "core/lux.h"
#include <cmath>


namespace Lux
{


struct LUX_CORE_API Vec3
{
	Vec3() {}

	Vec3(float a, float b, float c) : x(a), y(b), z(c) {}

	Vec3 operator +(const Vec3& rhs) const
	{
		return Vec3(x + rhs.x, y + rhs.y, z + rhs.z);
	}

	Vec3 operator-() const
	{
		return Vec3(-x, -y, -z);
	}

	Vec3 operator -(const Vec3& rhs) const
	{
		return Vec3(x - rhs.x, y - rhs.y, z - rhs.z);
	}

	void operator +=(const Vec3 rhs)
	{
		float x = this->x;
		float y = this->y;
		float z = this->z;
		x += rhs.x;
		y += rhs.y;
		z += rhs.z;
		this->x = x;
		this->y = y;
		this->z = z;
	}

	void operator -=(const Vec3 rhs)
	{
		float x = this->x;
		float y = this->y;
		float z = this->z;
		x -= rhs.x;
		y -= rhs.y;
		z -= rhs.z;
		this->x = x;
		this->y = y;
		this->z = z;
	}

	Vec3 operator *(float s)
	{
		return Vec3(x * s, y * s, z * s);
	}

	void operator *=(float rhs)
	{
		float x = this->x;
		float y = this->y;
		float z = this->z;
		x *= rhs;
		y *= rhs;
		z *= rhs;
		this->x = x;
		this->y = y;
		this->z = z;
	}

	Vec3 normalized() const
	{
		float x = this->x;
		float y = this->y;
		float z = this->z;
		float inv_len = 1 / sqrt(x * x + y * y + z * z);
		x *= inv_len;
		y *= inv_len;
		z *= inv_len;
		return Vec3(x, y, z);
	}

	void normalize()
	{
		float x = this->x;
		float y = this->y;
		float z = this->z;
		float inv_len = 1 / sqrt(x * x + y * y + z * z);
		x *= inv_len;
		y *= inv_len;
		z *= inv_len;
		this->x = x;
		this->y = y;
		this->z = z;
	}

	void set(float x, float y, float z)
	{
		this->x = x;
		this->y = y;
		this->z = z;
	}
	
	inline float length() const
	{
		float x = this->x;
		float y = this->y;
		float z = this->z;
		return sqrt(x * x + y * y + z * z);
	}

	inline float squaredLength() const
	{
		float x = this->x;
		float y = this->y;
		float z = this->z;
		return x * x + y * y + z * z;
	}

	inline void rotateY(float angle)
	{
		x = x * cos(angle) + sin(angle) * z;
		z = x * - sin(angle) + cos(angle) * z;
	}

	inline void rotateX(float angle)
	{
		y = y * cos(angle) - sin(angle) * z;
		z = y * sin(angle) + cos(angle) * z;
	}


	float x, y, z;
};


inline Vec3 operator *(const Vec3& v, float s)
{
	return Vec3(v.x * s, v.y * s, v.z* s);
}


inline float dotProduct(const Vec3& op1, const Vec3& op2)
{
	return op1.x * op2.x + op1.y * op2.y + op1.z * op2.z;
}


inline Vec3 crossProduct(const Vec3& op1, const Vec3& op2)
{
	float x1 = op1.x;
	float x2 = op2.x;
	return Vec3(
		op1.y * op2.z - op1.z * op2.y,
		op1.z * x2 - x1 * op2.z,
		x1 * op2.y - op1.y * x2
	);
}

inline void lerp(const Vec3& op1, const Vec3& op2, Vec3* out, float t)
{
	float invt = 1.0f - t;
	out->x = op1.x * invt + op2.x * t;
	out->y = op1.y * invt + op2.y * t;
	out->z = op1.z * invt + op2.z * t;
}


} // !namespace Lux