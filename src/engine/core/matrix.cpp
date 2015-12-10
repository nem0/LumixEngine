#include "core/matrix.h"
#include "core/quat.h"
#include "core/vec.h"
#include <cmath>


namespace Lumix
{


const Matrix Matrix::IDENTITY(
	1, 0, 0, 0,
	0, 1, 0, 0,
	0, 0, 1, 0,
	0, 0, 0, 1
);


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


void Matrix::setPerspective(float fov, float ratio, float near_plane, float far_plane)
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



void multiplicate(Matrix& result, const Matrix& op1, const Matrix& op2)
{
	ASSERT(&result != &op2);
	ASSERT(&result != &op1); // use operator *

	result.m11 = op1.m11 * op2.m11 + op1.m21 * op2.m12 + op1.m31 * op2.m13 + op1.m41 * op2.m14;
	result.m21 = op1.m11 * op2.m21 + op1.m21 * op2.m22 + op1.m31 * op2.m23 + op1.m41 * op2.m24;
	result.m31 = op1.m11 * op2.m31 + op1.m21 * op2.m32 + op1.m31 * op2.m33 + op1.m41 * op2.m34;
	result.m41 = op1.m11 * op2.m41 + op1.m21 * op2.m42 + op1.m31 * op2.m43 + op1.m41 * op2.m44;

	result.m12 = op1.m12 * op2.m11 + op1.m22 * op2.m12 + op1.m32 * op2.m13 + op1.m42 * op2.m14;
	result.m22 = op1.m12 * op2.m21 + op1.m22 * op2.m22 + op1.m32 * op2.m23 + op1.m42 * op2.m24;
	result.m32 = op1.m12 * op2.m31 + op1.m22 * op2.m32 + op1.m32 * op2.m33 + op1.m42 * op2.m34;
	result.m42 = op1.m12 * op2.m41 + op1.m22 * op2.m42 + op1.m32 * op2.m43 + op1.m42 * op2.m44;

	result.m13 = op1.m13 * op2.m11 + op1.m23 * op2.m12 + op1.m33 * op2.m13 + op1.m43 * op2.m14;
	result.m23 = op1.m13 * op2.m21 + op1.m23 * op2.m22 + op1.m33 * op2.m23 + op1.m43 * op2.m24;
	result.m33 = op1.m13 * op2.m31 + op1.m23 * op2.m32 + op1.m33 * op2.m33 + op1.m43 * op2.m34;
	result.m43 = op1.m13 * op2.m41 + op1.m23 * op2.m42 + op1.m33 * op2.m43 + op1.m43 * op2.m44;

	result.m14 = op1.m14 * op2.m11 + op1.m24 * op2.m12 + op1.m34 * op2.m13 + op1.m44 * op2.m14;
	result.m24 = op1.m14 * op2.m21 + op1.m24 * op2.m22 + op1.m34 * op2.m23 + op1.m44 * op2.m24;
	result.m34 = op1.m14 * op2.m31 + op1.m24 * op2.m32 + op1.m34 * op2.m33 + op1.m44 * op2.m34;
	result.m44 = op1.m14 * op2.m41 + op1.m24 * op2.m42 + op1.m34 * op2.m43 + op1.m44 * op2.m44;
}


Matrix Matrix::operator *(const Matrix& rhs) const
{
	return Matrix(
		this->m11 * rhs.m11 + this->m21 * rhs.m12 + this->m31 * rhs.m13 + this->m41 * rhs.m14,
		this->m12 * rhs.m11 + this->m22 * rhs.m12 + this->m32 * rhs.m13 + this->m42 * rhs.m14,
		this->m13 * rhs.m11 + this->m23 * rhs.m12 + this->m33 * rhs.m13 + this->m43 * rhs.m14,
		this->m14 * rhs.m11 + this->m24 * rhs.m12 + this->m34 * rhs.m13 + this->m44 * rhs.m14,

		this->m11 * rhs.m21 + this->m21 * rhs.m22 + this->m31 * rhs.m23 + this->m41 * rhs.m24,
		this->m12 * rhs.m21 + this->m22 * rhs.m22 + this->m32 * rhs.m23 + this->m42 * rhs.m24,
		this->m13 * rhs.m21 + this->m23 * rhs.m22 + this->m33 * rhs.m23 + this->m43 * rhs.m24,
		this->m14 * rhs.m21 + this->m24 * rhs.m22 + this->m34 * rhs.m23 + this->m44 * rhs.m24,

		this->m11 * rhs.m31 + this->m21 * rhs.m32 + this->m31 * rhs.m33 + this->m41 * rhs.m34,
		this->m12 * rhs.m31 + this->m22 * rhs.m32 + this->m32 * rhs.m33 + this->m42 * rhs.m34,
		this->m13 * rhs.m31 + this->m23 * rhs.m32 + this->m33 * rhs.m33 + this->m43 * rhs.m34,
		this->m14 * rhs.m31 + this->m24 * rhs.m32 + this->m34 * rhs.m33 + this->m44 * rhs.m34,

		this->m11 * rhs.m41 + this->m21 * rhs.m42 + this->m31 * rhs.m43 + this->m41 * rhs.m44,
		this->m12 * rhs.m41 + this->m22 * rhs.m42 + this->m32 * rhs.m43 + this->m42 * rhs.m44,
		this->m13 * rhs.m41 + this->m23 * rhs.m42 + this->m33 * rhs.m43 + this->m43 * rhs.m44,
		this->m14 * rhs.m41 + this->m24 * rhs.m42 + this->m34 * rhs.m43 + this->m44 * rhs.m44
	);
}


void Matrix::getRotation(Quat& rot) const
{
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


Vec3 Matrix::multiplyPosition(const Vec3& rhs) const
{
	return Vec3(
		m11 * rhs.x + m21 * rhs.y + m31 * rhs.z + m41,	
		m12 * rhs.x + m22 * rhs.y + m32 * rhs.z + m42,	
		m13 * rhs.x + m23 * rhs.y + m33 * rhs.z + m43
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
