#pragma once


#include "lumix.h"


namespace Lumix
{

class InputBlob;
class OutputBlob;
struct Quat;
struct RigidTransform;
class string;
struct Transform;
struct Vec3;
struct Vec4;

struct EntityGUID
{
	u64 value;
	bool operator ==(const EntityGUID& rhs) const { return value == rhs.value; }
};


const EntityGUID INVALID_ENTITY_GUID = { 0xffffFFFFffffFFFF };
inline bool isValid(EntityGUID guid) { return guid.value != INVALID_ENTITY_GUID.value; }


struct ISaveEntityGUIDMap
{
	virtual ~ISaveEntityGUIDMap() {}
	virtual EntityGUID get(EntityPtr entity) = 0;
};


struct ILoadEntityGUIDMap
{
	virtual ~ILoadEntityGUIDMap() {}
	virtual EntityPtr get(EntityGUID guid) = 0;
};


struct LUMIX_ENGINE_API ISerializer
{
	virtual ~ISerializer() {}

	virtual void write(const char* label, EntityPtr entity) = 0;
	virtual void write(const char* label, EntityRef entity) = 0;
	virtual void write(const char* label, const Transform& value) = 0;
	virtual void write(const char* label, const RigidTransform& value) = 0;
	virtual void write(const char* label, const Vec4& value) = 0;
	virtual void write(const char* label, const Vec3& value) = 0;
	virtual void write(const char* label, const Quat& value) = 0;
	virtual void write(const char* label, float value) = 0;
	virtual void write(const char* label, bool value) = 0;
	virtual void write(const char* label, i64 value) = 0;
	virtual void write(const char* label, u64 value) = 0;
	virtual void write(const char* label, i32 value) = 0;
	virtual void write(const char* label, u32 value) = 0;
	virtual void write(const char* label, u16 value) = 0;
	virtual void write(const char* label, i8 value) = 0;
	virtual void write(const char* label, u8 value) = 0;
	virtual void write(const char* label, const char* value) = 0;
	virtual EntityGUID getGUID(EntityRef entity) = 0;
};


struct LUMIX_ENGINE_API IDeserializer
{
	virtual ~IDeserializer() {}

	virtual void read(EntityPtr* entity) = 0;
	virtual void read(EntityRef* entity) = 0;
	virtual void read(Transform* value) = 0;
	virtual void read(RigidTransform* value) = 0;
	virtual void read(Vec4* value) = 0;
	virtual void read(Vec3* value) = 0;
	virtual void read(Quat* value) = 0;
	virtual void read(float* value) = 0;
	virtual void read(bool* value) = 0;
	virtual void read(u64* value) = 0;
	virtual void read(i64* value) = 0;
	virtual void read(u32* value) = 0;
	virtual void read(i32* value) = 0;
	virtual void read(u16* value) = 0;
	virtual void read(u8* value) = 0;
	virtual void read(i8* value) = 0;
	virtual void read(char* value, int max_size) = 0;
	virtual void read(string* value) = 0;
	virtual EntityPtr getEntity(EntityGUID guid) = 0;
};


struct LUMIX_ENGINE_API TextSerializer final : public ISerializer
{
	TextSerializer(OutputBlob& _blob, ISaveEntityGUIDMap& _entity_map)
		: blob(_blob)
		, entity_map(_entity_map)
	{
	}

	void write(const char* label, EntityPtr entity)  override;
	void write(const char* label, EntityRef entity)  override;
	void write(const char* label, const RigidTransform& value)  override;
	void write(const char* label, const Transform& value)  override;
	void write(const char* label, const Vec4& value)  override;
	void write(const char* label, const Vec3& value)  override;
	void write(const char* label, const Quat& value)  override;
	void write(const char* label, float value)  override;
	void write(const char* label, bool value)  override;
	void write(const char* label, i64 value)  override;
	void write(const char* label, u64 value)  override;
	void write(const char* label, i32 value)  override;
	void write(const char* label, u32 value)  override;
	void write(const char* label, u16 value)  override;
	void write(const char* label, i8 value)  override;
	void write(const char* label, u8 value)  override;
	void write(const char* label, const char* value)  override;
	EntityGUID getGUID(EntityRef entity) override;

	OutputBlob& blob;
	ISaveEntityGUIDMap& entity_map;
};


struct LUMIX_ENGINE_API TextDeserializer final : public IDeserializer
{
	TextDeserializer(InputBlob& _blob, ILoadEntityGUIDMap& _entity_map)
		: blob(_blob)
		, entity_map(_entity_map)
	{
	}

	void read(EntityPtr* entity)  override;
	void read(EntityRef* entity)  override;
	void read(RigidTransform* value)  override;
	void read(Transform* value)  override;
	void read(Vec4* value)  override;
	void read(Vec3* value)  override;
	void read(Quat* value)  override;
	void read(float* value)  override;
	void read(bool* value)  override;
	void read(u64* value)  override;
	void read(i64* value)  override;
	void read(u32* value)  override;
	void read(i32* value)  override;
	void read(u16* value)  override;
	void read(u8* value)  override;
	void read(i8* value)  override;
	void read(char* value, int max_size)  override;
	void read(string* value)  override;
	EntityPtr getEntity(EntityGUID guid) override;

	void skip();
	u32 readU32();

	InputBlob& blob;
	ILoadEntityGUIDMap& entity_map;
};


}