#pragma once


#include "lumix.h"


namespace Lumix
{

struct DVec3;
struct InputMemoryStream;
struct OutputMemoryStream;
struct LocalRigidTransform;
struct Quat;
struct RigidTransform;
struct String;
struct Transform;
struct IVec3;
struct Vec3;
struct Vec4;

struct LUMIX_ENGINE_API TextSerializer
{
	TextSerializer(OutputMemoryStream& _blob)
		: blob(_blob)
	{
	}

	void write(const char* label, EntityPtr entity);
	void write(const char* label, EntityRef entity);
	void write(const char* label, const RigidTransform& value);
	void write(const char* label, const LocalRigidTransform& value);
	void write(const char* label, const Transform& value);
	void write(const char* label, const Vec4& value);
	void write(const char* label, const DVec3& value);
	void write(const char* label, const Vec3& value);
	void write(const char* label, const IVec3& value);
	void write(const char* label, const Quat& value);
	void write(const char* label, float value);
	void write(const char* label, double value);
	void write(const char* label, bool value);
	void write(const char* label, i64 value);
	void write(const char* label, u64 value);
	void write(const char* label, i32 value);
	void write(const char* label, u32 value);
	void write(const char* label, u16 value);
	void write(const char* label, i8 value);
	void write(const char* label, u8 value);
	void write(const char* label, const char* value);

	OutputMemoryStream& blob;
};


struct LUMIX_ENGINE_API TextDeserializer
{
	TextDeserializer(InputMemoryStream& _blob)
		: blob(_blob)
	{
	}

	void read(Ref<EntityPtr> entity);
	void read(Ref<EntityRef> entity);
	void read(Ref<RigidTransform> value);
	void read(Ref<LocalRigidTransform> value);
	void read(Ref<Transform> value);
	void read(Ref<Vec4> value);
	void read(Ref<DVec3> value);
	void read(Ref<Vec3> value);
	void read(Ref<IVec3> value);
	void read(Ref<Quat> value);
	void read(Ref<float> value);
	void read(Ref<double> value);
	void read(Ref<bool> value);
	void read(Ref<u64> value);
	void read(Ref<i64> value);
	void read(Ref<u32> value);
	void read(Ref<i32> value);
	void read(Ref<u16> value);
	void read(Ref<u8> value);
	void read(Ref<i8> value);
	void read(Span<char> value);
	void read(Ref<String> value);

	void skip();
	u32 readU32();
	i32 readI32();
	u64 readU64();

	InputMemoryStream& blob;
};


}