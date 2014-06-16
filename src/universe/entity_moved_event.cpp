#include "entity_moved_event.h"
#include "core/crc32.h"


namespace Lumix
{


const Event::Type EntityMovedEvent::type = crc32("entity_moved");


} // !namespace Lumix
