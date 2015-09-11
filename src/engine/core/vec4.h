#pragma once


#include "lumix.h"
#include <cmath>
#include "core/vec3.h"


namespace Lumix
{


struct LUMIX_ENGINE_API Vec4
{
	Vec4() {}
	Vec4(float a, float b, float c, float d) : x(a), y(b), z(c), w(d) {}
	Vec4(const Vec3& v, float d) : x(v.x), y(v.y), z(v.z), w(d) {}

	Vec3 xyz() const { return Vec3(x, y, z); }

	Vec4 operator +(const Vec4& rhs) const
	{
		return Vec4(x + rhs.x, y + rhs.y, z + rhs.z, w + rhs.w );
	}

	Vec4 operator-() const
	{
		return Vec4(-x, -y, -z, -w);
	}

	Vec4 operator -(const Vec4& rhs) const
	{
		return Vec4(x - rhs.x, y - rhs.y, z - rhs.z, w - rhs.w);
	}

	void operator +=(const Vec4 rhs)
	{
		float x = this->x;
		float y = this->y;
		float z = this->z;
		float w = this->w;
		x += rhs.x;
		y += rhs.y;
		z += rhs.z;
		w += rhs.w;
		this->x = x;
		this->y = y;
		this->z = z;
		this->w = w;
	}

	void operator -=(const Vec4 rhs)
	{
		float x = this->x;
		float y = this->y;
		float z = this->z;
		float w = this->w;
		x -= rhs.x;
		y -= rhs.y;
		z -= rhs.z;
		w -= rhs.w;
		this->x = x;
		this->y = y;
		this->z = z;
		this->w = w;
	}

	Vec4 operator *(float s)
	{
		return Vec4(x * s, y * s, z * s, w * s);
	}

	void operator *=(float rhs)
	{
		float x = this->x;
		float y = this->y;
		float z = this->z;
		float w = this->w;
		x *= rhs;
		y *= rhs;
		z *= rhs;
		this->x = x;
		this->y = y;
		this->z = z;
		this->w = w;
	}

	void normalize()
	{
		float x = this->x;
		float y = this->y;
		float z = this->z;
		float w = this->w;
		float inv_len = 1 / sqrt(x * x + y * y + z * z + w * w);
		x *= inv_len;
		y *= inv_len;
		z *= inv_len;
		this->x = x;
		this->y = y;
		this->z = z;
		this->w = w;
	}

	void set(float x, float y, float z, float w)
	{
		this->x = x;
		this->y = y;
		this->z = z;
		this->w = w;
	}

	inline void set(const Vec4& rhs)
	{
		x = rhs.x;
		y = rhs.y;
		z = rhs.z;
		w = rhs.w;
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
		float w = this->w;
		return x * x + y * y + z * z + w * w;  
	}

	operator Vec3()
	{
		return Vec3(x, y, z);
	}

	float x, y, z, w;
};


inline Vec4 operator *(const Vec4& v, float s)
{
	return Vec4(v.x * s, v.y * s, v.z * s, v.w * s);
}


inline float dotProduct(const Vec4& op1, const Vec4& op2)
{
	return op1.x * op2.x + op1.y * op2.y + op1.z * op2.z + op1.w * op2.w;
}

inline void lerp(const Vec4& op1, const Vec4& op2, Vec4* out, float t)
{
	float invt = 1.0f - t;
	out->x = op1.x * invt + op2.x * t;
	out->y = op1.y * invt + op2.y * t;
	out->z = op1.z * invt + op2.z * t;
	out->w = op1.w * invt + op2.w * t;
}


} // !namespace Lumix
