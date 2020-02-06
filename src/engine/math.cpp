#include "math.h"
#include "simd.h"
#include <random>
#ifdef __linux__
	#include <float.h>
#endif

namespace Lumix
{


const Vec2 Vec2::MAX = Vec2(FLT_MAX);
const Vec2 Vec2::MIN = Vec2(-FLT_MAX);
const Vec2 Vec2::ZERO = Vec2(0);

const Vec3 Vec3::MAX = Vec3(FLT_MAX);
const Vec3 Vec3::MIN = Vec3(-FLT_MAX);
const Vec3 Vec3::ZERO = Vec3(0);

const Vec4 Vec4::MAX = Vec4(FLT_MAX);
const Vec4 Vec4::MIN = Vec4(-FLT_MAX);
const Vec4 Vec4::ZERO = Vec4(0);


float Vec2::squaredLength() const
{
	float x = this->x;
	float y = this->y;
	return x * x + y * y;
}


void Vec2::normalize()
{
	float x = this->x;
	float y = this->y;
	float inv_len = 1 / sqrt(x * x + y * y);
	x *= inv_len;
	y *= inv_len;
	this->x = x;
	this->y = y;
}


Lumix::Vec2 Vec2::normalized() const
{
	float x = this->x;
	float y = this->y;
	float inv_len = 1 / sqrt(x * x + y * y);
	x *= inv_len;
	y *= inv_len;
	return Vec2(x, y);
}


float Vec2::length() const
{
	float x = this->x;
	float y = this->y;
	return sqrt(x * x + y * y);
}


void Vec3::normalize()
{
	float x = this->x;
	float y = this->y;
	float z = this->z;
	float inv_len = 1 / sqrt(x * x + y * y + z * z);
	x *= inv_len;
	y *= inv_len;
	z *= inv_len;
	this->x = x;
	this->y = y;
	this->z = z;
}


Vec3 Vec3::normalized() const
{
	float x = this->x;
	float y = this->y;
	float z = this->z;
	const float inv_len = 1 / sqrt(x * x + y * y + z * z);
	x *= inv_len;
	y *= inv_len;
	z *= inv_len;
	return Vec3(x, y, z);
}


double DVec3::length() const
{
	const double x = this->x;
	const double y = this->y;
	const double z = this->z;
	return sqrt(x * x + y * y + z * z);
}


float Vec3::length() const
{
	const float x = this->x;
	const float y = this->y;
	const float z = this->z;
	return sqrt(x * x + y * y + z * z);
}


void Vec4::normalize()
{
	float x = this->x;
	float y = this->y;
	float z = this->z;
	float w = this->w;
	float inv_len = 1 / sqrt(x * x + y * y + z * z + w * w);
	x *= inv_len;
	y *= inv_len;
	z *= inv_len;
	w *= inv_len;
	this->x = x;
	this->y = y;
	this->z = z;
	this->w = w;
}


Vec4 Vec4::normalized() const
{
	float x = this->x;
	float y = this->y;
	float z = this->z;
	float w = this->w;
	float inv_len = 1 / sqrt(x * x + y * y + z * z + w * w);
	x *= inv_len;
	y *= inv_len;
	z *= inv_len;
	w *= inv_len;
	return Vec4(x, y, z, w);
}


float Vec4::length() const
{
	float x = this->x;
	float y = this->y;
	float z = this->z;
	float w = this->w;
	return sqrt(x * x + y * y + z * z + w * w);
}


const Quat Quat::IDENTITY = { 0, 0, 0, 1 };


Quat::Quat(const Vec3& axis, float angle)
{
	float half_angle = angle * 0.5f;
	float s = sinf(half_angle);
	w = cosf(half_angle);
	x = axis.x * s;
	y = axis.y * s;
	z = axis.z * s;
}


Quat Quat::vec3ToVec3(const Vec3& v0, const Vec3& v1)
{
	const Vec3 from = v0.normalized();
	const Vec3 to = v1.normalized();
	
	float cos_angle = dotProduct(from, to);
    Vec3 half;

	
    if(cos_angle > -1.0005f && cos_angle < -0.9995f) {
        half = crossProduct(from, Vec3(to.x + 0.3f, to.y - 0.15f, to.z - 0.15f)).normalized();
    }
    else
        half = (from + to).normalized();

    // http://physicsforgames.blogspot.sk/2010/03/quaternion-tricks.html
    return Quat(
        from.y * half.z - from.z * half.y,
        from.z * half.x - from.x * half.z,
        from.x * half.y - from.y * half.x,
		dotProduct(from, half));
}


void Quat::fromEuler(const Vec3& euler)
{
	ASSERT(euler.x >= -HALF_PI && euler.x <= HALF_PI);
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
			-PI * 0.5f, 0.0f, -atan2f(2.0f * (x * z - w * y), 1.0f - 2.0f * (y * y + z * z)));
	}
	if (check > 0.995f)
	{
		return Vec3(
			PI * 0.5f, 0.0f, atan2f(2.0f * (x * z - w * y), 1.0f - 2.0f * (y * y + z * z)));
	}
	return Vec3(asinf(check),
		atan2f(2.0f * (x * z + w * y), 1.0f - 2.0f * (x * x + y * y)),
		atan2f(2.0f * (x * y + w * z), 1.0f - 2.0f * (x * x + z * z)));
}


