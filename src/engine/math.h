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
	void operator/=(const double& rhs) { x /= rhs; y /= rhs; z /= rhs; }
	void operator+=(const DVec3& rhs) { x += rhs.x; y += rhs.y; z += rhs.z; }
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

	Matrix(const Vec4& col0, const Vec4& col1, const Vec4& col2, const Vec4& col3)
	{
		columns[0] = col0;	
		columns[1] = col1;	
		columns[2] = col2;	
		columns[3] = col3;	
	}

	Matrix(const Vec3& pos, const Quat& rot);

	void decompose(Vec3& position, Quat& rotation, float& scale) const;

	float operator [](int index) const { return (&columns[0].x)[index]; }
	float& operator [](int index) { return (&columns[0].x)[index]; }

	Matrix operator*(const Matrix& rhs) const;
	Matrix operator+(const Matrix& rhs) const;
	Matrix operator*(float rhs) const;
	Vec4 operator*(const Vec4& rhs) const { return columns[0] * rhs.x + columns[1] * rhs.y + columns[2] * rhs.z + columns[3] * rhs.w; }

	Vec3 getZVector() const { return columns[2].xyz(); }
	Vec3 getYVector() const { return columns[1].xyz(); }
	Vec3 getXVector() const { return columns[0].xyz(); }

	void setXVector(const Vec3& v)
	{
		columns[0].x = v.x;
		columns[0].y = v.y;
		columns[0].z = v.z;
	}

	void setYVector(const Vec3& v)
	{
		columns[1].x = v.x;
		columns[1].y = v.y;
		columns[1].z = v.z;
	}

	void setZVector(const Vec3& v)
	{
		columns[2].x = v.x;
		columns[2].y = v.y;
		columns[2].z = v.z;
	}


	float determinant() const
	{
		return
			columns[0].w * columns[1].z * columns[2].y * columns[3].x  -  columns[0].z * columns[1].w * columns[2].y * columns[3].x  -  columns[0].w * columns[1].y * columns[2].z * columns[3].x  +  columns[0].y * columns[1].w * columns[2].z * columns[3].x +
			columns[0].z * columns[1].y * columns[2].w * columns[3].x  -  columns[0].y * columns[1].z * columns[2].w * columns[3].x  -  columns[0].w * columns[1].z * columns[2].x * columns[3].y  +  columns[0].z * columns[1].w * columns[2].x * columns[3].y +
			columns[0].w * columns[1].x * columns[2].z * columns[3].y  -  columns[0].x * columns[1].w * columns[2].z * columns[3].y  -  columns[0].z * columns[1].x * columns[2].w * columns[3].y  +  columns[0].x * columns[1].z * columns[2].w * columns[3].y +
			columns[0].w * columns[1].y * columns[2].x * columns[3].z  -  columns[0].y * columns[1].w * columns[2].x * columns[3].z  -  columns[0].w * columns[1].x * columns[2].y * columns[3].z  +  columns[0].x * columns[1].w * columns[2].y * columns[3].z +
			columns[0].y * columns[1].x * columns[2].w * columns[3].z  -  columns[0].x * columns[1].y * columns[2].w * columns[3].z  -  columns[0].z * columns[1].y * columns[2].x * columns[3].w  +  columns[0].y * columns[1].z * columns[2].x * columns[3].w +
			columns[0].z * columns[1].x * columns[2].y * columns[3].w  -  columns[0].x * columns[1].z * columns[2].y * columns[3].w  -  columns[0].y * columns[1].x * columns[2].z * columns[3].w  +  columns[0].x * columns[1].y * columns[2].z * columns[3].w;
	}


	Matrix inverted() const {
		Matrix mtx;
		float d = determinant();
		if (d == 0) return *this;
		d = 1.0f / d;

		mtx.columns[0] = Vec4(d * (columns[1].z * columns[2].w * columns[3].y  -  columns[1].w * columns[2].z * columns[3].y  +  columns[1].w * columns[2].y * columns[3].z  -  columns[1].y * columns[2].w * columns[3].z  -  columns[1].z * columns[2].y * columns[3].w  +  columns[1].y * columns[2].z * columns[3].w)
							, d * (columns[0].w * columns[2].z * columns[3].y  -  columns[0].z * columns[2].w * columns[3].y  -  columns[0].w * columns[2].y * columns[3].z  +  columns[0].y * columns[2].w * columns[3].z  +  columns[0].z * columns[2].y * columns[3].w  -  columns[0].y * columns[2].z * columns[3].w)
							, d * (columns[0].z * columns[1].w * columns[3].y  -  columns[0].w * columns[1].z * columns[3].y  +  columns[0].w * columns[1].y * columns[3].z  -  columns[0].y * columns[1].w * columns[3].z  -  columns[0].z * columns[1].y * columns[3].w  +  columns[0].y * columns[1].z * columns[3].w)
							, d * (columns[0].w * columns[1].z * columns[2].y  -  columns[0].z * columns[1].w * columns[2].y  -  columns[0].w * columns[1].y * columns[2].z  +  columns[0].y * columns[1].w * columns[2].z  +  columns[0].z * columns[1].y * columns[2].w  -  columns[0].y * columns[1].z * columns[2].w));
		mtx.columns[1] = Vec4(d * (columns[1].w * columns[2].z * columns[3].x  -  columns[1].z * columns[2].w * columns[3].x  -  columns[1].w * columns[2].x * columns[3].z  +  columns[1].x * columns[2].w * columns[3].z  +  columns[1].z * columns[2].x * columns[3].w  -  columns[1].x * columns[2].z * columns[3].w)
							, d * (columns[0].z * columns[2].w * columns[3].x  -  columns[0].w * columns[2].z * columns[3].x  +  columns[0].w * columns[2].x * columns[3].z  -  columns[0].x * columns[2].w * columns[3].z  -  columns[0].z * columns[2].x * columns[3].w  +  columns[0].x * columns[2].z * columns[3].w)
							, d * (columns[0].w * columns[1].z * columns[3].x  -  columns[0].z * columns[1].w * columns[3].x  -  columns[0].w * columns[1].x * columns[3].z  +  columns[0].x * columns[1].w * columns[3].z  +  columns[0].z * columns[1].x * columns[3].w  -  columns[0].x * columns[1].z * columns[3].w)
							, d * (columns[0].z * columns[1].w * columns[2].x  -  columns[0].w * columns[1].z * columns[2].x  +  columns[0].w * columns[1].x * columns[2].z  -  columns[0].x * columns[1].w * columns[2].z  -  columns[0].z * columns[1].x * columns[2].w  +  columns[0].x * columns[1].z * columns[2].w));
		mtx.columns[2] = Vec4(d * (columns[1].y * columns[2].w * columns[3].x  -  columns[1].w * columns[2].y * columns[3].x  +  columns[1].w * columns[2].x * columns[3].y  -  columns[1].x * columns[2].w * columns[3].y  -  columns[1].y * columns[2].x * columns[3].w  +  columns[1].x * columns[2].y * columns[3].w)
							, d * (columns[0].w * columns[2].y * columns[3].x  -  columns[0].y * columns[2].w * columns[3].x  -  columns[0].w * columns[2].x * columns[3].y  +  columns[0].x * columns[2].w * columns[3].y  +  columns[0].y * columns[2].x * columns[3].w  -  columns[0].x * columns[2].y * columns[3].w)
							, d * (columns[0].y * columns[1].w * columns[3].x  -  columns[0].w * columns[1].y * columns[3].x  +  columns[0].w * columns[1].x * columns[3].y  -  columns[0].x * columns[1].w * columns[3].y  -  columns[0].y * columns[1].x * columns[3].w  +  columns[0].x * columns[1].y * columns[3].w)
							, d * (columns[0].w * columns[1].y * columns[2].x  -  columns[0].y * columns[1].w * columns[2].x  -  columns[0].w * columns[1].x * columns[2].y  +  columns[0].x * columns[1].w * columns[2].y  +  columns[0].y * columns[1].x * columns[2].w  -  columns[0].x * columns[1].y * columns[2].w));
		mtx.columns[3] = Vec4(d * (columns[1].z * columns[2].y * columns[3].x  -  columns[1].y * columns[2].z * columns[3].x  -  columns[1].z * columns[2].x * columns[3].y  +  columns[1].x * columns[2].z * columns[3].y  +  columns[1].y * columns[2].x * columns[3].z  -  columns[1].x * columns[2].y * columns[3].z)
							, d * (columns[0].y * columns[2].z * columns[3].x  -  columns[0].z * columns[2].y * columns[3].x  +  columns[0].z * columns[2].x * columns[3].y  -  columns[0].x * columns[2].z * columns[3].y  -  columns[0].y * columns[2].x * columns[3].z  +  columns[0].x * columns[2].y * columns[3].z)
							, d * (columns[0].z * columns[1].y * columns[3].x  -  columns[0].y * columns[1].z * columns[3].x  -  columns[0].z * columns[1].x * columns[3].y  +  columns[0].x * columns[1].z * columns[3].y  +  columns[0].y * columns[1].x * columns[3].z  -  columns[0].x * columns[1].y * columns[3].z)
							, d * (columns[0].y * columns[1].z * columns[2].x  -  columns[0].z * columns[1].y * columns[2].x  +  columns[0].z * columns[1].x * columns[2].y  -  columns[0].x * columns[1].z * columns[2].y  -  columns[0].y * columns[1].x * columns[2].z  +  columns[0].x * columns[1].y * columns[2].z));
		return mtx;
	}

	// orthonormal
	Matrix fastInverted() const
	{
		Matrix ret;
		ret.columns[0].x = columns[0].x;
		ret.columns[1].y = columns[1].y;
		ret.columns[2].z = columns[2].z;

		ret.columns[1].x = columns[0].y;
		ret.columns[0].y = columns[1].x;

		ret.columns[1].z = columns[2].y;
		ret.columns[2].y = columns[1].z;

		ret.columns[0].z = columns[2].x;
		ret.columns[2].x = columns[0].z;

		float x = -columns[3].x;
		float y = -columns[3].y;
		float z = -columns[3].z;
		ret.columns[3].x = x * ret.columns[0].x + y * ret.columns[1].x + z * ret.columns[2].x;
		ret.columns[3].y = x * ret.columns[0].y + y * ret.columns[1].y + z * ret.columns[2].y;
		ret.columns[3].z = x * ret.columns[0].z + y * ret.columns[1].z + z * ret.columns[2].z;

		ret.columns[0].w = 0;
		ret.columns[1].w = 0;
		ret.columns[2].w = 0;
		ret.columns[3].w = 1;

		return ret;
	}
	
	void copy3x3(const Matrix& mtx)
	{
		columns[0].x = mtx.columns[0].x;
		columns[0].y = mtx.columns[0].y;
		columns[0].z = mtx.columns[0].z;

		columns[1].x = mtx.columns[1].x;
		columns[1].y = mtx.columns[1].y;
		columns[1].z = mtx.columns[1].z;

		columns[2].x = mtx.columns[2].x;
		columns[2].y = mtx.columns[2].y;
		columns[2].z = mtx.columns[2].z;
	}

	void translate(const Vec3& t)
	{
		columns[3].x += t.x;
		columns[3].y += t.y;
		columns[3].z += t.z;
	}

	void translate(float x, float y, float z)
	{
		columns[3].x += x;
		columns[3].y += y;
		columns[3].z += z;
	}

	void setTranslation(const Vec3& t)
	{
		columns[3].x = t.x;
		columns[3].y = t.y;
		columns[3].z = t.z;
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


	Vec3 getTranslation() const
	{
		return columns[3].xyz();
	}

	Quat getRotation() const;
	void transpose();
	Vec3 transformPoint(const Vec3& pos) const;
	Vec3 transformVector(const Vec3& pos) const;
	void multiply3x3(float scale);
	void setIdentity();

	Vec4 columns[4];

	static const Matrix IDENTITY;
};

struct Matrix3x4 {
	Matrix3x4() {}
	explicit Matrix3x4(const Matrix& rhs) {
		columns[0] = rhs.columns[0];
		columns[1] = rhs.columns[1];
		columns[2] = rhs.columns[2];
	}

	Vec4 columns[3];
};

struct Matrix4x3 {
	Matrix4x3() {}
	explicit Matrix4x3(const Matrix& rhs) {
		columns[0] = rhs.columns[0].xyz();
		columns[1] = rhs.columns[1].xyz();
		columns[2] = rhs.columns[2].xyz();
		columns[3] = rhs.columns[3].xyz();
	}

	Matrix3x4 transposed() const { 
		Matrix3x4 res;
		res.columns[0] = Vec4(columns[0].x, columns[1].x, columns[2].x, columns[3].x);
		res.columns[1] = Vec4(columns[0].y, columns[1].y, columns[2].y, columns[3].y);
		res.columns[2] = Vec4(columns[0].z, columns[1].z, columns[2].z, columns[3].z);
		return res;
	}

	Vec3 columns[4];
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
