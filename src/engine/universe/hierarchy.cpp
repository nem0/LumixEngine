#include "hierarchy.h"
#include "engine/blob.h"
#include "engine/engine.h"
#include "engine/hash_map.h"
#include "engine/json_serializer.h"
#include "engine/property_register.h"
#include "universe.h"


namespace Lumix
{


static const ComponentType HIERARCHY_TYPE_HANDLE = PropertyRegister::getComponentType("hierarchy");


class HierarchyImpl : public Hierarchy
{
private:
	typedef HashMap<Entity, Entity> Parents;

public:
	HierarchyImpl(IPlugin& system, Universe& universe, IAllocator& allocator)
		: m_universe(universe)
		, m_parents(allocator)
		, m_children(allocator)
		, m_allocator(allocator)
		, m_system(system)
	{
		universe.registerComponentTypeScene(HIERARCHY_TYPE_HANDLE, this);
		m_is_processing = false;
		universe.entityDestroyed().bind<HierarchyImpl, &HierarchyImpl::onEntityDestroyed>(this);
		universe.entityTransformed().bind<HierarchyImpl, &HierarchyImpl::onEntityMoved>(this);
	}


	void clear() override
	{
		auto iter = m_children.begin(), end = m_children.end();
		while (iter != end)
		{
			LUMIX_DELETE(m_allocator, iter.value());
			++iter;
		}
		m_children.clear();
		m_parents.clear();
	}


	ComponentHandle createComponent(ComponentType type, Entity entity) override 
	{
		if (HIERARCHY_TYPE_HANDLE == type)
		{
			m_parents.insert(entity, INVALID_ENTITY);
			m_universe.addComponent(entity, type, this, {entity.index});
			return {entity.index};
		}
		return INVALID_COMPONENT;
	}


	void destroyComponent(ComponentHandle component, ComponentType type) override 
	{
		if (HIERARCHY_TYPE_HANDLE == type)
		{
			Entity entity = {component.index};
			auto parent_iter = m_parents.find(entity);

			if (parent_iter.isValid())
			{
				auto iter = m_children.find(parent_iter.value());
				if (iter != m_children.end())
				{
					LUMIX_DELETE(m_allocator, iter.value());
					m_children.erase(iter);
				}
				m_parents.erase(parent_iter);
			}
			
			m_universe.destroyComponent(entity, type, this, component);
		}
	}


	IPlugin& getPlugin() const override { return m_system; }
	void update(float time_delta, bool paused) override {}
	Universe& getUniverse() override { return m_universe; }
	IAllocator& getAllocator() { return m_allocator; }


	ComponentHandle getComponent(Entity entity, ComponentType type) override
	{
		ComponentHandle cmp = {entity.index};
		return m_parents.find(entity) != m_parents.end() ? cmp : INVALID_COMPONENT;
	}


	void onEntityDestroyed(Entity entity)
	{
		auto iter = m_children.find(entity);
		if (iter != m_children.end())
		{
			for (auto& x : *iter.value())
			{
				m_parents.erase(x.m_entity);
			}
			LUMIX_DELETE(m_allocator, iter.value());
			m_children.erase(iter);
		}
	}


	void onEntityMoved(Entity entity)
	{
		bool was_processing = m_is_processing;
		m_is_processing = true;
		Children::iterator iter = m_children.find(entity);
		if (iter.isValid())
		{
			Transform parent_transform = m_universe.getTransform(entity);
			Array<Child>& children = *iter.value();
			for (int i = 0, c = children.size(); i < c; ++i)
			{
				m_universe.setTransform(children[i].m_entity, parent_transform * children[i].m_local_transform);
			}
		}
		m_is_processing = was_processing;

		if (m_is_processing) return;

		Parents::iterator parent_iter = m_parents.find(entity);
		if (parent_iter.isValid())
		{
			Entity parent(parent_iter.value());
			Children::iterator child_iter = m_children.find(parent);
			if (child_iter.isValid())
			{
				Array<Child>& children = *child_iter.value();
				for (int i = 0, c = children.size(); i < c; ++i)
				{
					if (children[i].m_entity == entity)
					{
						Transform parent_transform = m_universe.getTransform(parent);
						children[i].m_local_transform = parent_transform.inverted() * m_universe.getTransform(entity);
						break;
					}
				}
			}
		}
	}


	void setLocalPosition(ComponentHandle cmp, const Vec3& position) override
	{
		Entity entity = {cmp.index};
		Parents::iterator parent_iter = m_parents.find(entity);

		if (parent_iter.isValid())
		{
			Quat parent_rot = m_universe.getRotation(parent_iter.value());
			Vec3 parent_pos = m_universe.getPosition(parent_iter.value());
			m_universe.setPosition(entity, parent_pos + parent_rot.rotate(position));
			return;
		}

		m_universe.setPosition(entity, position);
	}


