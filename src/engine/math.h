#pragma once


#include "engine/lumix.h"


namespace Lumix
{


struct Vec2;
struct DVec3;
struct Matrix;


struct LUMIX_ENGINE_API IVec2
{
	IVec2() {}
	explicit IVec2(int i) : x(i), y(i) {}
	explicit inline IVec2(const Vec2& rhs);
	IVec2(int x, int y) : x(x), y(y) {}
	IVec2 operator +(const IVec2& v) const { return IVec2(x + v.x, y + v.y); }
	IVec2 operator -(const IVec2& v) const { return IVec2(x - v.x, y - v.y); }
	IVec2 operator *(int i) const { return IVec2(x * i, y * i); }
	IVec2 operator / (int rhs) { return IVec2(x / rhs, y / rhs); }
	inline Vec2 operator / (const Vec2& rhs);
	bool operator !=(const IVec2& rhs) const { return rhs.x != x || rhs.y != y; }

	int x;
	int y;
};


struct LUMIX_ENGINE_API IVec3
{
	IVec3() {}
	IVec3(int x, int y, int z) : x(x), y(y), z(z) {}
	explicit inline IVec3(const DVec3& rhs);
	bool operator==(const IVec3& rhs) const
	{
		return x == rhs.x && y == rhs.y && z == rhs.z;
	}
	inline DVec3 operator *(double i) const;

	int x;
	int y;
    int z;
};


struct LUMIX_ENGINE_API IVec4
{
	IVec4() {}
	IVec4(const IVec2& a, const IVec2& b) : x(a.x), y(a.y), z(b.x), w(b.y) {}
	int x, y, z, w;
};


struct LUMIX_ENGINE_API Vec2
{
	Vec2() {}
	
	explicit Vec2(const IVec2& rhs)
		: x(float(rhs.x))
		, y(float(rhs.y))
	{
	}

	explicit Vec2(float a)
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
	Vec2 operator /(const Vec2& v) const { return Vec2(x / v.x, y / v.y); }
	Vec2 operator *(float f) const { return Vec2(x * f, y * f); }
	Vec2 operator /(float f) const { return Vec2(x / f, y / f); }
	Vec2 operator +(const Vec2& v) const { return Vec2(x + v.x, y + v.y); }
	void operator +=(const Vec2& v) { x += v.x; y += v.y; }
	Vec2 operator -(const Vec2& v) const { return Vec2(x - v.x, y - v.y); }
	Vec2 operator -(float f) const { return Vec2(x - f, y - f); }
	Vec2 operator -() const { return Vec2(-x, -y); }

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

	explicit Vec3(float a)
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
	Vec3 operator*(const Vec3& rhs) const { return Vec3(x * rhs.x, y * rhs.y, z * rhs.z); }
	Vec3 operator*(const IVec3& rhs) const { return Vec3(x * rhs.x, y * rhs.y, z * rhs.z); }
	Vec3 operator/(const IVec3& rhs) const { return Vec3(x / rhs.x, y / rhs.y, z / rhs.z); }

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

	float squaredLength() const { return x * x + y * y + z * z; }
	
	Vec2 xz() const { return {x, z}; }

	union
	{
		struct { float x, y, z; };
		struct { float r, g, b; };
	};

	static const Vec3 MAX;
	static const Vec3 MIN;
	static const Vec3 ZERO;
};


struct LUMIX_ENGINE_API DVec3
{
	DVec3() {}
	DVec3(float a) : x(a), y(a), z(a) {}
	DVec3(double x, double y, double z) : x(x), y(y), z(z) {}
	DVec3(const DVec3& rhs) = default;
	explicit DVec3(const Vec3& rhs) : x(rhs.x), y(rhs.y), z(rhs.z) {}

