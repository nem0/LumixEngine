#pragma once


#include "core/lumix.h"
#include "core/delegate_list.h"
#include "core/string.h"
#include "universe/entity.h"


namespace Lumix
{

	class ISerializer;
	class WorldEditor;

	class LUMIX_ENGINE_API EntityTemplateSystem
	{
		public:
			static EntityTemplateSystem* create(WorldEditor& editor);
			static void destroy(EntityTemplateSystem* system);

			virtual ~EntityTemplateSystem() {}
			virtual void serialize(ISerializer& serializer) = 0;
			virtual void deserialize(ISerializer& serializer) = 0;
			virtual void createTemplateFromEntity(const char* name, const Entity& entity) = 0;
			virtual uint32_t getTemplate(const Entity& entity) = 0;
			virtual const Array<Entity>& getInstances(uint32_t template_name_hash) = 0;
			virtual class Array<string>& getTemplateNames() = 0;
			virtual Entity createInstance(const char* name) = 0;

			virtual DelegateList<void()>& updated() = 0;
	};


} // namespace Lumix