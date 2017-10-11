#pragma once


#include "engine/lumix.h"
#include "engine/quat.h"
#include "engine/vec.h"


namespace Lumix
{


struct Quat;
struct Transform;


struct LUMIX_ENGINE_API RigidTransform
{
	RigidTransform() {}


	RigidTransform(const Vec3& _pos, const Quat& _rot)
		: pos(_pos)
		, rot(_rot)
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


	Vec3 transform(const Vec3& value) const
	{
		return pos + rot.rotate(value);
	}


	RigidTransform interpolate(const RigidTransform& rhs, float t)
	{
		RigidTransform ret;
		lerp(pos, rhs.pos, &ret.pos, t);
		nlerp(rot, rhs.rot, &ret.rot, t);
		return ret;
	}


	inline Transform toScaled(float scale) const;


	Matrix toMatrix() const;


	Quat rot;
	Vec3 pos;
};


struct LUMIX_ENGINE_API Transform
{
	Transform() {}


	Transform(const Vec3& _pos, const Quat& _rot, float _scale)
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


	Vec3 transform(const Vec3& value) const
	{
		return pos + rot.rotate(value) * scale;
	}


	RigidTransform getRigidPart() const
	{
		return {pos, rot};
	}


	Matrix toMatrix() const;


	Quat rot;
	Vec3 pos;
	float scale;
};


Transform RigidTransform::toScaled(float scale) const
{
	return {pos, rot, scale};
}


LUMIX_ALIGN_BEGIN(16) struct LUMIX_ENGINE_API Matrix
{
	static Matrix rotationX(float angle);
	static Matrix rotationY(float angle);
	static Matrix rotationZ(float angle);

	Matrix() {}

	Matrix(const Vec3& pos, const Quat& rot);

	Matrix(const float* m)
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


	void setOrtho(float left, float right, float bottom, float top, float z_near, float z_far, bool is_homogenous_depth)
	{
		*this = IDENTITY;
		m11 = 2 / (right - left);
		m22 = 2 / (top - bottom);
		m33 = (is_homogenous_depth ? -2 : -1) / (z_far - z_near);
		m41 = (right + left) / (left - right);
		m42 = (top + bottom) / (bottom - top);
		m43 = is_homogenous_depth ? (z_near + z_far) / (z_near - z_far) : z_near / (z_near - z_far);
	}


	void setPerspective(float fov, float ratio, float near_plane, float far_plane, bool is_homogenous_depth);


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

	RigidTransform toTransform() { return {getTranslation(), getRotation()}; }
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
} LUMIX_ALIGN_END(16);


} // !namespace Lumix
