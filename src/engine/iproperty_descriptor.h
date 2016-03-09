#pragma once


#include "core/array.h"
#include "universe/universe.h"


namespace Lumix
{


class LUMIX_ENGINE_API IPropertyDescriptor
{
public:
	enum Type
	{
		RESOURCE = 0,
		FILE,
		DECIMAL,
		BOOL,
		VEC3,
		INTEGER,
		STRING,
		ARRAY,
		COLOR,
		VEC4,
		VEC2,
		SAMPLED_FUNCTION,
		ENUM,
		INT2,
		ENTITY
	};

public:
	IPropertyDescriptor(IAllocator& allocator)
		: m_name(allocator)
		, m_children(allocator)
	{
	}
	virtual ~IPropertyDescriptor() {}

	virtual void set(ComponentUID cmp, int index, InputBlob& stream) const = 0;
	virtual void get(ComponentUID cmp, int index, OutputBlob& stream) const = 0;

	Type getType() const { return m_type; }
	uint32 getNameHash() const { return m_name_hash; }
	const char* getName() const { return m_name.c_str(); }
	void setName(const char* name);
	void addChild(IPropertyDescriptor* child) { m_children.push(child); }
	const Array<IPropertyDescriptor*>& getChildren() const { return m_children; }
	Array<IPropertyDescriptor*>& getChildren() { return m_children; }

protected:
	uint32 m_name_hash;
	string m_name;
	Type m_type;
	Array<IPropertyDescriptor*> m_children;
};


class LUMIX_ENGINE_API IDecimalPropertyDescriptor : public IPropertyDescriptor
{
public:
	IDecimalPropertyDescriptor(IAllocator& allocator);

	float getMin() const { return m_min; }
	float getMax() const { return m_max; }
	float getStep() const { return m_step; }

	void setMin(float value) { m_min = value; }
	void setMax(float value) { m_max = value; }
	void setStep(float value) { m_step = value; }

protected:
	float m_min;
	float m_max;
	float m_step;
};


class ResourcePropertyDescriptorBase
{
public:
	ResourcePropertyDescriptorBase(uint32 resource_type) { m_resource_type = resource_type; }

	uint32 getResourceType() { return m_resource_type; }

	uint32 m_resource_type;
};


class IEnumPropertyDescriptor : public IPropertyDescriptor
{
public:
	IEnumPropertyDescriptor(IAllocator& allocator)
		: IPropertyDescriptor(allocator)
	{
	}

	virtual int getEnumCount(IScene* scene) = 0;
	virtual const char* getEnumItemName(IScene* scene, int index) = 0;
	virtual void getEnumItemName(IScene* scene, int index, char* buf, int max_size) {}
};


class ISampledFunctionDescriptor : public IPropertyDescriptor
{
public:
	ISampledFunctionDescriptor(IAllocator& allocator)
		: IPropertyDescriptor(allocator)
	{
	}

	virtual float getMaxX() = 0;
	virtual float getMaxY() = 0;
};


class IArrayDescriptor : public IPropertyDescriptor
{
public:
	IArrayDescriptor(IAllocator& allocator)
		: IPropertyDescriptor(allocator)
	{
	}

	virtual void removeArrayItem(ComponentUID cmp, int index) const = 0;
	virtual void addArrayItem(ComponentUID cmp, int index) const = 0;
	virtual int getCount(ComponentUID cmp) const = 0;
	virtual bool canAdd() const = 0;
	virtual bool canRemove() const = 0;
};


} // namespace Lumix