#pragma once


#include "lumix.h"
#include "core/array.h"
#include "core/delegate_list.h"
#include "core/matrix.h"
#include "core/pod_hash_map.h"


namespace Lumix
{


	class InputBlob;
	class OutputBlob;
	class Universe;


	class Hierarchy
	{
		public:
			class Child
			{
				public:
					Entity m_entity;
					Matrix m_local_matrix;
			};

			typedef PODHashMap<int32, Array<Child>*> Children;

		public:
			static Hierarchy* create(Universe& universe, IAllocator& allocator);
			static void destroy(Hierarchy* hierarchy);

			virtual ~Hierarchy() {}

			virtual void setLocalRotation(Entity entity, const Quat& rotation) = 0;
			virtual void setParent(Entity child, Entity parent) = 0;
			virtual Entity getParent(Entity child) = 0;
			virtual void serialize(OutputBlob& serializer) = 0;
			virtual void deserialize(InputBlob& serializer) = 0;
			virtual Array<Child>* getChildren(Entity parent) = 0;
			virtual DelegateList<void (Entity, Entity)>& parentSet() = 0;
			virtual const Children& getAllChildren() const = 0;
	};


} // namespace Lumix