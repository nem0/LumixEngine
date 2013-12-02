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
			EntityNamesMap(Universe* universe);

			Entity getEntityByName(const char* entity_name) const;
			const char* getEntityName(int uid) const;
			bool setEntityName(const char* entity_name, int uid);
			void removeEntityName(int uid);

			void serialize(ISerializer& serializer);
			void deserialize(ISerializer& serializer);

		private:
			Lux::map<Lux::string, int> m_names_map;
			Universe* m_universe;
	};

} // !namespace Lux