void Quat::conjugate()
{
	w = -w;
}


Quat Quat::conjugated() const
{
	return Quat(x, y, z, -w);
}


void Quat::normalize()
{
	float l = 1/sqrt(x*x + y*y + z*z + w*w);
	x *= l;
	y *= l;
	z *= l;
	w *= l;
}


Quat Quat::normalized() const
{
	float l = 1 / sqrt(x * x + y * y + z * z + w * w);
	return Quat(x * l, y * l, z * l, w * l);
}


Quat nlerp(const Quat& q1, const Quat& q2, float t)
{
	Quat res;
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
	res.x = ox;
	res.y = oy;
	res.z = oz;
	res.w = ow;
	return res;
}


Quat Quat::operator*(const Quat& rhs) const
{
	return Quat(w * rhs.x + rhs.w * x + y * rhs.z - rhs.y * z,
		w * rhs.y + rhs.w * y + z * rhs.x - rhs.z * x,
		w * rhs.z + rhs.w * z + x * rhs.y - rhs.x * y,
		w * rhs.w - x * rhs.x - y * rhs.y - z * rhs.z);
}


Quat Quat::operator-() const
{
	return Quat(x, y, z, -w);
}


Quat Quat::operator+(const Quat& q) const
{
	return Quat(x + q.x, y + q.y, z + q.z, w + q.w);
}


Quat Quat::operator*(float m) const
{
	return Quat(x * m, y * m, z * m, w * m);
}


Vec3 Quat::operator*(const Vec3& q) const
{
	return rotate(q);
}


Matrix Quat::toMatrix() const
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

	Matrix mtx;
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
	return mtx;
}

	
Matrix LocalRigidTransform::toMatrix() const
{
	return Matrix(pos, rot);
}


LocalRigidTransform LocalRigidTransform::operator*(const LocalRigidTransform& rhs) const
{
	return{ rot.rotate(rhs.pos) + pos, rot * rhs.rot };
}


