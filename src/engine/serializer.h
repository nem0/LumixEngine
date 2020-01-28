#pragma once


#include "lumix.h"


namespace Lumix
{

struct DVec3;
class InputMemoryStream;
class OutputMemoryStream;
struct LocalRigidTransform;
struct Quat;
struct RigidTransform;
class String;
struct Transform;
struct IVec3;
struct Vec3;
struct Vec4;


struct LUMIX_ENGINE_API ISerializer
{
	virtual ~ISerializer() {}

	virtual void write(const char* label, EntityPtr entity) = 0;
	virtual void write(const char* label, EntityRef entity) = 0;
	virtual void write(const char* label, const Transform& value) = 0;
	virtual void write(const char* label, const RigidTransform& value) = 0;
	virtual void write(const char* label, const LocalRigidTransform& value) = 0;
	virtual void write(const char* label, const Vec4& value) = 0;
	virtual void write(const char* label, const DVec3& value) = 0;
	virtual void write(const char* label, const Vec3& value) = 0;
	virtual void write(const char* label, const IVec3& value) = 0;
	virtual void write(const char* label, const Quat& value) = 0;
	virtual void write(const char* label, float value) = 0;
	virtual void write(const char* label, double value) = 0;
	virtual void write(const char* label, bool value) = 0;
	virtual void write(const char* label, i64 value) = 0;
	virtual void write(const char* label, u64 value) = 0;
	virtual void write(const char* label, i32 value) = 0;
	virtual void write(const char* label, u32 value) = 0;
	virtual void write(const char* label, u16 value) = 0;
	virtual void write(const char* label, i8 value) = 0;
	virtual void write(const char* label, u8 value) = 0;
	virtual void write(const char* label, const char* value) = 0;
};


struct LUMIX_ENGINE_API IDeserializer
{
	virtual ~IDeserializer() {}

	virtual void read(Ref<EntityPtr> entity) = 0;
	virtual void read(Ref<EntityRef> entity) = 0;
	virtual void read(Ref<Transform> value) = 0;
	virtual void read(Ref<RigidTransform> value) = 0;
	virtual void read(Ref<LocalRigidTransform> value) = 0;
	virtual void read(Ref<Vec4> value) = 0;
	virtual void read(Ref<DVec3> value) = 0;
	virtual void read(Ref<Vec3> value) = 0;
	virtual void read(Ref<IVec3> value) = 0;
	virtual void read(Ref<Quat> value) = 0;
	virtual void read(Ref<float> value) = 0;
	virtual void read(Ref<double> value) = 0;
	virtual void read(Ref<bool> value) = 0;
	virtual void read(Ref<u64> value) = 0;
	virtual void read(Ref<i64> value) = 0;
	virtual void read(Ref<u32> value) = 0;
	virtual void read(Ref<i32> value) = 0;
	virtual void read(Ref<u16> value) = 0;
	virtual void read(Ref<u8> value) = 0;
	virtual void read(Ref<i8> value) = 0;
	virtual void read(Span<char> value) = 0;
	virtual void read(Ref<String> value) = 0;
};


struct LUMIX_ENGINE_API TextSerializer final : public ISerializer
{
	TextSerializer(OutputMemoryStream& _blob)
		: blob(_blob)
	{
	}

	void write(const char* label, EntityPtr entity)  override;
	void write(const char* label, EntityRef entity)  override;
	void write(const char* label, const RigidTransform& value)  override;
	void write(const char* label, const LocalRigidTransform& value)  override;
	void write(const char* label, const Transform& value)  override;
	void write(const char* label, const Vec4& value)  override;
	void write(const char* label, const DVec3& value)  override;
	void write(const char* label, const Vec3& value)  override;
	void write(const char* label, const IVec3& value)  override;
	void write(const char* label, const Quat& value)  override;
	void write(const char* label, float value)  override;
	void write(const char* label, double value)  override;
	void write(const char* label, bool value)  override;
	void write(const char* label, i64 value)  override;
	void write(const char* label, u64 value)  override;
	void write(const char* label, i32 value)  override;
	void write(const char* label, u32 value)  override;
	void write(const char* label, u16 value)  override;
	void write(const char* label, i8 value)  override;
	void write(const char* label, u8 value)  override;
	void write(const char* label, const char* value)  override;

	OutputMemoryStream& blob;
};


struct LUMIX_ENGINE_API TextDeserializer final : public IDeserializer
{
	TextDeserializer(InputMemoryStream& _blob)
		: blob(_blob)
	{
	}

	void read(Ref<EntityPtr> entity)  override;
	void read(Ref<EntityRef> entity)  override;
	void read(Ref<RigidTransform> value)  override;
	void read(Ref<LocalRigidTransform> value)  override;
	void read(Ref<Transform> value)  override;
	void read(Ref<Vec4> value)  override;
	void read(Ref<DVec3> value)  override;
	void read(Ref<Vec3> value)  override;
	void read(Ref<IVec3> value)  override;
	void read(Ref<Quat> value)  override;
	void read(Ref<float> value)  override;
	void read(Ref<double> value)  override;
	void read(Ref<bool> value)  override;
	void read(Ref<u64> value)  override;
	void read(Ref<i64> value)  override;
	void read(Ref<u32> value)  override;
	void read(Ref<i32> value)  override;
	void read(Ref<u16> value)  override;
	void read(Ref<u8> value)  override;
	void read(Ref<i8> value)  override;
	void read(Span<char> value)  override;
	void read(Ref<String> value)  override;

	void skip();
	u32 readU32();
	i32 readI32();
	u64 readU64();

	InputMemoryStream& blob;
};


}