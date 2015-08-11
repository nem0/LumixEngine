#pragma once


#include "lumix.h"
#include "core/delegate_list.h"
#include "core/string.h"


namespace Lumix
{

	class InputBlob;
	class OutputBlob;
	struct Vec3;
	class WorldEditor;

	class LUMIX_ENGINE_API EntityTemplateSystem
	{
		public:
			static EntityTemplateSystem* create(WorldEditor& editor);
			static void destroy(EntityTemplateSystem* system);

			virtual ~EntityTemplateSystem() {}
			virtual void serialize(OutputBlob& serializer) = 0;
			virtual void deserialize(InputBlob& serializer) = 0;
			virtual void createTemplateFromEntity(const char* name, Entity entity) = 0;
			virtual uint32_t getTemplate(Entity entity) = 0;
			virtual const Array<Entity>& getInstances(uint32_t template_name_hash) = 0;
			virtual Array<string>& getTemplateNames() = 0;
			virtual Entity createInstance(const char* name, const Vec3& position) = 0;

			virtual DelegateList<void()>& updated() = 0;
	};


} // namespace Lumix