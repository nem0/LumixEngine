#pragma once


#include "lumix.h"


namespace Lumix
{


struct Int2
{
	int x;
	int y;
};


struct LUMIX_ENGINE_API Vec2
{
	Vec2() {}
	Vec2(float a, float b)
		: x(a)
		, y(b)
	{
	}

	void set(float a, float b)
	{
		x = a;
		y = b;
	}


	Vec2 operator*(float f) const { return Vec2(x * f, y * f); }
	Vec2 operator+(const Vec2& v) const { return Vec2(x * v.x, y * v.y); }
	Vec2 normalized() const { return *this * (1 / length()); }
	float length() const;

	float x, y;
};


struct LUMIX_ENGINE_API Vec3
{
	Vec3() {}

	Vec3(float a, float b, float c)
		: x(a)
		, y(b)
		, z(c)
	{
	}

	Vec3 operator+(const Vec3& rhs) const { return Vec3(x + rhs.x, y + rhs.y, z + rhs.z); }

	Vec3 operator-() const { return Vec3(-x, -y, -z); }

	Vec3 operator-(const Vec3& rhs) const { return Vec3(x - rhs.x, y - rhs.y, z - rhs.z); }

	void operator+=(const Vec3& rhs)
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

	void operator-=(const Vec3& rhs)
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

	Vec3 operator*(float s) const { return Vec3(x * s, y * s, z * s); }

	Vec3 operator/(float s) const
	{
		float tmp = 1 / s;
		return Vec3(x * tmp, y * tmp, z * tmp);
	}

	void operator*=(float rhs)
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

	Vec3 normalized() const;

	void normalize();

	void set(float x, float y, float z)
	{
		this->x = x;
		this->y = y;
		this->z = z;
	}

	float length() const;

	float squaredLength() const
	{
		float x = this->x;
		float y = this->y;
		float z = this->z;
		return x * x + y * y + z * z;
	}

	void rotateY(float angle);

	void rotateX(float angle);


	float x, y, z;
};


struct LUMIX_ENGINE_API Vec4
{
	Vec4() {}
	Vec4(float a, float b, float c, float d)
		: x(a)
		, y(b)
		, z(c)
		, w(d)
	{
	}
	Vec4(const Vec3& v, float d)
		: x(v.x)
		, y(v.y)
		, z(v.z)
		, w(d)
	{
	}

	Vec3 xyz() const { return Vec3(x, y, z); }

	Vec4 operator+(const Vec4& rhs) const
	{
		return Vec4(x + rhs.x, y + rhs.y, z + rhs.z, w + rhs.w);
	}

	Vec4 operator-() const { return Vec4(-x, -y, -z, -w); }

	Vec4 operator-(const Vec4& rhs) const
	{
		return Vec4(x - rhs.x, y - rhs.y, z - rhs.z, w - rhs.w);
	}

	void operator+=(const Vec4& rhs)
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

	void operator-=(const Vec4& rhs)
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

	Vec4 operator*(float s) { return Vec4(x * s, y * s, z * s, w * s); }

	void operator*=(float rhs)
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

	void normalize();

	void set(float x, float y, float z, float w)
	{
		this->x = x;
		this->y = y;
		this->z = z;
		this->w = w;
	}

	void set(const Vec4& rhs)
	{
		x = rhs.x;
		y = rhs.y;
		z = rhs.z;
		w = rhs.w;
	}

	float length() const;

	float squaredLength() const
	{
		float x = this->x;
		float y = this->y;
		float z = this->z;
		float w = this->w;
		return x * x + y * y + z * z + w * w;
	}

	operator Vec3() { return Vec3(x, y, z); }

	float x, y, z, w;
};


inline Vec4 operator*(const Vec4& v, float s)
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


inline float dotProduct(const Vec3& op1, const Vec3& op2)
{
	return op1.x * op2.x + op1.y * op2.y + op1.z * op2.z;
}


inline Vec3 crossProduct(const Vec3& op1, const Vec3& op2)
{
	float x1 = op1.x;
	float x2 = op2.x;
	return Vec3(op1.y * op2.z - op1.z * op2.y, op1.z * x2 - x1 * op2.z, x1 * op2.y - op1.y * x2);
}


inline void lerp(const Vec3& op1, const Vec3& op2, Vec3* out, float t)
{
	float invt = 1.0f - t;
	out->x = op1.x * invt + op2.x * t;
	out->y = op1.y * invt + op2.y * t;
	out->z = op1.z * invt + op2.z * t;
}


} // !namespace Lumix
