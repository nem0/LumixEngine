#pragma once


#include "engine/lumix.h"


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

	Vec2(float a)
		: x(a)
		, y(a)
	{
	}

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

	template<typename L>
	float& operator[](L i)
	{
		ASSERT(i >= 0 && i < 2);
		return (&x)[i];
	}

	template<typename L>
	float operator[](L i) const
	{
		ASSERT(i >= 0 && i < 2);
		return (&x)[i];
	}

	bool operator==(const Vec2& rhs) const
	{
		return x == rhs.x && y == rhs.y;
	}

	bool operator!=(const Vec2& rhs) const
	{
		return x != rhs.x || y != rhs.y;
	}

	void operator/=(float rhs)
	{
		*this *= 1.0f / rhs;
	}

	void operator*=(float f)
	{
		x *= f;
		y *= f;
	}

	Vec2 operator *(const Vec2& v) const { return Vec2(x * v.x, y * v.y); }
	Vec2 operator *(float f) const { return Vec2(x * f, y * f); }
	Vec2 operator /(float f) const { return Vec2(x / f, y / f); }
	Vec2 operator +(const Vec2& v) const { return Vec2(x + v.x, y + v.y); }
	void operator +=(const Vec2& v) { x += v.x; y += v.y; }
	Vec2 operator -(const Vec2& v) const { return Vec2(x - v.x, y - v.y); }
	Vec2 operator -(float f) const { return Vec2(x - f, y - f); }

	void normalize();
	Vec2 normalized() const;
	float length() const;
	float squaredLength() const;

	float x, y;

	static const Vec2 MAX;
	static const Vec2 MIN;
	static const Vec2 ZERO;
};


struct LUMIX_ENGINE_API Vec3
{
	Vec3() {}

	Vec3(const Vec2& v, float c)
		: x(v.x)
		, y(v.y)
		, z(c)
	{}

	Vec3(float a)
		: x(a)
		, y(a)
		, z(a)
	{
	}

	Vec3(float a, float b, float c)
		: x(a)
		, y(b)
		, z(c)
	{
	}

	template<typename L>
	float& operator[](L i)
	{
		ASSERT(i >= 0 && i < 3);
		return (&x)[i];
	}

	template<typename L>
	float operator[](L i) const
	{
		ASSERT(i >= 0 && i < 3);
		return (&x)[i];
	}

	bool operator==(const Vec3& rhs) const
	{
		return x == rhs.x && y == rhs.y && z == rhs.z;
	}

	bool operator!=(const Vec3& rhs) const
	{
		return x != rhs.x || y != rhs.y || z != rhs.z;
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
	
	void operator/=(float rhs)
	{
		*this *= 1.0f / rhs;
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
	
	union
	{
		struct { float x, y, z; };
		struct { float r, g, b; };
	};

	static const Vec3 MAX;
	static const Vec3 MIN;
	static const Vec3 ZERO;
};


inline Vec3 operator *(float f, const Vec3& v)
{
	return Vec3(f * v.x, f * v.y, f * v.z);
}


struct LUMIX_ENGINE_API Vec4
{
	Vec4() {}

	Vec4(float a)
		: x(a)
		, y(a)
		, z(a)
		, w(a)
	{
	}

	Vec4(float a, float b, float c, float d)
		: x(a)
		, y(b)
		, z(c)
		, w(d)
	{
	}

	Vec4(const Vec2& v1, const Vec2& v2)
		: x(v1.x)
		, y(v1.y)
		, z(v2.x)
		, w(v2.y)
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
	Vec3 rgb() const { return Vec3(x, y, z); }

	template<typename L>
	float& operator[](L i)
	{
		ASSERT(i >= 0 && i < 4);
		return (&x)[i];
	}

	template<typename L>
	float operator[](L i) const
	{
		ASSERT(i >= 0 && i < 4);
		return (&x)[i];
	}

	bool operator==(const Vec4& rhs) const
	{
		return x == rhs.x && y == rhs.y && z == rhs.z && w == rhs.w;
	}

	bool operator!=(const Vec4& rhs) const
	{
		return x != rhs.x || y != rhs.y || z != rhs.z || w != rhs.w;
	}

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

	void operator/=(float rhs)
	{
		*this *= 1.0f / rhs;
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
		w *= rhs;
		this->x = x;
		this->y = y;
		this->z = z;
		this->w = w;
	}

	void normalize();
	Vec4 normalized() const;

	void set(const Vec3& v, float w)
	{
		this->x = v.x;
		this->y = v.y;
		this->z = v.z;
		this->w = w;
	}

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

	union
	{
		struct { float x, y, z, w; };
		struct { float r, g, b, a; };
	};

	static const Vec4 MAX;
	static const Vec4 MIN;
	static const Vec4 ZERO;
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
	const float invt = 1.0f - t;
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
	return Vec3(op1.y * op2.z - op1.z * op2.y, op1.z * op2.x - op1.x * op2.z, op1.x * op2.y - op1.y * op2.x);
}


inline void lerp(const Vec3& op1, const Vec3& op2, Vec3* out, float t)
{
	const float invt = 1.0f - t;
	out->x = op1.x * invt + op2.x * t;
	out->y = op1.y * invt + op2.y * t;
	out->z = op1.z * invt + op2.z * t;
}


inline void lerp(const Vec2& op1, const Vec2& op2, Vec2* out, float t)
{
	const float invt = 1.0f - t;
	out->x = op1.x * invt + op2.x * t;
	out->y = op1.y * invt + op2.y * t;
}


} // !namespace Lumix
