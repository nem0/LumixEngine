#include "hierarchy.h"
#include "core/array.h"
#include "core/blob.h"
#include "core/hash_map.h"
#include "core/json_serializer.h"
#include "core/matrix.h"
#include "core/pod_hash_map.h"
#include "universe.h"


namespace Lumix
{


class HierarchyImpl : public Hierarchy
{
private:
	
	typedef HashMap<int32_t, int32_t> Parents;

public:
	HierarchyImpl(Universe& universe, IAllocator& allocator)
		: m_universe(universe)
		, m_parents(allocator)
		, m_children(allocator)
		, m_allocator(allocator)
		, m_parent_set(allocator)
	{
		m_is_processing = false;
		universe.entityTransformed()
			.bind<HierarchyImpl, &HierarchyImpl::onEntityMoved>(this);
	}


	~HierarchyImpl()
	{
		PODHashMap<int32_t, Array<Child> *>::iterator iter = m_children.begin(),
													  end = m_children.end();
		while (iter != end)
		{
			m_allocator.deleteObject(iter.value());
			++iter;
		}
	}


	IAllocator& getAllocator() { return m_allocator; }


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
				m_universe.setMatrix(children[i].m_entity,
									 parent_matrix *
										 children[i].m_local_matrix);
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
						Matrix inv_parent_matrix =
							m_universe.getPositionAndRotation(parent);
						inv_parent_matrix.inverse();
						children[i].m_local_matrix =
							inv_parent_matrix *
							m_universe.getPositionAndRotation(entity);
						break;
					}
				}
			}
		}
	}


	virtual void setLocalRotation(Entity entity, const Quat& rotation) override
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


	virtual const Children& getAllChildren() const override
	{
		return m_children;
	}


	virtual void setParent(Entity child, Entity parent) override
	{
		Parents::iterator old_parent_iter = m_parents.find(child);
		if (old_parent_iter.isValid())
		{
			Children::iterator child_iter =
				m_children.find(old_parent_iter.value());
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
			m_parents.erase(old_parent_iter);
		}

		if (parent >= 0)
		{
			m_parents.insert(child, parent);

			Children::iterator child_iter = m_children.find(parent);
			if (!child_iter.isValid())
			{
				m_children.insert(
					parent,
					m_allocator.newObject<Array<Child>>(m_allocator));
				child_iter = m_children.find(parent);
			}
			Child& c = child_iter.value()->pushEmpty();
			c.m_entity = child;
			Matrix inv_parent_matrix = m_universe.getPositionAndRotation(parent);
			inv_parent_matrix.inverse();
			c.m_local_matrix = inv_parent_matrix * m_universe.getPositionAndRotation(child);
		}
		m_parent_set.invoke(child, parent);
	}


	virtual Entity getParent(Entity child) override
	{
		Parents::iterator parent_iter = m_parents.find(child);
		if (parent_iter.isValid())
		{
			return Entity(parent_iter.value());
		}
		return INVALID_ENTITY;
	}


	virtual void serialize(OutputBlob& serializer) override
	{
		int size = m_parents.size();
		serializer.write((int32_t)size);
		Parents::iterator iter = m_parents.begin(), end = m_parents.end();
		while (iter != end)
		{
			serializer.write(iter.key());
			serializer.write(iter.value());
			++iter;
		}
	}


	virtual void deserialize(InputBlob& serializer) override
	{
		int32_t size;
		serializer.read(size);
		for (int i = 0; i < size; ++i)
		{
			int32_t child, parent;
			serializer.read(child);
			serializer.read(parent);
			setParent(Entity(child), Entity(parent));
		}
	}


	virtual DelegateList<void(Entity, Entity)>&
	parentSet() override
	{
		return m_parent_set;
	}


	virtual Array<Child>* getChildren(Entity parent) override
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
	DelegateList<void(Entity, Entity)> m_parent_set;
	bool m_is_processing;
};


Hierarchy* Hierarchy::create(Universe& universe, IAllocator& allocator)
{
	return allocator.newObject<HierarchyImpl>(universe, allocator);
}


void Hierarchy::destroy(Hierarchy* hierarchy)
{
	static_cast<HierarchyImpl*>(hierarchy)->getAllocator().deleteObject(
		hierarchy);
}


} // namespace Lumix