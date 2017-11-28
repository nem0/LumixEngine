#include "engine/matrix.h"
#include "engine/quat.h"
#include "engine/simd.h"
#include "engine/vec.h"
#include <cmath>


namespace Lumix
{


Matrix RigidTransform::toMatrix() const
{
	Matrix mtx = rot.toMatrix();
	mtx.setTranslation(pos);
	return mtx;
}


Matrix Transform::toMatrix() const
{
	Matrix mtx = rot.toMatrix();
	mtx.setTranslation(pos);
	mtx.multiply3x3(scale);
	return mtx;
}


const Matrix Matrix::IDENTITY(
	1, 0, 0, 0,
	0, 1, 0, 0,
	0, 0, 1, 0,
	0, 0, 0, 1
);


Matrix::Matrix(const Vec3& pos, const Quat& rot)
{
	*this = rot.toMatrix();
	setTranslation(pos);
}


void Matrix::fromEuler(float yaw, float pitch, float roll)
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


Matrix Matrix::rotationX(float angle)
{
	Matrix m = IDENTITY;
	float c = cosf(angle);
	float s = sinf(angle);

	m.m22 = m.m33 = c;
	m.m32 = -s;
	m.m23 = s;

	return m;
}


Matrix Matrix::rotationY(float angle)
{
	Matrix m = IDENTITY;
	float c = cosf(angle);
	float s = sinf(angle);

	m.m11 = m.m33 = c;
	m.m31 = s;
	m.m13 = -s;

	return m;
}

Matrix Matrix::rotationZ(float angle)
{
	Matrix m = IDENTITY;
	float c = cosf(angle);
	float s = sinf(angle);

	m.m11 = m.m22 = c;
	m.m21 = -s;
	m.m12 = s;
	
	return m;
}


void Matrix::setPerspective(float fov, float ratio, float near_plane, float far_plane, bool is_homogenous_depth)
{
	*this = Matrix::IDENTITY;
	float f = 1 / tanf(fov * 0.5f);
	float z_diff = near_plane - far_plane;
	m11 = f / ratio;
	m22 = f;
	m33 = (is_homogenous_depth ? far_plane + near_plane : far_plane) / z_diff;
	m44 = 0;
	m43 = is_homogenous_depth ? 2 * far_plane * near_plane / z_diff : near_plane * far_plane / z_diff;
	m34 = -1.0f;
}


void Matrix::decompose(Vec3& position, Quat& rotation, float& scale) const
{
	getTranslation(position);
	scale = getXVector().length();
	Matrix tmp = *this;
	tmp.multiply3x3(1 / scale);
	rotation = tmp.getRotation();
}


Matrix Matrix::operator *(float rhs) const
{
	Matrix out;
	out.m11 = m11 * rhs;
	out.m12 = m12 * rhs;
	out.m13 = m13 * rhs;
	out.m14 = m14 * rhs;

	out.m21 = m21 * rhs;
	out.m22 = m22 * rhs;
	out.m23 = m23 * rhs;
	out.m24 = m24 * rhs;

	out.m31 = m31 * rhs;
	out.m32 = m32 * rhs;
	out.m33 = m33 * rhs;
	out.m34 = m34 * rhs;

	out.m41 = m41 * rhs;
	out.m42 = m42 * rhs;
	out.m43 = m43 * rhs;
	out.m44 = m44 * rhs;

	return out;
}


Matrix Matrix::operator +(const Matrix& rhs) const
{
	Matrix out;

	out.m11 = m11 + rhs.m11;
	out.m12 = m12 + rhs.m12;
	out.m13 = m13 + rhs.m13;
	out.m14 = m14 + rhs.m14;

	out.m21 = m21 + rhs.m21;
	out.m22 = m22 + rhs.m22;
	out.m23 = m23 + rhs.m23;
	out.m24 = m24 + rhs.m24;

	out.m31 = m31 + rhs.m31;
	out.m32 = m32 + rhs.m32;
	out.m33 = m33 + rhs.m33;
	out.m34 = m34 + rhs.m34;

	out.m41 = m41 + rhs.m41;
	out.m42 = m42 + rhs.m42;
	out.m43 = m43 + rhs.m43;
	out.m44 = m44 + rhs.m44;

	return out;
}


Matrix Matrix::operator *(const Matrix& rhs) const
{
	Matrix out;

	const float4 a = f4Load(&m11);
	const float4 b = f4Load(&m21);
	const float4 c = f4Load(&m31);
	const float4 d = f4Load(&m41);

	float4 t1 = f4Splat(rhs.m11);
	float4 t2 = f4Mul(a, t1);
	t1 = f4Splat(rhs.m12);
	t2 = f4Add(f4Mul(b, t1), t2);
	t1 = f4Splat(rhs.m13);
	t2 = f4Add(f4Mul(c, t1), t2);
	t1 = f4Splat(rhs.m14);
	t2 = f4Add(f4Mul(d, t1), t2);

	f4Store(&out.m11, t2);

	t1 = f4Splat(rhs.m21);
	t2 = f4Mul(a, t1);
	t1 = f4Splat(rhs.m22);
	t2 = f4Add(f4Mul(b, t1), t2);
	t1 = f4Splat(rhs.m23);
	t2 = f4Add(f4Mul(c, t1), t2);
	t1 = f4Splat(rhs.m24);
	t2 = f4Add(f4Mul(d, t1), t2);

	f4Store(&out.m21, t2);

	t1 = f4Splat(rhs.m31);
	t2 = f4Mul(a, t1);
	t1 = f4Splat(rhs.m32);
	t2 = f4Add(f4Mul(b, t1), t2);
	t1 = f4Splat(rhs.m33);
	t2 = f4Add(f4Mul(c, t1), t2);
	t1 = f4Splat(rhs.m34);
	t2 = f4Add(f4Mul(d, t1), t2);

	f4Store(&out.m31, t2);

	t1 = f4Splat(rhs.m41);
	t2 = f4Mul(a, t1);
	t1 = f4Splat(rhs.m42);
	t2 = f4Add(f4Mul(b, t1), t2);
	t1 = f4Splat(rhs.m43);
	t2 = f4Add(f4Mul(c, t1), t2);
	t1 = f4Splat(rhs.m44);
	t2 = f4Add(f4Mul(d, t1), t2);

	f4Store(&out.m41, t2);

	return out;
}


void Matrix::normalizeScale()
{
	Vec3 scale = {
		1 / Vec3(m11, m21, m31).length(),
		1 / Vec3(m12, m22, m32).length(),
		1 / Vec3(m13, m23, m33).length()
	};

	m11 *= scale.x;
	m21 *= scale.x;
	m31 *= scale.x;

	m12 *= scale.z;
	m22 *= scale.z;
	m32 *= scale.z;

	m13 *= scale.z;
	m23 *= scale.z;
	m33 *= scale.z;
}


Quat Matrix::getRotation() const
{
	Quat rot;
	float tr = m11 + m22 + m33;

	if (tr > 0)
	{ 
		float t = tr + 1.0f;
		float s = 1 / sqrt(t) * 0.5f;

		rot.w = s * t;
		rot.z = (m12 - m21) * s;
		rot.y = (m31 - m13) * s;
		rot.x = (m23 - m32) * s;
	} 
	else if ((m11 > m22) && (m11 > m33))
	{ 
		float t = 1.0f + m11 - m22 - m33;
		float s = 1 / sqrt(t) * 0.5f;
		rot.x = s * t;
		rot.y = (m12 + m21) * s; 
		rot.z = (m13 + m31) * s; 
		rot.w = (m23 - m32) * s;
	}
	else if (m22 > m33)
	{ 
		float t = 1.0f + m22 - m11 - m33;
		float s = 1 / sqrt(t) * 0.5f; 
		rot.w = (m31 - m13) * s;
		rot.x = (m12 + m21) * s; 
		rot.y = s * t;
		rot.z = (m23 + m32) * s; 
	}
	else
	{ 
		float t = 1.0f + m33 - m11 - m22;
		float s = 1 / sqrt(t) * 0.5f; 
		rot.w = (m12 - m21) * s;
		rot.x = (m31 + m13) * s;
		rot.y = (m32 + m23) * s;
		rot.z = s * t;
	}
	return rot;
}


void Matrix::transpose()
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

