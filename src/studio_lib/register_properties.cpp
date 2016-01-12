#include "editor/property_register.h"
#include "core/crc32.h"
#include "core/resource_manager.h"
#include "editor/property_descriptor.h"
#include "editor/world_editor.h"
#include "universe/hierarchy.h"
#include "utils.h"


using namespace Lumix;


template <class S> class EntityEnumPropertyDescriptor : public IEnumPropertyDescriptor
{
public:
	typedef Entity (S::*Getter)(ComponentIndex);
	typedef void (S::*Setter)(ComponentIndex, Entity);
	typedef Entity(S::*ArrayGetter)(ComponentIndex, int);
	typedef void (S::*ArraySetter)(ComponentIndex, int, Entity);

public:
	EntityEnumPropertyDescriptor(const char* name,
		Getter _getter,
		Setter _setter,
		Lumix::WorldEditor& editor,
		IAllocator& allocator)
		: IEnumPropertyDescriptor(allocator)
		, m_editor(editor)
	{
		setName(name);
		m_single.getter = _getter;
		m_single.setter = _setter;
		m_type = ENUM;
	}


	EntityEnumPropertyDescriptor(const char* name,
		ArrayGetter _getter,
		ArraySetter _setter,
		Lumix::WorldEditor& editor,
		IAllocator& allocator)
		: IEnumPropertyDescriptor(allocator)
		, m_editor(editor)
	{
		setName(name);
		m_array.getter = _getter;
		m_array.setter = _setter;
		m_type = ENUM;
	}


	void set(ComponentUID cmp, int index, InputBlob& stream) const override
	{
		int value;
		stream.read(&value, sizeof(value));
		auto entity = value < 0 ? INVALID_ENTITY : m_editor.getUniverse()->getEntityFromDenseIdx(value);
		if (index == -1)
		{
			(static_cast<S*>(cmp.scene)->*m_single.setter)(cmp.index, entity);
		}
		else
		{
			(static_cast<S*>(cmp.scene)->*m_array.setter)(cmp.index, index, entity);
		}
	};


	void get(ComponentUID cmp, int index, OutputBlob& stream) const override
	{
		Entity value;
		if (index == -1)
		{
			value = (static_cast<S*>(cmp.scene)->*m_single.getter)(cmp.index);
		}
		else
		{
			value = (static_cast<S*>(cmp.scene)->*m_array.getter)(cmp.index, index);
		}
		auto dense_idx = m_editor.getUniverse()->getDenseIdx(value);
		int len = sizeof(dense_idx);
		stream.write(&dense_idx, len);
	};


	int getEnumCount(IScene* scene) override { return scene->getUniverse().getEntityCount(); }


	const char* getEnumItemName(IScene* scene, int index) override { return nullptr; }


	void getEnumItemName(IScene* scene, int index, char* buf, int max_size) override
	{
		auto entity = scene->getUniverse().getEntityFromDenseIdx(index);
		getEntityListDisplayName(m_editor, buf, max_size, entity);
	}

private:
	union
	{
		struct
		{
			Getter getter;
			Setter setter;
		} m_single;
		struct
		{
			ArrayGetter getter;
			ArraySetter setter;
		} m_array;
	};
	Lumix::WorldEditor& m_editor;
};


void registerEngineProperties(Lumix::WorldEditor& editor)
{
	PropertyRegister::registerComponentType("hierarchy", "Hierarchy");
	IAllocator& allocator = editor.getAllocator();
	PropertyRegister::add("hierarchy",
		LUMIX_NEW(allocator, EntityEnumPropertyDescriptor<Hierarchy>)("parent",
									 &Hierarchy::getParent,
									 &Hierarchy::setParent,
									 editor,
									 allocator));

}


void registerProperties(WorldEditor& editor)
{
	registerEngineProperties(editor);
}