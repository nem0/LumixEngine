#pragma once


#include "lumix.h"


namespace Lumix
{

class InputBlob;
class OutputBlob;
struct Quat;
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

struct IEntityGUIDMap
{
	virtual Entity get(EntityGUID guid) = 0;
	virtual EntityGUID get(Entity entity) = 0;
};


struct ISerializer
{
	virtual void write(const char* label, Entity entity) = 0;
	virtual void write(const char* label, ComponentHandle value) = 0;
	virtual void write(const char* label, const Transform& value) = 0;
	virtual void write(const char* label, const Vec4& value) = 0;
	virtual void write(const char* label, const Vec3& value) = 0;
	virtual void write(const char* label, const Quat& value) = 0;
	virtual void write(const char* label, float value) = 0;
	virtual void write(const char* label, bool value) = 0;
	virtual void write(const char* label, i64 value) = 0;
	virtual void write(const char* label, u64 value) = 0;
	virtual void write(const char* label, i32 value) = 0;
	virtual void write(const char* label, u32 value) = 0;
	virtual void write(const char* label, i8 value) = 0;
	virtual void write(const char* label, u8 value) = 0;
	virtual void write(const char* label, const char* value) = 0;
	virtual EntityGUID getGUID(Entity entity) = 0;
};


struct IDeserializer
{
	virtual void read(Entity* entity) = 0;
	virtual void read(ComponentHandle* value) = 0;
	virtual void read(Transform* value) = 0;
	virtual void read(Vec4* value) = 0;
	virtual void read(Vec3* value) = 0;
	virtual void read(Quat* value) = 0;
	virtual void read(float* value) = 0;
	virtual void read(bool* value) = 0;
	virtual void read(u64* value) = 0;
	virtual void read(i64* value) = 0;
	virtual void read(u32* value) = 0;
	virtual void read(i32* value) = 0;
	virtual void read(u8* value) = 0;
	virtual void read(i8* value) = 0;
	virtual void read(char* value, int max_size) = 0;
	virtual Entity getEntity(EntityGUID guid) = 0;
};


struct LUMIX_ENGINE_API TextSerializer LUMIX_FINAL : public ISerializer
{
	TextSerializer(OutputBlob& _blob, IEntityGUIDMap& _entity_map)
		: blob(_blob)
		, entity_map(_entity_map)
	{
	}

	void write(const char* label, Entity entity)  override;
	void write(const char* label, ComponentHandle value)  override;
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
	void write(const char* label, i8 value)  override;
	void write(const char* label, u8 value)  override;
	void write(const char* label, const char* value)  override;
	EntityGUID getGUID(Entity entity) override;

	OutputBlob& blob;
	IEntityGUIDMap& entity_map;
};


struct LUMIX_ENGINE_API TextDeserializer LUMIX_FINAL : public IDeserializer
{
	TextDeserializer(InputBlob& _blob, IEntityGUIDMap& _entity_map)
		: blob(_blob)
		, entity_map(_entity_map)
	{
	}

	void read(Entity* entity)  override;
	void read(ComponentHandle* value)  override;
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
	void read(u8* value)  override;
	void read(i8* value)  override;
	void read(char* value, int max_size)  override;
	Entity getEntity(EntityGUID guid) override;

	void skip();
	u32 readU32();

	InputBlob& blob;
	IEntityGUIDMap& entity_map;
};


}