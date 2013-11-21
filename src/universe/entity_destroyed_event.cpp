#include "entity_destroyed_event.h"
#include "core/crc32.h"


namespace Lux
{


Event::Type EntityDestroyedEvent::type = crc32("entity_destroyed");


} // !namespace Lux