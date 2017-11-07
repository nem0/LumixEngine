#include "vec.h"
#include <cmath>
#include <float.h>


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
	float inv_len = 1 / sqrt(x * x + y * y + z * z);
	x *= inv_len;
	y *= inv_len;
	z *= inv_len;
	return Vec3(x, y, z);
}


float Vec3::length() const
{
	float x = this->x;
	float y = this->y;
	float z = this->z;
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



} // namespace Lumix