#include "hierarchy.h"
#include "engine/core/blob.h"
#include "engine/core/crc32.h"
#include "engine/core/hash_map.h"
#include "engine/core/json_serializer.h"
#include "engine/engine.h"
#include "universe.h"


namespace Lumix
{


static const Lumix::uint32 HIERARCHY_HASH = Lumix::crc32("hierarchy");


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
		m_is_processing = false;
		universe.entityDestroyed().bind<HierarchyImpl, &HierarchyImpl::onEntityDestroyed>(this);
		universe.entityTransformed().bind<HierarchyImpl, &HierarchyImpl::onEntityMoved>(this);
	}


	~HierarchyImpl()
	{
		auto iter = m_children.begin(), end = m_children.end();
		while (iter != end)
		{
			LUMIX_DELETE(m_allocator, iter.value());
			++iter;
		}
	}


	ComponentIndex createComponent(uint32 type, Entity entity) override 
	{
		if (HIERARCHY_HASH == type)
		{
			m_parents.insert(entity, INVALID_ENTITY);
			m_universe.addComponent(entity, type, this, entity);
		}
		return INVALID_COMPONENT;
	}


	void destroyComponent(ComponentIndex component, uint32 type) override 
	{
		if (HIERARCHY_HASH == type)
		{
			auto parent_iter = m_parents.find(component);

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
			
			m_universe.destroyComponent(component, type, this, component);
		}
	}


	IPlugin& getPlugin() const override { return m_system; }
	void update(float time_delta, bool paused) override {}
	bool ownComponentType(uint32 type) const override { return HIERARCHY_HASH == type; }
	Universe& getUniverse() override { return m_universe; }
	IAllocator& getAllocator() { return m_allocator; }


	ComponentIndex getComponent(Entity entity, uint32 type) override
	{
		ASSERT(ownComponentType(type));
		return m_parents.find(entity) != m_parents.end() ? entity : INVALID_COMPONENT;
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
			Matrix parent_matrix = m_universe.getPositionAndRotation(entity);
			Array<Child>& children = *iter.value();
			for (int i = 0, c = children.size(); i < c; ++i)
			{
				m_universe.setMatrix(
					children[i].m_entity, parent_matrix * children[i].m_local_matrix);
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
						Matrix inv_parent_matrix = m_universe.getPositionAndRotation(parent);
						inv_parent_matrix.inverse();
						children[i].m_local_matrix =
							inv_parent_matrix * m_universe.getPositionAndRotation(entity);
						break;
					}
				}
			}
		}
	}


	void setLocalPosition(ComponentIndex cmp, const Vec3& position) override
	{
		Parents::iterator parent_iter = m_parents.find(cmp);

		if (parent_iter.isValid())
		{
			Quat parent_rot = m_universe.getRotation(parent_iter.value());
			Vec3 parent_pos = m_universe.getPosition(parent_iter.value());
			m_universe.setPosition(cmp, parent_pos + parent_rot * position);
			return;
		}

		m_universe.setPosition(cmp, position);
	}


	Vec3 getLocalPosition(ComponentIndex cmp) override
	{
		Parents::iterator parent_iter = m_parents.find(cmp);

		if (parent_iter.isValid() && parent_iter.value() != INVALID_ENTITY)
		{
			auto child_iter = m_children.find(parent_iter.value());
			ASSERT(child_iter.isValid());

			for (auto& child : *child_iter.value())
			{
				if (child.m_entity == cmp)
				{
					return child.m_local_matrix.getTranslation();
				}
			}
		}

		return m_universe.getPosition(cmp);
	}


	void setLocalRotation(Entity entity, const Quat& rotation) override
	{
		Parents::iterator parent_iter = m_parents.find(entity);

		if (parent_iter.isValid())
		{
			Quat parent_rot = m_universe.getRotation(parent_iter.value());
			m_universe.setRotation(entity, rotation * parent_rot);
			return;
		}

		m_universe.setRotation(entity, rotation);
	}


	Quat getLocalRotation(ComponentIndex cmp) override
	{
		Parents::iterator parent_iter = m_parents.find(cmp);

		if (parent_iter.isValid())
		{
			auto child_iter = m_children.find(parent_iter.value());
			ASSERT(child_iter.isValid());

			for (auto& child : *child_iter.value())
			{
				if (child.m_entity == cmp)
				{
					Quat rot;
					child.m_local_matrix.getRotation(rot);
					return rot;
				}
			}
		}

		return m_universe.getRotation(cmp);
	}

	const Children& getAllChildren() const override { return m_children; }


	void setParent(ComponentIndex child, Entity parent) override
	{
		Parents::iterator old_parent_iter = m_parents.find(child);
		if (old_parent_iter.isValid())
		{
			if (old_parent_iter.value() != INVALID_ENTITY)
			{
				Children::iterator child_iter = m_children.find(old_parent_iter.value());
				ASSERT(child_iter.isValid());
				Array<Child>& children = *child_iter.value();
				for (int i = 0; i < children.size(); ++i)
				{
					if (children[i].m_entity == child)
					{
						children.erase(i);
						break;
					}
				}
			}
			m_parents.erase(old_parent_iter);
		}

		if (parent >= 0)
		{
			m_parents.insert(child, parent);

			Children::iterator child_iter = m_children.find(parent);
			if (!child_iter.isValid())
			{
				m_children.insert(parent, LUMIX_NEW(m_allocator, Array<Child>)(m_allocator));
				child_iter = m_children.find(parent);
			}
			Child& c = child_iter.value()->emplace();
			c.m_entity = child;
			Matrix inv_parent_matrix = m_universe.getPositionAndRotation(parent);
			inv_parent_matrix.inverse();
			c.m_local_matrix = inv_parent_matrix * m_universe.getPositionAndRotation(child);
		}
	}


	Entity getParent(ComponentIndex child) override
	{
		Parents::iterator parent_iter = m_parents.find(child);
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
			int32 child, parent;
			serializer.read(child);
			serializer.read(parent);
			setParent(Entity(child), Entity(parent));
			m_universe.addComponent(child, HIERARCHY_HASH, this, child);
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
