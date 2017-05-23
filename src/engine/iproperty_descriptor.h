#pragma once


#include "engine/array.h"
#include "engine/universe/universe.h"


namespace Lumix
{


class LUMIX_ENGINE_API PropertyDescriptorBase
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
		UNSIGNED_INTEGER,
		STRING,
		ARRAY,
		COLOR,
		VEC4,
		VEC2,
		SAMPLED_FUNCTION,
		ENUM,
		INT2,
		ENTITY,
		BLOB
	};

public:
	PropertyDescriptorBase()
		: m_is_in_radians(false)
	{
	}
	virtual ~PropertyDescriptorBase() {}

	virtual void set(ComponentUID cmp, int index, InputBlob& stream) const = 0;
	virtual void get(ComponentUID cmp, int index, OutputBlob& stream) const = 0;

	Type getType() const { return m_type; }
	u32 getNameHash() const { return m_name_hash; }
	const char* getName() const { return m_name; }
	void setName(const char* name);
	PropertyDescriptorBase& setIsInRadians(bool is) { m_is_in_radians = is; return *this; }
	bool isInRadians() const { return m_is_in_radians; }

protected:
	bool m_is_in_radians;
	u32 m_name_hash;
	StaticString<32> m_name;
	Type m_type;
};


template<typename T>
class NumericPropertyDescriptorBase : public PropertyDescriptorBase
{
public:
	T getMin() const { return m_min; }
	T getMax() const { return m_max; }
	T getStep() const { return m_step; }

	void setMin(T value) { m_min = value; }
	void setMax(T value) { m_max = value; }
	void setStep(T value) { m_step = value; }

protected:
	T m_min;
	T m_max;
	T m_step;
};


struct IResourcePropertyDescriptor : public PropertyDescriptorBase
{
	IResourcePropertyDescriptor()
	{
		PropertyDescriptorBase::m_type = PropertyDescriptorBase::RESOURCE;
	}

	virtual struct ResourceType getResourceType() = 0;
};


struct IEnumPropertyDescriptor : public PropertyDescriptorBase
{
	virtual int getEnumCount(IScene* scene, ComponentHandle cmp) = 0;
	virtual const char* getEnumItemName(IScene* scene, ComponentHandle cmp, int index) = 0;
	virtual void getEnumItemName(IScene* scene, ComponentHandle cmp, int index, char* buf, int max_size) {}
};


struct ISampledFunctionDescriptor : public PropertyDescriptorBase
{
	virtual float getMaxX() = 0;
	virtual float getMaxY() = 0;
};


class ArrayDescriptorBase : public PropertyDescriptorBase
{
public:
	ArrayDescriptorBase(IAllocator& allocator)
		: m_children(allocator)
		, m_allocator(allocator)
	{
	}

	~ArrayDescriptorBase()
	{
		for (auto* child : m_children)
		{
			LUMIX_DELETE(m_allocator, child);
		}
	}

	virtual void removeArrayItem(ComponentUID cmp, int index) const = 0;
	virtual void addArrayItem(ComponentUID cmp, int index) const = 0;
	virtual int getCount(ComponentUID cmp) const = 0;
	virtual bool canAdd() const = 0;
	virtual bool canRemove() const = 0;
	void addChild(PropertyDescriptorBase* child) { m_children.push(child); }
	const Array<PropertyDescriptorBase*>& getChildren() const { return m_children; }

protected:
	Array<PropertyDescriptorBase*> m_children;
	IAllocator& m_allocator;
};


} // namespace Lumix
