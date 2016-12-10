#pragma once


#include "lumix.h"


namespace Lumix
{

struct Transform;
struct Vec3;
struct Vec4;

struct ISerializer
{
	virtual void write(const char* label, Entity entity) = 0;
	virtual void write(const char* label, ComponentHandle value) = 0;
	virtual void write(const char* label, const Transform& value) = 0;
	virtual void write(const char* label, const Vec4& value) = 0;
	virtual void write(const char* label, const Vec3& value) = 0;
	virtual void write(const char* label, float value) = 0;
	virtual void write(const char* label, bool value) = 0;
	virtual void write(const char* label, i64 value) = 0;
	virtual void write(const char* label, i32 value) = 0;
	virtual void write(const char* label, u32 value) = 0;
	virtual void write(const char* label, i8 value) = 0;
	virtual void write(const char* label, u8 value) = 0;
	virtual void write(const char* label, const char* value) = 0;
};


struct IDeserializer
{
	virtual void read(Entity* entity) = 0;
	virtual void read(ComponentHandle* value) = 0;
	virtual void read(Transform* value) = 0;
	virtual void read(Vec4* value) = 0;
	virtual void read(Vec3* value) = 0;
	virtual void read(float* value) = 0;
	virtual void read(bool* value) = 0;
	virtual void read(i64* value) = 0;
	virtual void read(u32* value) = 0;
	virtual void read(i32* value) = 0;
	virtual void read(u8* value) = 0;
	virtual void read(i8* value) = 0;
	virtual void read(char* value, int max_size) = 0;
};


}