LocalRigidTransform LocalRigidTransform::interpolate(const LocalRigidTransform& rhs, float t) const
{
	LocalRigidTransform ret;
	ret.pos = lerp(pos, rhs.pos, t);
	ret.rot = nlerp(rot, rhs.rot, t);
	return ret;
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


void Matrix::setOrtho(float left, float right, float bottom, float top, float z_near, float z_far, bool is_homogenous_depth, bool reversed_z)
{
	*this = IDENTITY;
	m11 = 2 / (right - left);
	m22 = 2 / (top - bottom);
	m41 = (right + left) / (left - right);
	m42 = (top + bottom) / (bottom - top);
	if (reversed_z)
	{
		m33 = (is_homogenous_depth ? 2 : 1) / (z_far - z_near);
		m43 = is_homogenous_depth ? (z_near + z_far) / (z_far - z_near) : z_far / (z_far - z_near);
	}
	else
	{
		m33 = (is_homogenous_depth ? -2 : -1) / (z_far - z_near);
		m43 = is_homogenous_depth ? (z_near + z_far) / (z_near - z_far) : z_near / (z_near - z_far);
	}
}


void Matrix::setPerspective(float fov, float ratio, float near_plane, float far_plane, bool is_homogenous_depth, bool reversed_z)
{
	*this = Matrix::IDENTITY;
	float f = 1 / tanf(fov * 0.5f);
	float z_diff = near_plane - far_plane;
	m11 = f / ratio;
	m22 = f;
	m44 = 0;
	m34 = -1.0f;

	if (reversed_z)
	{
		m33 = (is_homogenous_depth ? near_plane : -far_plane) / z_diff - (is_homogenous_depth ? 0 : 1);
		m43 = is_homogenous_depth ? far_plane * near_plane / z_diff : -near_plane * far_plane / z_diff;
	}
	else
	{
		m33 = (is_homogenous_depth ? far_plane + near_plane : far_plane) / z_diff;
		m43 = is_homogenous_depth ? 2 * far_plane * near_plane / z_diff : near_plane * far_plane / z_diff;
	}
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

	m12 *= scale.y;
	m22 *= scale.y;
	m32 *= scale.y;

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


Vec3 degreesToRadians(const Vec3& v)
{
	return Vec3(degreesToRadians(v.x), degreesToRadians(v.y), degreesToRadians(v.z));
}

Vec3 radiansToDegrees(const Vec3& v)
{
	return Vec3(radiansToDegrees(v.x), radiansToDegrees(v.y), radiansToDegrees(v.z));
}

float angleDiff(float a, float b)
{
	float delta = a - b;
	delta = fmodf(delta, PI * 2);
	if (delta > PI) return -PI * 2 + delta;
	if (delta < -PI) return PI * 2 + delta;
	return delta;
}

bool getRayPlaneIntersecion(const Vec3& origin,
	const Vec3& dir,
	const Vec3& plane_point,
	const Vec3& normal,
	float& out)
{
	float d = dotProduct(dir, normal);
	if (d == 0)
	{
		return false;
	}
	d = dotProduct(plane_point - origin, normal) / d;
	out = d;
	return true;
}

bool getRaySphereIntersection(const Vec3& origin,
	const Vec3& dir,
	const Vec3& center,
	float radius,
	Vec3& out)
{
	ASSERT(dir.length() < 1.01f && dir.length() > 0.99f);
	Vec3 L = center - origin;
	float tca = dotProduct(L, dir);
	if (tca < 0) return false;
	float d2 = dotProduct(L, L) - tca * tca;
	if (d2 > radius * radius) return false;
	float thc = sqrt(radius * radius - d2);
	float t0 = tca - thc;
	out = origin + dir * t0;
	return true;
}

bool getRayAABBIntersection(const Vec3& origin,
	const Vec3& dir,
	const Vec3& min,
	const Vec3& size,
	Vec3& out)
{
	Vec3 dirfrac;

	dirfrac.x = 1.0f / (dir.x == 0 ? 0.00000001f : dir.x);
	dirfrac.y = 1.0f / (dir.y == 0 ? 0.00000001f : dir.y);
	dirfrac.z = 1.0f / (dir.z == 0 ? 0.00000001f : dir.z);

	Vec3 max = min + size;
	float t1 = (min.x - origin.x) * dirfrac.x;
	float t2 = (max.x - origin.x) * dirfrac.x;
	float t3 = (min.y - origin.y) * dirfrac.y;
	float t4 = (max.y - origin.y) * dirfrac.y;
	float t5 = (min.z - origin.z) * dirfrac.z;
	float t6 = (max.z - origin.z) * dirfrac.z;

	float tmin = maximum(
		maximum(minimum(t1, t2), minimum(t3, t4)), minimum(t5, t6));
	float tmax = minimum(
		minimum(maximum(t1, t2), maximum(t3, t4)), maximum(t5, t6));

	if (tmax < 0)
	{
		return false;
	}

	if (tmin > tmax)
	{
		return false;
	}

	out = tmin < 0 ? origin : origin + dir * tmin;
	return true;
}


float getLineSegmentDistance(const Vec3& origin, const Vec3& dir, const Vec3& a, const Vec3& b)
{
	Vec3 a_origin = origin - a;
	Vec3 ab = b - a;

	float dot1 = dotProduct(ab, a_origin);
	float dot2 = dotProduct(ab, dir);
	float dot3 = dotProduct(dir, a_origin);
	float dot4 = dotProduct(ab, ab);
	float dot5 = dotProduct(dir, dir);

	float denom = dot4 * dot5 - dot2 * dot2;
	if (abs(denom) < 1e-5f)
	{
		Vec3 X = origin + dir * dotProduct(b - origin, dir);
		return (b - X).length();
	}

	float numer = dot1 * dot2 - dot3 * dot4;
	float param_a = numer / denom;
	float param_b = (dot1 + dot2 * param_a) / dot4;

	if (param_b < 0 || param_b > 1)
	{
		param_b = clamp(param_b, 0.0f, 1.0f);
		Vec3 B = a + ab * param_b;
		Vec3 X = origin + dir * dotProduct(b - origin, dir);
		return (B - X).length();
	}

	Vec3 vec = (origin + dir * param_a) - (a + ab * param_b);
	return vec.length();
}


bool getRayTriangleIntersection(const Vec3& origin,
	const Vec3& dir,
	const Vec3& p0,
	const Vec3& p1,
	const Vec3& p2,
	float* out_t)
{
	Vec3 normal = crossProduct(p1 - p0, p2 - p0);
	float q = dotProduct(normal, dir);
	if (q == 0) return false;

	float d = -dotProduct(normal, p0);
	float t = -(dotProduct(normal, origin) + d) / q;
	if (t < 0) return false;

	Vec3 hit_point = origin + dir * t;

	Vec3 edge0 = p1 - p0;
	Vec3 VP0 = hit_point - p0;
	if (dotProduct(normal, crossProduct(edge0, VP0)) < 0)
	{
		return false;
	}

	Vec3 edge1 = p2 - p1;
	Vec3 VP1 = hit_point - p1;
	if (dotProduct(normal, crossProduct(edge1, VP1)) < 0)
	{
		return false;
	}

	Vec3 edge2 = p0 - p2;
	Vec3 VP2 = hit_point - p2;
	if (dotProduct(normal, crossProduct(edge2, VP2)) < 0)
	{
		return false;
	}

	if (out_t) *out_t = t;
	return true;
}


LUMIX_ENGINE_API bool getSphereTriangleIntersection(const Vec3& center,
	float radius,
	const Vec3& v0,
	const Vec3& v1,
	const Vec3& v2)
{
	Vec3 normal = crossProduct(v0 - v1, v2 - v1).normalized();
	float D = -dotProduct(v0, normal);

	float dist = dotProduct(center, normal) + D;

	if (fabs(dist) > radius) return false;

	float squared_radius = radius * radius;
	if ((v0 - center).squaredLength() < squared_radius) return true;
	if ((v1 - center).squaredLength() < squared_radius) return true;
	if ((v2 - center).squaredLength() < squared_radius) return true;

	return false;
}


static std::mt19937_64& getGUIDRandomGenerator()
{
	static std::random_device seed;
	static std::mt19937_64 gen(seed());

	return gen;
}

static std::mt19937& getRandomGenerator()
{
	static std::random_device seed;
	static std::mt19937 gen(seed());

	return gen;
}


u64 randGUID()
{
	return getGUIDRandomGenerator()();
}


u32 rand()
{
	return getRandomGenerator()();
}


u32 rand(u32 from_incl, u32 to_incl)
{
	std::uniform_int_distribution<> dist(from_incl, to_incl);
	return dist(getRandomGenerator());
}


float randFloat()
{
	std::uniform_real_distribution<float> dist;
	return dist(getRandomGenerator());
}


void seedRandom(u32 seed)
{
	getRandomGenerator().seed(seed);
}


float randFloat(float from, float to)
{
	std::uniform_real_distribution<float> dist(from, to);
	return dist(getRandomGenerator());
}


} // namespace Lumix