	Vec3 getLocalPosition(ComponentHandle cmp) override
	{
		Entity entity = {cmp.index};
		Parents::iterator parent_iter = m_parents.find(entity);

		if (parent_iter.isValid() && isValid(parent_iter.value()))
		{
			auto child_iter = m_children.find(parent_iter.value());
			ASSERT(child_iter.isValid());

			for (auto& child : *child_iter.value())
			{
				if (child.m_entity == entity)
				{
					return child.m_local_transform.pos;
				}
			}
		}

		return m_universe.getPosition(entity);
	}


	void setLocalRotationEuler(ComponentHandle cmp, const Vec3& rotation) override
	{
		Quat q;
		q.fromEuler(rotation);
		setLocalRotation(cmp, q);
	}


	Vec3 getLocalRotationEuler(ComponentHandle cmp) override
	{
		return getLocalRotation(cmp).toEuler();
	}


	void setLocalRotation(ComponentHandle cmp, const Quat& rotation) override
	{
		Entity entity = { cmp.index };
		Parents::iterator parent_iter = m_parents.find(entity);

		if (parent_iter.isValid())
		{
			Quat parent_rot = m_universe.getRotation(parent_iter.value());
			m_universe.setRotation(entity, parent_rot * rotation);
			return;
		}

		m_universe.setRotation(entity, rotation);
	}


	Quat getLocalRotation(ComponentHandle cmp) override
	{
		Entity entity = {cmp.index};
		Parents::iterator parent_iter = m_parents.find(entity);

		if (parent_iter.isValid())
		{
			auto child_iter = m_children.find(parent_iter.value());
			ASSERT(child_iter.isValid());

			for (auto& child : *child_iter.value())
			{
				if (child.m_entity == entity)
				{
					Quat rot = child.m_local_transform.rot;
					return rot;
				}
			}
		}

		return m_universe.getRotation(entity);
	}

	const Children& getAllChildren() const override { return m_children; }


	void setParent(ComponentHandle child, Entity parent) override
	{
		Entity child_entity = {child.index};
		Parents::iterator old_parent_iter = m_parents.find(child_entity);
		if (old_parent_iter.isValid())
		{
			if (isValid(old_parent_iter.value()))
			{
				Children::iterator child_iter = m_children.find(old_parent_iter.value());
				ASSERT(child_iter.isValid());
				Array<Child>& children = *child_iter.value();
				for (int i = 0; i < children.size(); ++i)
				{
					if (children[i].m_entity == child_entity)
					{
						children.erase(i);
						break;
					}
				}
			}
			m_parents.erase(old_parent_iter);
		}

		if (isValid(parent))
		{
			m_parents.insert(child_entity, parent);

			Children::iterator child_iter = m_children.find(parent);
			if (!child_iter.isValid())
			{
				m_children.insert(parent, LUMIX_NEW(m_allocator, Array<Child>)(m_allocator));
				child_iter = m_children.find(parent);
			}
			Child& c = child_iter.value()->emplace();
			c.m_entity = child_entity;
			Transform parent_transform = m_universe.getTransform(parent);
			c.m_local_transform = parent_transform.inverted() * m_universe.getTransform(child_entity);
		}
	}


	Entity getParent(ComponentHandle child) override
	{
		Entity child_entity = {child.index};
		Parents::iterator parent_iter = m_parents.find(child_entity);
		if (parent_iter.isValid())
		{
			return Entity(parent_iter.value());
		}
		return INVALID_ENTITY;
	}


	void serialize(OutputBlob& serializer) override
	{
		int size = m_parents.size();
		serializer.write((int32)size);
		Parents::iterator iter = m_parents.begin(), end = m_parents.end();
		while (iter != end)
		{
			serializer.write(iter.key());
			serializer.write(iter.value());
			++iter;
		}
	}


	void deserialize(InputBlob& serializer, int /*version*/) override
	{
		int32 size;
		serializer.read(size);
		for (int i = 0; i < size; ++i)
		{
			Entity child, parent;
			serializer.read(child);
			serializer.read(parent);
			ComponentHandle cmp = {child.index};
			setParent(cmp, parent);
			m_universe.addComponent(child, HIERARCHY_TYPE_HANDLE, this, cmp);
		}
	}


	Array<Child>* getChildren(Entity parent) override
	{
		Children::iterator iter = m_children.find(parent);
		if (iter.isValid())
		{
			return iter.value();
		}
		return nullptr;
	}


private:
	IAllocator& m_allocator;
	Universe& m_universe;
	Parents m_parents;
	Children m_children;
	IPlugin& m_system;
	bool m_is_processing;
};


IScene* HierarchyPlugin::createScene(Universe& ctx)
{
	return Hierarchy::create(*this, ctx, m_allocator);
}


void HierarchyPlugin::destroyScene(IScene* scene)
{
	Hierarchy::destroy(static_cast<Hierarchy*>(scene));
}


Hierarchy* Hierarchy::create(IPlugin& system, Universe& universe, IAllocator& allocator)
{
	return LUMIX_NEW(allocator, HierarchyImpl)(system, universe, allocator);
}


void Hierarchy::destroy(Hierarchy* hierarchy)
{
	LUMIX_DELETE(static_cast<HierarchyImpl*>(hierarchy)->getAllocator(), hierarchy);
}


} // namespace Lumix
