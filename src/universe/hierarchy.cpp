#include "hierarchy.h"
#include "core/array.h"
#include "core/hash_map.h"
#include "core/iserializer.h"
#include "core/matrix.h"
#include "universe.h"


namespace Lumix
{


class HierarchyImpl : public Hierarchy
{
	private:
		typedef HashMap<int32_t, Array<Child> > Children;
		typedef HashMap<int32_t, int32_t> Parents;

	public:
		HierarchyImpl(Universe& universe, IAllocator& allocator)
			: m_universe(universe)
			, m_parents(allocator)
			, m_children(allocator)
			, m_allocator(allocator)
			, m_parent_set(allocator)
		{
			universe.entityMoved().bind<HierarchyImpl, &HierarchyImpl::onEntityMoved>(this);
		}


		IAllocator& getAllocator()
		{
			return m_allocator;
		}


		void onEntityMoved(const Entity& entity)
		{
			Children::iterator iter = m_children.find(entity.index);
			if(iter.isValid())
			{
				Matrix parent_matrix = entity.getMatrix();
				Array<Child>& children = iter.value();
				for(int i = 0, c = children.size(); i < c; ++i)
				{
					Entity e(&m_universe, children[i].m_entity);
					e.setMatrix(parent_matrix * children[i].m_local_matrix);
				}
			}

			Parents::iterator parent_iter = m_parents.find(entity.index);
			if(parent_iter.isValid())
			{
				Entity parent(&m_universe, parent_iter.value());
				Children::iterator child_iter = m_children.find(parent.index);
				if(child_iter.isValid())
				{
					Array<Child>& children = child_iter.value();
					for(int i = 0, c = children.size(); i < c; ++i)
					{
						if(children[i].m_entity == entity.index)
						{
							Matrix inv_parent_matrix = parent.getMatrix();
							inv_parent_matrix.inverse();
							children[i].m_local_matrix = inv_parent_matrix * entity.getMatrix();
							break;
						}
					}
				}
			}
		}

		
		virtual void setParent(const Entity& child, const Entity& parent) override
		{
			Parents::iterator old_parent_iter = m_parents.find(child.index);
			if(old_parent_iter.isValid())
			{
				Children::iterator child_iter = m_children.find(old_parent_iter.value());
				ASSERT(child_iter.isValid());
				Array<Child>& children = child_iter.value();
				for(int i = 0; i < children.size(); ++i)
				{
					if(children[i].m_entity == child.index)
					{
						children.erase(i);
						break;
					}
				}
				m_parents.erase(old_parent_iter);

			}

			if(parent.index >= 0)
			{
				m_parents.insert(child.index, parent.index);
			
				Children::iterator child_iter = m_children.find(parent.index);
				if(!child_iter.isValid())
				{
					m_children.insert(parent.index, Array<Child>());
					child_iter = m_children.find(parent.index);
				}
				Child& c = child_iter.value().pushEmpty();
				c.m_entity = child.index;
				Matrix inv_parent_matrix = parent.getMatrix();
				inv_parent_matrix.inverse();
				c.m_local_matrix = inv_parent_matrix * child.getMatrix();
			}
			m_parent_set.invoke(child, parent);
		}
		
		
		virtual Entity getParent(const Entity& child) override
		{
			Parents::iterator parent_iter = m_parents.find(child.index);
			if(parent_iter.isValid())
			{
				return Entity(&m_universe, parent_iter.value());
			}
			return Entity::INVALID;
		}
		
		
		virtual void serialize(ISerializer& serializer) override
		{
			int size = m_parents.size();
			serializer.serialize("hierarchy_size", size);
			serializer.beginArray("hierarchy");
			Parents::iterator iter = m_parents.begin(), end = m_parents.end();
			while(iter != end)
			{
				serializer.serializeArrayItem(iter.key());			
				serializer.serializeArrayItem(iter.value());			
				++iter;
			}
			serializer.endArray();
		}
		
		
		virtual void deserialize(ISerializer& serializer) override
		{
			int size;
			serializer.deserialize("hierarchy_size", size);
			serializer.deserializeArrayBegin("hierarchy");
			for(int i = 0; i < size; ++i)
			{
				int32_t child, parent;
				serializer.deserializeArrayItem(child);			
				serializer.deserializeArrayItem(parent);
				setParent(Entity(&m_universe, child), Entity(&m_universe, parent));
			}
			serializer.deserializeArrayEnd();
		}


		virtual DelegateList<void (const Entity&, const Entity&)>& parentSet() override
		{
			return m_parent_set;
		}

			
		virtual Array<Child>* getChildren(const Entity& parent) override
		{
			Children::iterator iter = m_children.find(parent.index);
			if(iter.isValid())
			{
				return &iter.value();
			}
			return NULL;
		}

	private:
		IAllocator& m_allocator;
		Universe& m_universe;
		Parents m_parents;
		Children m_children;
		DelegateList<void (const Entity&, const Entity&)> m_parent_set;
};


Hierarchy* Hierarchy::create(Universe& universe, IAllocator& allocator)
{
	return allocator.newObject<HierarchyImpl>(universe, allocator);
}


void Hierarchy::destroy(Hierarchy* hierarchy)
{
	static_cast<HierarchyImpl*>(hierarchy)->getAllocator().deleteObject(hierarchy);
}


} // namespace Lumix