	DVec3 operator-() const { return {-x, -y, -z}; }
	DVec3 operator*(float rhs) const { return {x * rhs, y * rhs, z * rhs}; }
	DVec3 operator/(float rhs) const { return {x / rhs, y / rhs, z / rhs}; }
	DVec3 operator/(const DVec3& rhs) const { return {x / rhs.x, y / rhs.y, z / rhs.z}; }
	DVec3 operator-(const DVec3& rhs) const { return {x - rhs.x, y - rhs.y, z - rhs.z }; }
	DVec3 operator+(const DVec3& rhs) const { return {x + rhs.x, y + rhs.y, z + rhs.z }; }
	DVec3 operator-(const Vec3& rhs) const { return {x - rhs.x, y - rhs.y, z - rhs.z }; }
	DVec3 operator+(const Vec3& rhs) const { return {x + rhs.x, y + rhs.y, z + rhs.z }; }
	double length() const;
	double squaredLength() const { return x * x + y * y + z * z; }
	void operator*=(const double& rhs) { x *= rhs; y *= rhs; z *= rhs; }
	void operator+=(const Vec3& rhs) { x += rhs.x; y += rhs.y; z += rhs.z; }
	void operator-=(const Vec3& rhs) { x -= rhs.x; y -= rhs.y; z -= rhs.z; }
	Vec3 toFloat() const { return {(float)x, (float)y, (float)z}; }
	void fromFloat(const Vec3& v) { x = v.x; y = v.y; z = v.z; }

	double x, y, z;
};


IVec2::IVec2(const Vec2& rhs)
	: x(int(rhs.x))
	, y(int(rhs.y))
{
}


Vec2 IVec2::operator/(const Vec2& rhs) { return {x / rhs.x, y / rhs.y}; }


IVec3::IVec3(const DVec3& rhs)
	: x(int(rhs.x))
	, y(int(rhs.y))
	, z(int(rhs.z))
{
}


DVec3 IVec3::operator *(double i) const
{
    return {i * x, i * y, i * z};
}


inline Vec3 operator *(float f, const Vec3& v)
{
	return Vec3(f * v.x, f * v.y, f * v.z);
}


struct LUMIX_ENGINE_API Vec4
{
	Vec4() {}

	explicit Vec4(float a)
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

	Vec2 xz() const { return Vec2(x, z); }
	Vec2 xy() const { return Vec2(x, y); }
	Vec3 xyz() const { return Vec3(x, y, z); }
	Vec2 yz() const { return Vec2(y, z); }
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

	union
	{
		struct { float x, y, z, w; };
		struct { float r, g, b, a; };
	};

	static const Vec4 MAX;
	static const Vec4 MIN;
	static const Vec4 ZERO;
};


inline Vec4 operator *(float f, const Vec4& v)
{
	return Vec4(f * v.x, f * v.y, f * v.z, f * v.w);
}


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


inline DVec3 crossProduct(const DVec3& op1, const DVec3& op2)
{
	return DVec3(op1.y * op2.z - op1.z * op2.y, op1.z * op2.x - op1.x * op2.z, op1.x * op2.y - op1.y * op2.x);
}


inline Vec3 lerp(const Vec3& op1, const Vec3& op2, float t)
{
	Vec3 res;
	const float invt = 1.0f - t;
	res.x = op1.x * invt + op2.x * t;
	res.y = op1.y * invt + op2.y * t;
	res.z = op1.z * invt + op2.z * t;
	return res;
}


inline DVec3 lerp(const DVec3& op1, const DVec3& op2, float t)
{
	DVec3 res;
	const float invt = 1.0f - t;
	res.x = op1.x * invt + op2.x * t;
	res.y = op1.y * invt + op2.y * t;
	res.z = op1.z * invt + op2.z * t;
	return res;
}


inline Vec2 lerp(const Vec2& op1, const Vec2& op2, float t)
{
	Vec2 res;
	const float invt = 1.0f - t;
	res.x = op1.x * invt + op2.x * t;
	res.y = op1.y * invt + op2.y * t;
	return res;
}


struct LUMIX_ENGINE_API Quat
{
	Quat() {}
	Quat(const Vec3& axis, float angle);
	Quat(float _x, float _y, float _z, float _w) { x = _x; y = _y; z = _z; w = _w; } 

