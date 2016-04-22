#include "engine/core/quat.h"
#include <cmath>
#include "engine/core/vec.h"
#include "engine/core/math_utils.h"
#include "engine/core/matrix.h"


namespace Lumix
{


Quat::AxisAngle Quat::getAxisAngle() const
{
	AxisAngle ret;
	if (fabs(1 - w*w) < 0.00001f)
	{
		ret.angle = 0;
		ret.axis.set(0, 1, 0);
	}
	else
	{
		ret.angle = 2 * acosf(w);
		float tmp = 1 / sqrt(1 - w*w);
		ret.axis.set(x * tmp, y * tmp, z * tmp);
	}
	return ret;
}


Quat::Quat(const Vec3& axis, float angle)
{
	float half_angle = angle * 0.5f;
	float s = sinf(half_angle);
	w = cosf(half_angle);
	x = axis.x * s;
	y = axis.y * s;
	z = axis.z * s;
}


void Quat::fromEuler(const Vec3& euler)
{
	float ex = euler.x * 0.5f;
	float ey = euler.y * 0.5f;
	float ez = euler.z * 0.5f;
	float sinX = sinf(ex);
	float cosX = cosf(ex);
	float sinY = sinf(ey);
	float cosY = cosf(ey);
	float sinZ = sinf(ez);
	float cosZ = cosf(ez);

	w = cosY * cosX * cosZ + sinY * sinX * sinZ;
	x = cosY * sinX * cosZ + sinY * cosX * sinZ;
	y = sinY * cosX * cosZ - cosY * sinX * sinZ;
	z = cosY * cosX * sinZ - sinY * sinX * cosZ;
}


Vec3 Quat::toEuler() const
{
	// from urho3d
	float check = 2.0f * (-y * z + w * x);

	if (check < -0.995f)
	{
		return Vec3(
			-Math::PI * 0.5f, 0.0f, -atan2f(2.0f * (x * z - w * y), 1.0f - 2.0f * (y * y + z * z)));
	}
	else if (check > 0.995f)
	{
		return Vec3(
			Math::PI * 0.5f, 0.0f, atan2f(2.0f * (x * z - w * y), 1.0f - 2.0f * (y * y + z * z)));
	}
	else
	{
		return Vec3(asinf(check),
			atan2f(2.0f * (x * z + w * y), 1.0f - 2.0f * (x * x + y * y)),
			atan2f(2.0f * (x * y + w * z), 1.0f - 2.0f * (x * x + z * z)));
	}
}


void Quat::conjugate()
{
	w = -w;
}


void Quat::conjugated(Quat& q)
{
	q.x = x;
	q.y = y;
	q.z = z;
	q.w = -w;
}


void Quat::normalize()
{
	float l = 1/sqrt(x*x + y*y + z*z + w*w);
	x *= l;
	y *= l;
	z *= l;
	w *= l;
}


void nlerp(const Quat& q1, const Quat& q2, Quat* out, float t)
{
	float inv = 1.0f - t;
	if (q1.x * q2.x + q1.y * q2.y + q1.z * q2.z + q1.w * q2.w < 0) t = -t;
	float ox = q1.x * inv + q2.x * t;
	float oy = q1.y * inv + q2.y * t;
	float oz = q1.z * inv + q2.z * t;
	float ow = q1.w * inv + q2.w * t;
	float l = 1 / sqrt(ox * ox + oy * oy + oz * oz + ow * ow);
	ox *= l;
	oy *= l;
	oz *= l;
	ow *= l;
	out->x = ox;
	out->y = oy;
	out->z = oz;
	out->w = ow;
}


Quat Quat::operator *(const Quat& rhs) const
{
	Quat tmp;

	tmp.w = (rhs.w * w) - (rhs.x * x) - (rhs.y * y) - (rhs.z * z);
	tmp.x = (rhs.w * x) + (rhs.x * w) + (rhs.y * z) - (rhs.z * y);
	tmp.y = (rhs.w * y) + (rhs.y * w) + (rhs.z * x) - (rhs.x * z);
	tmp.z = (rhs.w * z) + (rhs.z * w) + (rhs.x * y) - (rhs.y * x);

	return tmp;
}


Quat Quat::operator -() const
{
	return Quat(x, y, z, -w);
}


void Quat::toMatrix(Matrix& mtx) const
{
	float fx = x + x;
	float fy = y + y;
	float fz = z + z;
	float fwx = fx*w;
	float fwy = fy*w;
	float fwz = fz*w;
	float fxx = fx*x;
	float fxy = fy*x;
	float fxz = fz*x;
	float fyy = fy*y;
	float fyz = fz*y;
	float fzz = fz*z;

	mtx.m11 = 1.0f - (fyy + fzz);
	mtx.m21 = fxy - fwz;
	mtx.m31 = fxz + fwy;
	mtx.m12 = fxy + fwz;
	mtx.m22 = 1.0f - (fxx + fzz);
	mtx.m32 = fyz - fwx;
	mtx.m13 = fxz - fwy;
	mtx.m23 = fyz + fwx;
	mtx.m33 = 1.0f - (fxx + fyy);

	mtx.m41 = mtx.m42 = mtx.m43 = mtx.m14 = mtx.m24 = mtx.m34 = 0;
	mtx.m44 = 1;
}


} // !namespace Lumix