		tmp = m41;
		m41 = m14;
		m14 = tmp;

		tmp = m42;
		m42 = m24;
		m24 = tmp;

		tmp = m43;
		m43 = m34;
		m34 = tmp;
}


void Matrix::multiply3x3(float scale)
{
	m11 *= scale;
	m12 *= scale;
	m13 *= scale;
	m21 *= scale;
	m22 *= scale;
	m23 *= scale;
	m31 *= scale;
	m32 *= scale;
	m33 *= scale;
}


Vec3 Matrix::transformPoint(const Vec3& rhs) const
{
	return Vec3(
		m11 * rhs.x + m21 * rhs.y + m31 * rhs.z + m41,
		m12 * rhs.x + m22 * rhs.y + m32 * rhs.z + m42,
		m13 * rhs.x + m23 * rhs.y + m33 * rhs.z + m43
	);
}


Vec3 Matrix::transformVector(const Vec3& rhs) const
{
	return Vec3(
		m11 * rhs.x + m21 * rhs.y + m31 * rhs.z,
		m12 * rhs.x + m22 * rhs.y + m32 * rhs.z,
		m13 * rhs.x + m23 * rhs.y + m33 * rhs.z
	);
}


void Matrix::setIdentity()
{
	m11 = 1; m12 = 0; m13 = 0; m14 = 0; 
	m21 = 0; m22 = 1; m23 = 0; m24 = 0; 
	m31 = 0; m32 = 0; m33 = 1; m34 = 0; 
	m41 = 0; m42 = 0; m43 = 0; m44 = 1; 
}


} // !namespace Lumix
