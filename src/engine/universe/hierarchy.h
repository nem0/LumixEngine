#pragma once


#include "core/delegate_list.h"
#include "lumix.h"
#include "core/matrix.h"


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


		public:
			static Hierarchy* create(Universe& universe, IAllocator& allocator);
			static void destroy(Hierarchy* hierarchy);

			virtual ~Hierarchy() {}

			virtual void setParent(Entity child, Entity parent) = 0;
			virtual Entity getParent(Entity child) = 0;
			virtual void serialize(OutputBlob& serializer) = 0;
			virtual void deserialize(InputBlob& serializer) = 0;
			virtual Array<Child>* getChildren(Entity parent) = 0;
			virtual DelegateList<void (Entity, Entity)>& parentSet() = 0;
	};


} // namespace Lumix