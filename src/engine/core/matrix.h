#pragma once


#include "lumix.h"
#include "core/vec3.h"
#include "core/vec4.h"


namespace Lumix
{


struct Quat;


struct LUMIX_ENGINE_API Matrix
{
	Matrix() {}

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

	Matrix operator *(const Matrix& rhs) const;

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
	
	float determinant()
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


	void setOrtho(float left, float right, float top, float bottom, float z_near, float z_far)
	{
		*this = IDENTITY;
		m11 = 2 / (right - left);
		m22 = 2 / (top - bottom);
		m33 = -1 / (z_far - z_near);
		m41 = (right + left) / (left - right);
		m42 = (top + bottom) / (bottom - top);
		m43 = z_near / (z_near - z_far);
	}


	void setPerspective(float fov, float ratio, float near_plane, float far_plane)
	{
		*this = Matrix::IDENTITY;
		float f = 1 / tanf(fov * 0.5f);
		m11 = f / ratio;
		m22 = f;
		m33 = (far_plane + near_plane) / (near_plane - far_plane);
		m44 = 0;
		m43 = (2 * far_plane * near_plane) / (near_plane - far_plane);
		m34 = -1;
	}


	void fromEuler(float yaw, float pitch, float roll)
	{
		float sroll = sinf(roll);
		float croll = cosf(roll);
		float spitch = sinf(pitch);
		float cpitch = cosf(pitch);
		float syaw = sinf(yaw);
		float cyaw = cosf(yaw);

		m11 = sroll * spitch * syaw + croll * cyaw;
		m12 = sroll * cpitch;
		m13 = sroll * spitch * cyaw - croll * syaw;
		m14 = 0.0f;
		m21 = croll * spitch * syaw - sroll * cyaw;
		m22 = croll * cpitch;
		m23 = croll * spitch * cyaw + sroll * syaw;
		m24 = 0.0f;
		m31 = cpitch * syaw;
		m32 = -spitch;
		m33 = cpitch * cyaw;
		m34 = 0.0f;
		m41 = 0.0f;
		m42 = 0.0f;
		m43 = 0.0f;
		m44 = 1.0f;
	}


	void lookAt(const Vec3& pos, const Vec3& center, const Vec3& up)
	{
		*this = Matrix::IDENTITY;
		Vec3 f = center - pos;
		f.normalize();
		Vec3 r = crossProduct(f, up);
		r.normalize();
		Vec3 u = crossProduct(r, f);
		setXVector(r);
		setYVector(u);
		setZVector(-f);
		transpose();
		setTranslation(Vec3(-dotProduct(r, pos), -dotProduct(u, pos), dotProduct(f, pos)));
	}


	void getTranslation(Vec3& pos) const;
	Vec3 getTranslation() const;
	void getRotation(Quat& rot) const;
	void transpose();
	Vec3 multiplyPosition(const Vec3& pos) const;
	void multiply3x3(float scale);
	void setIdentity();

	float m11, m12, m13, m14;
	float m21, m22, m23, m24;
	float m31, m32, m33, m34;
	float m41, m42, m43, m44;

	static const Matrix IDENTITY;
};


void multiplicate(Matrix& result, const Matrix& op1, const Matrix& op2);


} // !namespace Lumix
