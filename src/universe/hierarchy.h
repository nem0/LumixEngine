#pragma once


#include "universe/entity.h"
#include "core/delegate_list.h"


namespace Lumix
{


	class ISerializer;


	class Hierarchy
	{
		public:
			class Child
			{
				public:
					int32_t m_entity;
					Matrix m_local_matrix;
			};


		public:
			static Hierarchy* create(Universe& universe, IAllocator& allocator);
			static void destroy(Hierarchy* hierarchy);

			virtual ~Hierarchy() {}

			virtual void setParent(const Entity& child, const Entity& parent) = 0;
			virtual Entity getParent(const Entity& child) = 0;
			virtual void serialize(ISerializer& serializer) = 0;
			virtual void deserialize(ISerializer& serializer) = 0;
			virtual Array<Child>* getChildren(const Entity& parent) = 0;
			virtual DelegateList<void (const Entity&, const Entity&)>& parentSet() = 0;
	};


} // namespace Lumix