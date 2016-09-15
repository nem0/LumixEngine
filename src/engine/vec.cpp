#include "vec.h"
#include <cmath>


namespace Lumix
{


float Vec2::squaredLength() const
{
	float x = this->x;
	float y = this->y;
	return x * x + y * y;
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
	this->x = x;
	this->y = y;
	this->z = z;
	this->w = w;
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