	void fromEuler(const Vec3& euler);
	Vec3 toEuler() const;
	void set(float _x, float _y, float _z, float _w) { x = _x; y = _y; z = _z; w = _w; } 
	void conjugate();
	Quat conjugated() const;
	void normalize();
	Quat normalized() const;
	Matrix toMatrix() const;

	Vec3 rotate(const Vec3& v) const
	{
		// nVidia SDK implementation

		Vec3 qvec(x, y, z);
		Vec3 uv = crossProduct(qvec, v);
		Vec3 uuv = crossProduct(qvec, uv);
		uv *= (2.0f * w);
		uuv *= 2.0f;

		return v + uv + uuv;
	}
	
	DVec3 rotate(const DVec3& v) const
	{
		// nVidia SDK implementation

		const DVec3 qvec(x, y, z);
		DVec3 uv = crossProduct(qvec, v);
		DVec3 uuv = crossProduct(qvec, uv);
		uv *= (2.0 * w);
		uuv *= 2.0;

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


LUMIX_ENGINE_API Vec3 slerp(const Vec3& a, const Vec3& b, float t);
LUMIX_ENGINE_API Quat nlerp(const Quat& q1, const Quat& q2, float t);


struct LUMIX_ENGINE_API LocalRigidTransform {
	Vec3 pos;
	Quat rot;

	LocalRigidTransform inverted() const
	{
		LocalRigidTransform result;
		result.rot = rot.conjugated();
		result.pos = result.rot.rotate(-pos);
		return result;
	}

	LocalRigidTransform operator*(const LocalRigidTransform& rhs) const;
	Matrix toMatrix() const;
	LocalRigidTransform interpolate(const LocalRigidTransform& rhs, float t) const;
};


struct LUMIX_ENGINE_API RigidTransform
{
	RigidTransform() {}


	RigidTransform(const DVec3& pos, const Quat& rot)
		: pos(pos)
		, rot(rot)
	{
	}


	RigidTransform inverted() const
	{
		RigidTransform result;
		result.rot = rot.conjugated();
		result.pos = result.rot.rotate(-pos);
		return result;
	}


	RigidTransform operator*(const RigidTransform& rhs) const
	{
		return{ rot.rotate(rhs.pos) + pos, rot * rhs.rot };
	}


	RigidTransform operator*(const LocalRigidTransform& rhs) const
	{
		return{ DVec3(rot.rotate(rhs.pos)) + pos, rot * rhs.rot };
	}

	Quat rot;
	DVec3 pos;
};


struct LUMIX_ENGINE_API Transform
{
	Transform() {}


	Transform(const DVec3& _pos, const Quat& _rot, float _scale)
		: pos(_pos)
		, rot(_rot)
		, scale(_scale)
	{
	}

	
	Transform inverted() const
	{
		Transform result;
		result.rot = rot.conjugated();
		result.pos = result.rot.rotate(-pos / scale);
		result.scale = 1.0f / scale;
		return result;
	}


	Transform operator*(const Transform& rhs) const
	{
		return {rot.rotate(rhs.pos * scale) + pos, rot * rhs.rot, scale * rhs.scale};
	}

	Transform operator*(const LocalRigidTransform& rhs) const
	{
		return {pos + rot.rotate(rhs.pos * scale), rot * rhs.rot, scale};
	}

	DVec3 transform(const Vec3& value) const
	{
		return pos + rot.rotate(value) * scale;
	}


	DVec3 transform(const DVec3& value) const
	{
		return pos + rot.rotate(value) * scale;
	}


	RigidTransform getRigidPart() const
	{
		return {pos, rot};
	}


	DVec3 pos;
	Quat rot;
	float scale;

	static const Transform IDENTITY;
};


struct alignas(16) LUMIX_ENGINE_API Matrix
{
	static Matrix rotationX(float angle);
	static Matrix rotationY(float angle);
	static Matrix rotationZ(float angle);

	Matrix() {}

	Matrix(const Vec3& x, const Vec3& y, const Vec3& z)
	{
		setXVector(x);
		setYVector(y);
		setZVector(z);
	}

	Matrix(const Vec3& pos, const Quat& rot);

	explicit Matrix(const float* m)
	{
		m11 = m[0]; m12 = m[1]; m13 = m[2]; m14 = m[3];
		m21 = m[4]; m22 = m[5]; m23 = m[6]; m24 = m[7];
		m31 = m[8]; m32 = m[9]; m33 = m[10]; m34 = m[11];
		m41 = m[12]; m42 = m[13]; m43 = m[14]; m44 = m[15];
	}

	Matrix(
		float r11, float r12, float r13, float r14,
		float r21, float r22, float r23, float r24,
		float r31, float r32, float r33, float r34,
		float r41, float r42, float r43, float r44
	)
	{
		m11 = r11; m12 = r12; m13 = r13; m14 = r14;
		m21 = r21; m22 = r22; m23 = r23; m24 = r24;
		m31 = r31; m32 = r32; m33 = r33; m34 = r34;
		m41 = r41; m42 = r42; m43 = r43; m44 = r44;
	}

	void decompose(Vec3& position, Quat& rotation, float& scale) const;

	float operator [](int index) const { return (&m11)[index]; }
	float& operator [](int index) { return (&m11)[index]; }

	Matrix operator *(const Matrix& rhs) const;

	Matrix operator +(const Matrix& rhs) const;

	Matrix operator *(float rhs) const;

	Vec4 operator *(const Vec4& rhs) const
	{
		return Vec4(
			m11 * rhs.x + m21 * rhs.y + m31 * rhs.z + m41 * rhs.w,	
			m12 * rhs.x + m22 * rhs.y + m32 * rhs.z + m42 * rhs.w,	
			m13 * rhs.x + m23 * rhs.y + m33 * rhs.z + m43 * rhs.w,
			m14 * rhs.x + m24 * rhs.y + m34 * rhs.z + m44 * rhs.w
		);
	}

	Vec3 getZVector() const
	{
		return Vec3(m31, m32, m33);
	}

	Vec3 getYVector() const
	{
		return Vec3(m21, m22, m23);
	}

	Vec3 getXVector() const
	{
		return Vec3(m11, m12, m13);
	}

	void setXVector(const Vec3& v)
	{
		m11 = v.x; m12 = v.y; m13 = v.z;
	}

	void setYVector(const Vec3& v)
	{
		m21 = v.x; m22 = v.y; m23 = v.z;
	}

	void setZVector(const Vec3& v)
	{
		m31 = v.x; m32 = v.y; m33 = v.z;
	}


	float determinant() const
	{
		return
			m14 * m23 * m32 * m41  -  m13 * m24 * m32 * m41  -  m14 * m22 * m33 * m41  +  m12 * m24 * m33 * m41 +
			m13 * m22 * m34 * m41  -  m12 * m23 * m34 * m41  -  m14 * m23 * m31 * m42  +  m13 * m24 * m31 * m42 +
			m14 * m21 * m33 * m42  -  m11 * m24 * m33 * m42  -  m13 * m21 * m34 * m42  +  m11 * m23 * m34 * m42 +
			m14 * m22 * m31 * m43  -  m12 * m24 * m31 * m43  -  m14 * m21 * m32 * m43  +  m11 * m24 * m32 * m43 +
			m12 * m21 * m34 * m43  -  m11 * m22 * m34 * m43  -  m13 * m22 * m31 * m44  +  m12 * m23 * m31 * m44 +
			m13 * m21 * m32 * m44  -  m11 * m23 * m32 * m44  -  m12 * m21 * m33 * m44  +  m11 * m22 * m33 * m44;
	}


	Matrix inverted() const
	{
		Matrix tmp = *this;
		tmp.inverse();
		return tmp;
	}

	void inverse()
	{
		Matrix mtx;
		float d = determinant();
		if( d == 0 ) return;
		d = 1.0f / d;

		mtx.m11 = d * (m23 * m34 * m42  -  m24 * m33 * m42  +  m24 * m32 * m43  -  m22 * m34 * m43  -  m23 * m32 * m44  +  m22 * m33 * m44);
		mtx.m12 = d * (m14 * m33 * m42  -  m13 * m34 * m42  -  m14 * m32 * m43  +  m12 * m34 * m43  +  m13 * m32 * m44  -  m12 * m33 * m44);
		mtx.m13 = d * (m13 * m24 * m42  -  m14 * m23 * m42  +  m14 * m22 * m43  -  m12 * m24 * m43  -  m13 * m22 * m44  +  m12 * m23 * m44);
		mtx.m14 = d * (m14 * m23 * m32  -  m13 * m24 * m32  -  m14 * m22 * m33  +  m12 * m24 * m33  +  m13 * m22 * m34  -  m12 * m23 * m34);
		mtx.m21 = d * (m24 * m33 * m41  -  m23 * m34 * m41  -  m24 * m31 * m43  +  m21 * m34 * m43  +  m23 * m31 * m44  -  m21 * m33 * m44);
		mtx.m22 = d * (m13 * m34 * m41  -  m14 * m33 * m41  +  m14 * m31 * m43  -  m11 * m34 * m43  -  m13 * m31 * m44  +  m11 * m33 * m44);
		mtx.m23 = d * (m14 * m23 * m41  -  m13 * m24 * m41  -  m14 * m21 * m43  +  m11 * m24 * m43  +  m13 * m21 * m44  -  m11 * m23 * m44);
		mtx.m24 = d * (m13 * m24 * m31  -  m14 * m23 * m31  +  m14 * m21 * m33  -  m11 * m24 * m33  -  m13 * m21 * m34  +  m11 * m23 * m34);
		mtx.m31 = d * (m22 * m34 * m41  -  m24 * m32 * m41  +  m24 * m31 * m42  -  m21 * m34 * m42  -  m22 * m31 * m44  +  m21 * m32 * m44);
		mtx.m32 = d * (m14 * m32 * m41  -  m12 * m34 * m41  -  m14 * m31 * m42  +  m11 * m34 * m42  +  m12 * m31 * m44  -  m11 * m32 * m44);
		mtx.m33 = d * (m12 * m24 * m41  -  m14 * m22 * m41  +  m14 * m21 * m42  -  m11 * m24 * m42  -  m12 * m21 * m44  +  m11 * m22 * m44);
		mtx.m34 = d * (m14 * m22 * m31  -  m12 * m24 * m31  -  m14 * m21 * m32  +  m11 * m24 * m32  +  m12 * m21 * m34  -  m11 * m22 * m34);
		mtx.m41 = d * (m23 * m32 * m41  -  m22 * m33 * m41  -  m23 * m31 * m42  +  m21 * m33 * m42  +  m22 * m31 * m43  -  m21 * m32 * m43);
		mtx.m42 = d * (m12 * m33 * m41  -  m13 * m32 * m41  +  m13 * m31 * m42  -  m11 * m33 * m42  -  m12 * m31 * m43  +  m11 * m32 * m43);
		mtx.m43 = d * (m13 * m22 * m41  -  m12 * m23 * m41  -  m13 * m21 * m42  +  m11 * m23 * m42  +  m12 * m21 * m43  -  m11 * m22 * m43);
		mtx.m44 = d * (m12 * m23 * m31  -  m13 * m22 * m31  +  m13 * m21 * m32  -  m11 * m23 * m32  -  m12 * m21 * m33  +  m11 * m22*m33);

		*this = mtx;
	}

	// orthonormal
	void fastInverse()
	{
		float tmp = m21;
		m21 = m12;
		m12 = tmp;

		tmp = m32;
		m32 = m23;
		m23 = tmp;

		tmp = m31;
		m31 = m13;
		m13 = tmp;

		float m41 = -this->m41;
		float m42 = -this->m42;
		float m43 = -this->m43;
		this->m41 = m41 * m11 + m42 * m21 + m43 * m31;
		this->m42 = m41 * m12 + m42 * m22 + m43 * m32;
		this->m43 = m41 * m13 + m42 * m23 + m43 * m33;
	}
	
	// orthonormal
	Matrix fastInverted() const
	{
		Matrix ret;
		ret.m11 = m11;
		ret.m22 = m22;
		ret.m33 = m33;

		ret.m21 = m12;
		ret.m12 = m12;

		ret.m23 = m32;
		ret.m32 = m23;

		ret.m13 = m31;
		ret.m31 = m13;

		ret.m41 = -m41;
		ret.m42 = -m42;
		ret.m43 = -m43;
		ret.m41 = ret.m41 * ret.m11 + ret.m42 * ret.m21 + ret.m43 * ret.m31;
		ret.m42 = ret.m41 * ret.m12 + ret.m42 * ret.m22 + ret.m43 * ret.m32;
		ret.m43 = ret.m41 * ret.m13 + ret.m42 * ret.m23 + ret.m43 * ret.m33;

		ret.m14 = 0;
		ret.m24 = 0;
		ret.m34 = 0;
		ret.m44 = 1;

		return ret;
	}
	void copy3x3(const Matrix& mtx)
	{
		m11 = mtx.m11;
		m12 = mtx.m12;
		m13 = mtx.m13;

		m21 = mtx.m21;
		m22 = mtx.m22;
		m23 = mtx.m23;

		m31 = mtx.m31;
		m32 = mtx.m32;
		m33 = mtx.m33;
	}

	void translate(const Vec3& t)
	{
		m41 += t.x;
		m42 += t.y;
		m43 += t.z;
	}

	void translate(float x, float y, float z)
	{
		m41 += x;
		m42 += y;
		m43 += z;
	}

	void setTranslation(const Vec3& t)
	{
		m41 = t.x;
		m42 = t.y;
		m43 = t.z;
	}


	void setOrtho(float left, float right, float bottom, float top, float z_near, float z_far, bool reversed_z);


	void setPerspective(float fov, float ratio, float near_plane, float far_plane, bool invert_z);


	void fromEuler(float yaw, float pitch, float roll);


	void lookAt(const Vec3& eye, const Vec3& at, const Vec3& up)
	{
		*this = Matrix::IDENTITY;
		Vec3 f = eye - at;
		f.normalize();
		Vec3 r = crossProduct(up, f);
		r.normalize();
		Vec3 u = crossProduct(f, r);
		setXVector(r);
		setYVector(u);
		setZVector(f);
		transpose();
		setTranslation(Vec3(-dotProduct(r, eye), -dotProduct(u, eye), -dotProduct(f, eye)));
	}


	void normalizeScale();


	void getTranslation(Vec3& pos) const
	{
		pos.set(m41, m42, m43);
	}

	Vec3 getTranslation() const
	{
		return Vec3(m41, m42, m43);
	}

	Quat getRotation() const;
	void transpose();
	Vec3 transformPoint(const Vec3& pos) const;
	Vec3 transformVector(const Vec3& pos) const;
	void multiply3x3(float scale);
	void setIdentity();

	float m11, m12, m13, m14;
	float m21, m22, m23, m24;
	float m31, m32, m33, m34;
	float m41, m42, m43, m44;

	static const Matrix IDENTITY;
};

constexpr float PI = 3.14159265f;
constexpr float HALF_PI = 3.14159265f * 0.5f;
constexpr float SQRT2 = 1.41421356237f;
constexpr float SQRT3 = 1.73205080757f;

LUMIX_ENGINE_API bool getRayPlaneIntersecion(const Vec3& origin,
	const Vec3& dir,
	const Vec3& plane_point,
	const Vec3& normal,
	float& out);


LUMIX_ENGINE_API bool getRaySphereIntersection(const Vec3& origin,
	const Vec3& dir,
	const Vec3& center,
	float radius,
	Ref<float> out);


LUMIX_ENGINE_API bool getRayAABBIntersection(const Vec3& origin,
	const Vec3& dir,
	const Vec3& min,
	const Vec3& size,
	Vec3& out);


LUMIX_ENGINE_API float getLineSegmentDistance(const Vec3& origin,
	const Vec3& dir,
	const Vec3& a,
	const Vec3& b);


LUMIX_ENGINE_API bool getRayTriangleIntersection(const Vec3& origin,
	const Vec3& dir,
	const Vec3& a,
	const Vec3& b,
	const Vec3& c,
	float* out_t);


LUMIX_ENGINE_API bool getSphereTriangleIntersection(const Vec3& center,
	float radius,
	const Vec3& v0,
	const Vec3& v1,
	const Vec3& v2);


template <typename T> LUMIX_FORCE_INLINE void swap(T& a, T& b)
{
	T tmp = a;
	a = b;
	b = tmp;
}


template <typename T> LUMIX_FORCE_INLINE T minimum(T a)
{
	return a;
}


template <typename T1, typename... T2> LUMIX_FORCE_INLINE T1 minimum(T1 a, T2... b)
{
	T1 min_b = minimum(b...);
	return a < min_b ? a : min_b;
}


template <typename T> LUMIX_FORCE_INLINE T maximum(T a)
{
	return a;
}


template <typename T1, typename... T2> LUMIX_FORCE_INLINE T1 maximum(T1 a, T2... b)
{
	T1 min_b = maximum(b...);
	return a > min_b ? a : min_b;
}


// converts float to u32 so it can be used in radix sort
// float float_value = 0;
// u32 sort_key = floatFlip(*(u32*)&float_value);
// http://stereopsis.com/radix.html
LUMIX_FORCE_INLINE u32 floatFlip(u32 float_bits_value)
{
	u32 mask = -i32(float_bits_value >> 31) | 0x80000000;
	return float_bits_value ^ mask;
}


template <typename T> LUMIX_FORCE_INLINE T signum(T a)
{
	return a > 0 ? (T)1 : (a < 0 ? (T)-1 : 0);
}


template <typename T1, typename T2, typename T3> LUMIX_FORCE_INLINE T1 clamp(T1 value, T2 min_value, T3 max_value)
{
	return minimum(maximum(value, min_value), max_value);
}


inline u32 nextPow2(u32 v)
{
	v--;
	v |= v >> 1;
	v |= v >> 2;
	v |= v >> 4;
	v |= v >> 8;
	v |= v >> 16;
	v++;
	return v;
}


inline u32 log2(u32 v)
{
	u32 r = (v > 0xffff) << 4; v >>= r;
	u32 shift = (v > 0xff) << 3; v >>= shift; r |= shift;
	shift = (v > 0xf) << 2; v >>= shift; r |= shift;
	shift = (v > 0x3) << 1; v >>= shift; r |= shift;
	r |= (v >> 1);
	return r;
}


template <typename T> bool isPowOfTwo(T n)
{
	return (n) && !(n & (n - 1));
}


LUMIX_FORCE_INLINE float degreesToRadians(float angle)
{
	return angle * PI / 180.0f;
}


LUMIX_FORCE_INLINE double degreesToRadians(double angle)
{
	return angle * PI / 180.0;
}


LUMIX_FORCE_INLINE float degreesToRadians(int angle)
{
	return angle * PI / 180.0f;
}


LUMIX_FORCE_INLINE float radiansToDegrees(float angle)
{
	return angle / PI * 180.0f;
}


LUMIX_ENGINE_API Vec2 radiansToDegrees(const Vec2& v);
LUMIX_ENGINE_API Vec3 radiansToDegrees(const Vec3& v);
LUMIX_ENGINE_API Vec2 degreesToRadians(const Vec2& v);
LUMIX_ENGINE_API Vec3 degreesToRadians(const Vec3& v);

inline float easeInOut(float t)
{
	float scaled_t = t * 2;
	if (scaled_t < 1)
	{
		return 0.5f * scaled_t * scaled_t;
	}
	--scaled_t;
	return -0.5f * (scaled_t * (scaled_t - 2) - 1);
}


LUMIX_ENGINE_API float angleDiff(float a, float b);
LUMIX_ENGINE_API u64 randGUID();
LUMIX_ENGINE_API u32 rand();
LUMIX_ENGINE_API u32 rand(u32 from, u32 to);
LUMIX_ENGINE_API void seedRandom(u32 seed);
LUMIX_ENGINE_API float randFloat();
LUMIX_ENGINE_API float randFloat(float from, float to);


} // namespace Lumix
