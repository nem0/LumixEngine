#include "entity_destroyed_event.h"
#include "core/crc32.h"


namespace Lumix
{


const Event::Type EntityDestroyedEvent::type = crc32("entity_destroyed");


} // !namespace Lumix
