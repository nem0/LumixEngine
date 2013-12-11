#pragma once

#include "core/lux.h"
#include "core/map.h"
#include "core/string.h"

#include "universe/universe.h"

class ISerializer;

namespace Lux
{
	class LUX_ENGINE_API EntityNamesMap LUX_FINAL
	{
		public:
			void setUniverse(Universe* universe);

			Entity getEntityByName(const char* entity_name) const;
			const char* getEntityName(const Entity& entity) const;
			bool setEntityName(const char* entity_name, const Entity& entity);
			void removeEntityName(const Entity& entity);

			void serialize(ISerializer& serializer);
			void deserialize(ISerializer& serializer);

		private:
			map<string, int> m_names_map; //TODO: remove
			map<uint32_t, int> m_crc_names_map;
			Universe* m_universe;
	};

} // !namespace Lux