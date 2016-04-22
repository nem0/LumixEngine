#pragma once


#include "lumix.h"
#include "engine/core/matrix.h"
#include "engine/core/hash_map.h"
#include "engine/iplugin.h"


namespace Lumix
{


	class InputBlob;
	class OutputBlob;
	class Universe;
	template <typename T> class Array;


	class HierarchyPlugin : public IPlugin
	{
	public:
		explicit HierarchyPlugin(IAllocator& allocator) : m_allocator(allocator) {}

		bool create() override { return true; }
		void destroy() override {}
		const char* getName() const override { return "hierarchy"; }

		IScene* createScene(Universe&) override;
		void destroyScene(IScene*) override;
	
	private:
		IAllocator& m_allocator;
	};


	class Hierarchy : public IScene
	{
		public:
			class Child
			{
				public:
					Entity m_entity;
					Matrix m_local_matrix;
			};

			typedef HashMap<int32, Array<Child>*> Children;

		public:
			static Hierarchy* create(IPlugin& system, Universe& universe, IAllocator& allocator);
			static void destroy(Hierarchy* hierarchy);

			virtual ~Hierarchy() {}

			virtual void setLocalPosition(ComponentIndex cmp, const Vec3& position) = 0;
			virtual Vec3 getLocalPosition(ComponentIndex cmp) = 0;
			virtual void setLocalRotation(Entity entity, const Quat& rotation) = 0;
			virtual Quat getLocalRotation(ComponentIndex cmp) = 0;
			virtual void setParent(ComponentIndex cmp, Entity parent) = 0;
			virtual Entity getParent(ComponentIndex cmp) = 0;
			virtual Array<Child>* getChildren(Entity parent) = 0;
			virtual const Children& getAllChildren() const = 0;
	};


} // namespace Lumix
