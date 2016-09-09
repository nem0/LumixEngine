#pragma once


#include "engine/lumix.h"
#include "engine/matrix.h"
#include "engine/hash_map.h"
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
					Transform m_local_transform;
			};

			typedef HashMap<Entity, Array<Child>*> Children;

		public:
			static Hierarchy* create(IPlugin& system, Universe& universe, IAllocator& allocator);
			static void destroy(Hierarchy* hierarchy);

			virtual ~Hierarchy() {}

			virtual void setLocalPosition(ComponentHandle cmp, const Vec3& position) = 0;
			virtual Vec3 getLocalPosition(ComponentHandle cmp) = 0;
			virtual void setLocalRotation(ComponentHandle cmp, const Quat& rotation) = 0;
			virtual Quat getLocalRotation(ComponentHandle cmp) = 0;
			virtual void setLocalRotationEuler(ComponentHandle cmp, const Vec3& rotation) = 0;
			virtual Vec3 getLocalRotationEuler(ComponentHandle cmp) = 0;
			virtual void setParent(ComponentHandle cmp, Entity parent) = 0;
			virtual Entity getParent(ComponentHandle cmp) = 0;
			virtual Array<Child>* getChildren(Entity parent) = 0;
			virtual const Children& getAllChildren() const = 0;
	};


} // namespace Lumix
