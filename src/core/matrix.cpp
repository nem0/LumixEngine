#include "core/matrix.h"
#include <cassert>
#include "core/vec3.h"
#include "core/quat.h"


namespace Lux
{


const Matrix Matrix::IDENTITY(
	1, 0, 0, 0,
	0, 1, 0, 0,
	0, 0, 1, 0,
	0, 0, 0, 1
);


void multiplicate(Matrix& result, const Matrix& op1, const Matrix& op2)
{
	assert(&result != &op2);
	assert(&result != &op1); // use operator *

	/// TODO optimize
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


void Matrix::getTranslation(Vec3& pos) const
{
	pos.x = m41;
	pos.y = m42;
	pos.z = m43;
}


Vec3 Matrix::getTranslation() const
{
	return Vec3(m41, m42, m43);
}


void Matrix::getRotation(Quat& rot) const
{
	float tr = m11 + m22 + m33;

	if (tr > 0)
	{ 
		float S = sqrt(tr+1.0f) * 2;
		rot.w = 0.25f * S;
		rot.x = (m23 - m32) / S;
		rot.y = (m31 - m13) / S; 
		rot.z = (m12 - m21) / S; 
	} 
	else if ((m11 > m22)&(m11 > m33))
	{ 
		float S = sqrt(1.0f + m11 - m22 - m33) * 2; 
		rot.w = (m23 - m32) / S;
		rot.x = 0.25f * S;
		rot.y = (m21 + m12) / S; 
		rot.z = (m31 + m13) / S; 
	}
	else if (m22 > m33)
	{ 
		float S = sqrt(1.0f + m22 - m11 - m33) * 2; 
		rot.w = (m31 - m13) / S;
		rot.x = (m32 + m12) / S; 
		rot.y = 0.25f * S;
		rot.z = (m32 + m23) / S; 
	}
	else
	{ 
		float S = sqrt(1.0f + m33 - m11 - m22) * 2; 
		rot.w = (m12 - m21) / S;
		rot.x = (m31 + m13) / S;
		rot.y = (m32 + m23) / S;
		rot.z = 0.25f * S;
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


Vec3 Matrix::mutliplyPosition(const Vec3& rhs)
{
	return Vec3(
		m11 * rhs.x + m21 * rhs.y + m31 * rhs.z + m41,	
		m12 * rhs.x + m22 * rhs.y + m32 * rhs.z + m42,	
		m13 * rhs.x + m23 * rhs.y + m33 * rhs.z + m43
	);
}


void Matrix::setIdentity(Matrix& mtx)
{
	mtx.m11 = 1; mtx.m12 = 0; mtx.m13 = 0; mtx.m14 = 0; 
	mtx.m21 = 0; mtx.m22 = 1; mtx.m23 = 0; mtx.m24 = 0; 
	mtx.m31 = 0; mtx.m32 = 0; mtx.m33 = 1; mtx.m34 = 0; 
	mtx.m41 = 0; mtx.m42 = 0; mtx.m43 = 0; mtx.m44 = 1; 
}


} // !namespace Lux